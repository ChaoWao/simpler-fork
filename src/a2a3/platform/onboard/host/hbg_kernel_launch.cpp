/*
 * Copyright (c) PyPTO Contributors.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 * -----------------------------------------------------------------------------------------------------------
 */
#include "device_runner_base.h"

#include <acl/acl.h>
#include <cstring>
#include <limits>
#include <atomic>

#include "common/host_api.h"
#include "host/kernel_launch_binder.h"
#include "host_build_graph/host/host_tensor_access.h"
#include "host_build_graph/host_graph_build.h"
#include "host_build_graph/kernel_argument_snapshot.h"
#include "host_build_graph/kernel_callable_registration.h"
#include "host_build_graph/kernel_resource_requirements.h"
#include "host_build_graph/kernel_graph_template.h"
#include "task_interface/kernel_invocation_validation.h"

namespace {
std::mutex claim_mutex;
std::unordered_set<int> claimed_devices;
std::atomic<uint64_t> next_graph_lease{1};

uint64_t allocate_graph_lease() noexcept {
    auto next = next_graph_lease.load(std::memory_order_relaxed);
    while (next != std::numeric_limits<uint64_t>::max()) {
        if (next_graph_lease.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) return next;
    }
    return 0;
}

int drain_owned_streams(const KernelExecutionState &state, int timeout_ms) {
    for (auto kind : {KernelStreamKind::Aicpu, KernelStreamKind::Aicore}) {
        const int rc = aclrtSynchronizeStreamWithTimeout(state.hidden_stream(kind), timeout_ms);
        if (rc != 0) return rc;
    }
    return 0;
}

int publish_control(
    DeviceRunnerBase &runner, void *stream, void *payload, size_t bytes, const char *symbol, int timeout_ms,
    bool *published = nullptr
) try {
    const int rc = runner.launch_aicpu_payload(stream, payload, bytes, symbol, 1);
    if (rc != 0) return rc;
    // Submission acceptance survives a failed completion wait. Cleanup must
    // revoke accepted work, but must not revoke a record never submitted.
    if (published != nullptr) *published = true;
    return aclrtSynchronizeStreamWithTimeout(stream, timeout_ms);
} catch (...) {
    return PTO_RUNTIME_ERR_INTERNAL;
}
}  // namespace

namespace hbg {
struct KernelContextLaunchState {
    int device{-1};
    uint64_t context_generation{0};
    uint64_t runtime_binary_id{0};
    GraphSlotRegistration registration{};
    RuntimeArenaLayout layout{};
    uintptr_t previous_caller_identity{0};
    uintptr_t graph_caller_identity{0};
    std::unordered_set<uint64_t> graph_leases;
    bool diagnostics_requested{false};
    bool registered{false};
    bool submitted{false};
    bool closing{false};
    bool uncertain{false};
    ~KernelContextLaunchState() {
        if (device >= 0) {
            std::scoped_lock lock(claim_mutex);
            claimed_devices.erase(device);
        }
    }
};
struct KernelCallableLaunchState {
    Runtime runtime;
    PersistentKernelArgs arguments;
    simpler::kernel::HbgCallableBinding binding{};
    bool published{false};
};
}  // namespace hbg

extern "C" int runtime_hbg_kernel_supported_impl() { return 1; }

int DeviceRunnerBase::admit_hbg_native_run() {
    if (hbg_kernel_ == nullptr) return 0;
    if (hbg_kernel_->closing || hbg_kernel_->uncertain || !hbg_kernel_->graph_leases.empty())
        return PTO_RUNTIME_ERR_INVALID_STATE;
    if (!hbg_kernel_->submitted) return 0;
    aclrtEventRecordedStatus status{};
    const int rc = aclrtQueryEventStatus(kernel_exec_state_.event(KernelEventKind::SerialTail), &status);
    if (rc != 0) return rc;
    return status == ACL_EVENT_RECORDED_STATUS_COMPLETE ? 0 : PTO_RUNTIME_ERR_INVALID_STATE;
}

int DeviceRunnerBase::configure_hbg_kernel_events() {
    if (runtime_hbg_kernel_supported_impl() == 0) return 0;
    uint8_t mode = 0;
    int rc = rtEventWorkModeGet(&mode);
    if (rc == ACL_ERROR_RT_FEATURE_NOT_SUPPORT) return 0;
    if (rc != 0 || mode == 1) return rc;
    rc = rtEventWorkModeSet(1);
    if (rc == 0) return 0;
    if (rtEventWorkModeGet(&mode) == 0 && mode == 1) return 0;
    return rc;
}

int DeviceRunnerBase::prepare_hbg_kernel_runtime(
    const CallConfig &config, uint64_t generation, uint64_t runtime_binary_id
) {
    if (hbg_kernel_ != nullptr) return PTO_RUNTIME_ERR_INVALID_STATE;
    auto state = std::make_shared<hbg::KernelContextLaunchState>();
    {
        std::scoped_lock lock(claim_mutex);
        if (!claimed_devices.insert(device_id_).second) return PTO_RUNTIME_ERR_INVALID_STATE;
        state->device = device_id_;
    }
    state->context_generation = generation;
    state->diagnostics_requested = config.enable_chip_swimlane || config.enable_dump_args || config.enable_pmu ||
                                   config.enable_dep_gen || config.enable_scope_stats;
    state->runtime_binary_id = runtime_binary_id;
    if (state->runtime_binary_id == 0) return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    hbg_kernel_ = state;
    if (aicore_bin_handle_ == nullptr) {
        rtDevBinary_t binary{};
        binary.magic = RT_DEV_BINARY_MAGIC_ELF;
        binary.data = aicore_kernel_binary_.data();
        binary.length = aicore_kernel_binary_.size();
        const int rc = rtRegisterAllKernel(&binary, &aicore_bin_handle_);
        if (rc != 0) return rc;
    }
    const uint64_t capacity =
        config.runtime_env.ring_task_window[0] == 0 ? CHIP_DEFAULT_GRAPH_TASKS : config.runtime_env.ring_task_window[0];
    int rc = hbg::make_kernel_graph_layout(capacity, state->layout);
    if (rc != 0) return rc;
    const uint64_t budget =
        config.runtime_env.ring_heap[0] == 0 ? UINT64_C(256) * 1024 * 1024 : config.runtime_env.ring_heap[0];
    hbg::GraphResourceRequirements requirements{
        {hbg::RuntimeArchitecture::A2A3, capacity, state->layout.arena_size, state->layout.off_copied_begin,
         state->layout.off_copied_end},
        budget,
        state->layout.off_copied_end + SharedMemoryHandle::calculate_size(capacity),
        budget,
        0
    };
    hbg::KernelResourceCapacity plan;
    rc = hbg::KernelResourceCapacity::create(&requirements, 1, plan);
    if (rc == 0) rc = plan.prepare(kernel_exec_state_, KernelResourceOps::from_allocator(mem_alloc_));
    if (rc == 0) rc = kernel_exec_state_.freeze_resources();
    if (rc == 0)
        rc = plan.make_registration(
            kernel_exec_state_, device_id_, generation, state->runtime_binary_id, state->registration
        );
    if (rc != 0) return rc;
    const auto &binding = state->registration;
    kernel_runtime_.set_gm_sm_ptr(
        reinterpret_cast<void *>(binding.destinations[1].address + state->layout.off_copied_end)
    );
    kernel_runtime_.set_prebuilt_arena(
        reinterpret_cast<void *>(binding.destinations[1].address), state->layout.off_runtime
    );
    auto stream = kernel_exec_state_.hidden_stream(KernelStreamKind::Aicpu);
    rc = publish_control(
        *this, stream, &state->registration, sizeof(state->registration),
        "simpler_aicpu_l1_hbg_register_execution_slot", timeout_config_.stream_sync_timeout_ms, &state->registered
    );
    if (rc != 0) kernel_exec_state_.poison(rc);
    return rc;
}

int DeviceRunnerBase::prepare_hbg_kernel_callable(int32_t id) {
    const auto it = callables_.find(id);
    if (hbg_kernel_ == nullptr || !hbg_kernel_->registered || hbg_kernel_->closing || it == callables_.end() ||
        it->second.host_orch_func_ptr == nullptr)
        return PTO_RUNTIME_ERR_INVALID_STATE;
    if (it->second.hbg_kernel != nullptr) return PTO_RUNTIME_ERR_INVALID_STATE;
    if (next_hbg_callable_generation_ == std::numeric_limits<uint64_t>::max()) return PTO_RUNTIME_ERR_INVALID_STATE;
    auto state = std::make_shared<hbg::KernelCallableLaunchState>();
    it->second.hbg_kernel = state;
    std::memcpy(&state->runtime.dev, &kernel_runtime_.dev, sizeof(kernel_runtime_.dev));
    int rc = bind_registered_callable_tables(state->runtime, id);
    if (rc != 0) return rc;
    auto &binding = state->binding;
    binding.callable_id = id;
    binding.context_generation = hbg_kernel_->context_generation;
    binding.callable_generation = next_hbg_callable_generation_++;
    if (simpler::kernel::derive_invocation_counts(
            it->second.signature.data(), it->second.signature.size(), &binding.tensor_count, &binding.scalar_count
        ) != simpler::kernel::InvocationStatus::Ok)
        return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    rc = state->arguments.prepare_once(state->runtime, persistent_args_ops(), device_id_);
    if (rc != 0) return rc;
    binding.runtime_address = reinterpret_cast<uint64_t>(state->arguments.args().runtime_args);
    auto stream = kernel_exec_state_.hidden_stream(KernelStreamKind::Aicpu);
    rc = publish_control(
        *this, stream, &binding, sizeof(binding), "simpler_aicpu_l1_hbg_register_callable",
        timeout_config_.stream_sync_timeout_ms, &state->published
    );
    if (rc != 0) {
        kernel_exec_state_.poison(rc);
        return rc;
    }
    return kernel_exec_state_.mark_ready_enqueued();
}

int DeviceRunnerBase::launch_hbg_kernel_callable(
    int32_t id, const ChipStorageTaskArgs &args, void *caller, const HostApi *api
) try {
    const auto live = callables_.find(id);
    if (hbg_kernel_ != nullptr && hbg_kernel_->graph_caller_identity != 0 &&
        hbg_kernel_->graph_caller_identity != reinterpret_cast<uintptr_t>(caller))
        return PTO_RUNTIME_ERR_PREPARED_INCOMPATIBLE;
    if (hbg_kernel_ == nullptr || hbg_kernel_->closing || hbg_kernel_->uncertain ||
        !kernel_exec_state_.accepts_launch() || live == callables_.end() || live->second.hbg_kernel == nullptr ||
        !live->second.hbg_kernel->published || api == nullptr)
        return PTO_RUNTIME_ERR_INVALID_STATE;
    if (hbg_kernel_->diagnostics_requested) return PTO_RUNTIME_ERR_UNSUPPORTED;
    auto &state = *live->second.hbg_kernel;
    if (args.tensor_count() != state.binding.tensor_count || args.scalar_count() != state.binding.scalar_count)
        return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    for (int i = 0; i < args.tensor_count(); ++i) {
        if (args.tensor(i).address_space != AddressSpace::DEVICE || args.tensor(i).transfer != TensorTransfer::NONE)
            return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    }
    const uint64_t hash = hbg::kernel_argument_snapshot_hash(args);
    if (hash == 0) return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    DeviceArena arena;
    const uint64_t capacity = hbg_kernel_->layout.task_capacity;
    const auto layout = runtime_reserve_layout(arena, capacity);
    if (arena.commit(DeviceArena::kDefaultBaseAlign) == nullptr) return PTO_RUNTIME_ERR_INTERNAL;
    const uint64_t sm_bytes = SharedMemoryHandle::calculate_size(capacity);
    auto *rt = runtime_init_data_from_layout(arena, layout, MODE_EXECUTE, nullptr, sm_bytes);
    if (rt == nullptr) return PTO_RUNTIME_ERR_INTERNAL;
    runtime_wire_arena_pointers(arena, layout, rt);
    rt->prebuilt_layout = layout;
    state.runtime.set_orch_args(args);
    ChipTaskArgs orchestration;
    orchestration.create_from_entry_storage(state.runtime.get_orch_args());
    HostTensorAccessor access;
    hbg::GraphBuild build;
    int rc = hbg::build_graph(
        &state.runtime, api, access, rt, sm_bytes, capacity, live->second.host_orch_func_ptr, orchestration, build
    );
    if (rc < 0) return rc;
    const hbg::GraphInvocationIdentity identity{
        id,
        args.tensor_count(),
        args.scalar_count(),
        state.binding.callable_generation,
        live->second.chip_buffer_hash,
        hash,
        live->second.aicore_image_hash
    };
    hbg::GraphLaunchTemplate graph;
    rc = hbg::make_graph_launch_template(build, *rt, hbg_kernel_->registration, identity, graph);
    if (rc != 0) return rc;
    hbg::GraphHostArgs packet;
    rc = hbg::make_graph_host_args(graph, packet);
    if (rc != 0 || packet.bytes > UINT32_MAX - LAUNCH_ENVELOPE_HEADER_BYTES) return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    std::vector<uint8_t> launch(LAUNCH_ENVELOPE_HEADER_BYTES + packet.bytes, 0);
    auto header = state.arguments.args();
    header.entry_args_offset = LAUNCH_ENVELOPE_HEADER_BYTES;
    header.entry_args_source = static_cast<uint32_t>(EntryArgsSource::HbgKernelInvocation);
    header.graph_section_offset = LAUNCH_ENVELOPE_HEADER_BYTES;
    header.graph_section_bytes = packet.bytes;
    header.graph_section_source = static_cast<uint32_t>(GraphSectionSource::RestoredSlot);
    std::memcpy(launch.data(), &header, sizeof(header));
    std::memcpy(launch.data() + LAUNCH_ENVELOPE_HEADER_BYTES, packet.storage.data(), packet.bytes);
    namespace kl = simpler::kernel_launch;
    struct Submission {
        DeviceRunnerBase *runner;
        hbg::KernelContextLaunchState *context;
        hbg::KernelCallableLaunchState *callable;
        void *caller;
        std::vector<uint8_t> *launch;
    } submission{this, hbg_kernel_.get(), &state, caller, &launch};
    kl::KernelLaunchGateOps gate;
    gate.context = &submission;
    gate.acquire = [](void *p, const kl::KernelInvocationBinding &, void *,
                      kl::KernelLaunchAdmission *out) noexcept -> int {
        auto &s = *static_cast<Submission *>(p);
        auto &context = s.runner->kernel_exec_state_;
        if (!context.accepts_launch()) return PTO_RUNTIME_ERR_INVALID_STATE;
        out->handles = {
            nullptr,
            context.hidden_stream(KernelStreamKind::Aicpu),
            context.hidden_stream(KernelStreamKind::Aicore),
            context.event(KernelEventKind::Start),
            context.event(KernelEventKind::AicoreStart),
            context.event(KernelEventKind::AicoreDone),
            context.event(KernelEventKind::AicpuDone),
            context.event(KernelEventKind::SerialTail)
        };
        out->previous_caller_identity = s.context->previous_caller_identity;
        return 0;
    };
    gate.finish = [](void *p, const kl::KernelLaunchResult &result) noexcept {
        auto &s = *static_cast<Submission *>(p);
        if (result.status == 0) {
            s.context->previous_caller_identity = reinterpret_cast<uintptr_t>(s.caller);
            s.context->submitted = true;
            if (!s.context->graph_leases.empty())
                s.context->graph_caller_identity = reinterpret_cast<uintptr_t>(s.caller);
        } else if (result.enqueue_started) {
            s.context->uncertain = true;
            s.runner->kernel_exec_state_.poison(result.status);
        }
    };
    gate.query_tail = [](void *, void *event, bool *complete) noexcept {
        aclrtEventRecordedStatus status{};
        const int rc = aclrtQueryEventStatus(event, &status);
        *complete = rc == 0 && status == ACL_EVENT_RECORDED_STATUS_COMPLETE;
        return rc;
    };
    kl::KernelLaunchOps ops;
    ops.context = &submission;
    ops.wait_event = [](void *, void *stream, void *event) noexcept {
        return aclrtStreamWaitEvent(stream, event);
    };
    ops.record_event = [](void *, void *event, void *stream) noexcept {
        return aclrtRecordEvent(event, stream);
    };
    ops.memset_handshake = [](void *p, void *stream) noexcept {
        auto &s = *static_cast<Submission *>(p);
        const auto offset = reinterpret_cast<uintptr_t>(s.callable->runtime.get_workers()) -
                            reinterpret_cast<uintptr_t>(&s.callable->runtime);
        const size_t bytes = sizeof(Handshake) * s.callable->runtime.get_worker_count();
        auto *reports = reinterpret_cast<uint8_t *>(s.callable->arguments.args().runtime_args) + offset;
        return aclrtMemsetAsync(reports, bytes, 0, bytes, stream);
    };
    ops.launch_aicore = [](void *p, void *stream) noexcept -> int {
        auto &s = *static_cast<Submission *>(p);
        try {
            s.runner->activate_launch_shape(s.callable->runtime);
            return s.runner->launch_aicore_kernel(stream, s.callable->arguments.args());
        } catch (...) {
            return PTO_RUNTIME_ERR_INTERNAL;
        }
    };
    ops.launch_aicpu = [](void *p, void *stream) noexcept -> int {
        auto &s = *static_cast<Submission *>(p);
        try {
            return s.runner->launch_aicpu_payload(
                stream, s.launch->data(), s.launch->size(), host::KernelNames::RunName,
                s.callable->runtime.get_aicpu_launch_count()
            );
        } catch (...) {
            return PTO_RUNTIME_ERR_INTERNAL;
        }
    };
    return kl::launch_bound_kernel({launch.data(), launch.size(), nullptr, 0}, caller, gate, ops).status;
} catch (...) {
    return PTO_RUNTIME_ERR_INTERNAL;
}

int DeviceRunnerBase::revoke_hbg_kernel_callable(int32_t id) {
    const auto live = callables_.find(id);
    if (live == callables_.end() || live->second.hbg_kernel == nullptr) return 0;
    auto &state = *live->second.hbg_kernel;
    if (hbg_kernel_ == nullptr || !hbg_kernel_->graph_leases.empty() || hbg_kernel_->uncertain)
        return PTO_RUNTIME_ERR_INVALID_STATE;
    if (hbg_kernel_->submitted) {
        aclrtEventRecordedStatus status{};
        const int rc = aclrtQueryEventStatus(kernel_exec_state_.event(KernelEventKind::SerialTail), &status);
        if (rc != 0) return rc;
        if (status != ACL_EVENT_RECORDED_STATUS_COMPLETE) return PTO_RUNTIME_ERR_INVALID_STATE;
    }
    auto stream = kernel_exec_state_.hidden_stream(KernelStreamKind::Aicpu);
    if (state.published) {
        int rc = publish_control(
            *this, stream, &state.binding, sizeof(state.binding), "simpler_aicpu_l1_hbg_revoke_callable",
            timeout_config_.stream_sync_timeout_ms
        );
        if (rc != 0) {
            kernel_exec_state_.poison(rc);
            return rc;
        }
        state.published = false;
    }
    const int rc = state.arguments.finalize_once();
    if (rc == 0) live->second.hbg_kernel.reset();
    return rc;
}

int DeviceRunnerBase::close_hbg_kernel_runtime() {
    if (hbg_kernel_ == nullptr) return 0;
    if (!hbg_kernel_->graph_leases.empty()) return PTO_RUNTIME_ERR_INVALID_STATE;
    hbg_kernel_->closing = true;
    if (hbg_kernel_->uncertain) {
        const int rc = drain_owned_streams(kernel_exec_state_, timeout_config_.stream_sync_timeout_ms);
        if (rc != 0) return rc;
        hbg_kernel_->uncertain = false;
        hbg_kernel_->submitted = false;
    }
    for (auto &entry : callables_) {
        const int rc = revoke_hbg_kernel_callable(entry.first);
        if (rc != 0) return rc;
    }
    if (hbg_kernel_->registered) {
        auto stream = kernel_exec_state_.hidden_stream(KernelStreamKind::Aicpu);
        hbg::GraphSlotDetachRequest detach{
            hbg_kernel_->registration.registry.address, hbg_kernel_->context_generation, hbg_kernel_->runtime_binary_id,
            device_id_, 0
        };
        int rc = publish_control(
            *this, stream, &detach, sizeof(detach), "simpler_aicpu_l1_hbg_detach_execution_slot",
            timeout_config_.stream_sync_timeout_ms
        );
        if (rc != 0) {
            kernel_exec_state_.poison(rc);
            return rc;
        }
        hbg_kernel_->registered = false;
    }
    return 0;
}

int DeviceRunnerBase::retain_hbg_kernel_graph(uint64_t *lease) {
    if (lease == nullptr) return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    if (hbg_kernel_ == nullptr || hbg_kernel_->closing || !kernel_exec_state_.accepts_launch())
        return PTO_RUNTIME_ERR_INVALID_STATE;
    const uint64_t next = allocate_graph_lease();
    if (next == 0) return PTO_RUNTIME_ERR_INVALID_STATE;
    hbg_kernel_->graph_leases.insert(next);
    *lease = next;
    return 0;
}
int DeviceRunnerBase::release_hbg_kernel_graph(uint64_t lease) {
    if (hbg_kernel_ == nullptr || hbg_kernel_->graph_leases.count(lease) == 0) return PTO_RUNTIME_ERR_INVALID_ARGUMENT;
    if (hbg_kernel_->graph_leases.size() == 1) {
        // A destroyed graph may never have replayed: its captured tail is not a
        // completion fence. Drain only owned streams after the caller retires
        // the graph, keeping the token live if completion cannot be established.
        const int rc = drain_owned_streams(kernel_exec_state_, timeout_config_.stream_sync_timeout_ms);
        if (rc != 0) return rc;
        hbg_kernel_->submitted = false;
        hbg_kernel_->previous_caller_identity = 0;
        hbg_kernel_->graph_caller_identity = 0;
    }
    hbg_kernel_->graph_leases.erase(lease);
    return 0;
}
