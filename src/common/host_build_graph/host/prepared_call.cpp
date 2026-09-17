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

/**
 * host_build_graph prepared call. See host_build_graph/prepared_call.h for the
 * contract.
 */

#include "host_build_graph/prepared_call.h"

#include <array>
#include <cinttypes>
#include <cstring>
#include <mutex>
#include <utility>

#include "assert_compat.h"
#include "common/unified_log.h"
#include "host_build_graph/graph_execution.h"
#include "host_build_graph/runtime_types.h"
#include "worker/runtime_c_api.h"

namespace simpler::hbg {

namespace {

// One task's three records, resolved from the image's storage segment.
ChipTaskStorage *image_storage(char *image_base, const sm_layout::BindUsage &usage) {
    return reinterpret_cast<ChipTaskStorage *>(
        image_base + sm_layout::segment_offsets(sm_layout::image_extents(usage)).storage
    );
}

const ChipTaskStorage *image_storage(const char *image_base, const sm_layout::BindUsage &usage) {
    return reinterpret_cast<const ChipTaskStorage *>(
        image_base + sm_layout::segment_offsets(sm_layout::image_extents(usage)).storage
    );
}

// Fold one value into a digest. A 64-bit FNV-1a step: the digest only has to
// separate incompatible shapes, and every input is a compile-time constant or this
// bind's task capacity.
constexpr uint64_t digest_step(uint64_t digest, uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        digest ^= (value >> (byte * 8)) & 0xFFU;
        digest *= 0x100000001B3ULL;
    }
    return digest;
}

struct RegisterTable {
    std::mutex mutex;
    std::array<PreparedCallPtr, kPreparedCallRegisterCount + 1> slots{};
};

RegisterTable &registers() {
    static RegisterTable table;
    return table;
}

struct Metrics {
    std::mutex mutex;
    PreparedCallMetrics values{};
};

Metrics &metrics_state() {
    static Metrics state;
    return state;
}

}  // namespace

void AlignedBuffer::resize(size_t bytes) {
    if (bytes == 0) {
        storage_.clear();
        bytes_ = 0;
        return;
    }
    // The extra CHIP_ALIGN_SIZE is the headroom the base round-up may consume, so
    // `bytes` usable bytes sit behind data() whatever address the allocator returns.
    storage_.assign(bytes + CHIP_ALIGN_SIZE, std::byte{0});
    bytes_ = bytes;
}

void AlignedBuffer::assign(const char *source, size_t bytes) {
    resize(bytes);
    if (bytes != 0 && source != nullptr) {
        std::memcpy(data(), source, bytes);
    }
}

const char *prepared_call_reuse_bar_name(PreparedCallReuseBar bar) noexcept {
    switch (bar) {
    case PreparedCallReuseBar::None:
        return "none";
    case PreparedCallReuseBar::HostOrchestrationWroteDeviceTensor:
        return "host orchestration wrote a device-memory caller tensor";
    }
    return "unknown";
}

uint64_t PreparedCall::retained_host_bytes() const noexcept {
    return static_cast<uint64_t>(image.host_bytes()) + static_cast<uint64_t>(copied_zone.size()) +
           static_cast<uint64_t>(definition_block.size()) +
           static_cast<uint64_t>(definition_bindings.size() * sizeof(DefinitionBinding)) +
           static_cast<uint64_t>(arg_slices.size() * sizeof(ArgSliceRelocation)) +
           static_cast<uint64_t>(args.size() * sizeof(PreparedArg));
}

uint64_t PreparedCall::restored_device_bytes() const noexcept {
    return static_cast<uint64_t>(copied_zone.size()) + image_bytes() + static_cast<uint64_t>(definition_block.size());
}

uint64_t prepared_call_layout_digest(uint64_t task_capacity) noexcept {
    uint64_t digest = 0xCBF29CE484222325ULL;
    digest = digest_step(digest, task_capacity);
    digest = digest_step(digest, sizeof(ChipTaskStorage));
    digest = digest_step(digest, sizeof(TaskDescriptor));
    digest = digest_step(digest, sizeof(ChipTaskSlotState));
    digest = digest_step(digest, sizeof(TaskPayload));
    digest = digest_step(digest, sizeof(SharedMemoryHeader));
    digest = digest_step(digest, sizeof(simpler::hbg::Tensor));
    digest = digest_step(digest, sizeof(GraphDefinitionHeader));
    digest = digest_step(digest, sizeof(RuntimeArenaLayout));
    digest = digest_step(digest, CHIP_ALIGN_SIZE);
    digest = digest_step(digest, CHIP_MAX_FANIN);
    digest = digest_step(digest, MAX_TENSOR_ARGS);
    digest = digest_step(digest, MAX_SCALAR_ARGS);
    digest = digest_step(digest, HEAP_VIRTUAL_BASE);
    return digest;
}

uint64_t prepared_arg_geometry_digest(const ChipTensor &tensor) noexcept {
    uint64_t digest = 0xCBF29CE484222325ULL;
    digest = digest_step(digest, tensor.start_offset);
    digest = digest_step(digest, tensor.buffer.size);
    digest = digest_step(digest, tensor.ndims);
    digest = digest_step(digest, static_cast<uint64_t>(tensor.dtype));
    digest = digest_step(digest, static_cast<uint64_t>(tensor.address_space));
    for (uint32_t dim = 0; dim < tensor.ndims && dim < MAX_TENSOR_DIMS; ++dim) {
        digest = digest_step(digest, tensor.shapes[dim]);
        digest = digest_step(digest, tensor.strides[dim]);
    }
    return digest;
}

bool prepared_call_collect_arg_slices(PreparedCall &call, uint64_t temp_base, uint64_t temp_bytes) {
    call.arg_slices.clear();
    call.temp_bytes = temp_bytes;
    if (temp_base == 0 || temp_bytes == 0) {
        return true;
    }
    const char *base = call.image.data();
    if (base == nullptr) {
        return true;
    }
    const ChipTaskStorage *storage = image_storage(base, call.usage);
    for (uint64_t task = 0; task < call.usage.submitted_tasks; ++task) {
        const TaskPayload &payload = storage[task].payload;
        const simpler::hbg::Tensor *tensors = payload.tensor_data();
        if (tensors == nullptr) continue;
        for (int32_t index = 0; index < payload.tensor_count; ++index) {
            const uint64_t addr = tensors[index].buffer.addr;
            if (addr < temp_base || addr - temp_base >= temp_bytes) continue;
            const uint64_t offset = addr - temp_base;
            // An address inside the span that belongs to no declared slice means the
            // argument contract does not describe everything the image addresses, so
            // a later execution would be bound from an incomplete list.
            bool declared = false;
            for (const PreparedArg &arg : call.args) {
                if (arg.kind != PreparedArgKind::HostMemory) continue;
                if (offset >= arg.slice_offset && offset - arg.slice_offset < arg.nbytes) {
                    declared = true;
                    break;
                }
            }
            if (!declared) {
                LOG_ERROR(
                    "prepared call: task %" PRIu64 " tensor %d addresses temp-buffer offset %" PRIu64
                    " that no argument slice declares",
                    task, index, offset
                );
                return false;
            }
            call.arg_slices.push_back(ArgSliceRelocation{task, index, offset});
        }
    }
    return true;
}

void prepared_call_bind_image(
    const PreparedCall &call, char *out, uint64_t temp_base, const sm_layout::HeapRebase &heap,
    uint64_t definition_block_base
) {
    always_assert(out != nullptr);
    always_assert(reinterpret_cast<uintptr_t>(out) % CHIP_ALIGN_SIZE == 0);
    std::memcpy(out, call.image.data(), call.image.size());

    ChipTaskStorage *storage = image_storage(out, call.usage);

    // Argument slices first: their addresses are real device addresses both before
    // and after, so this pass must not see the heap window and the heap rebase must
    // not see a stale slice address.
    for (const ArgSliceRelocation &relocation : call.arg_slices) {
        TaskPayload &payload = storage[relocation.task_index].payload;
        simpler::hbg::Tensor *tensors = payload.tensor_data();
        always_assert(tensors != nullptr && relocation.tensor_index < payload.tensor_count);
        tensors[relocation.tensor_index].buffer.addr = temp_base + relocation.slice_offset;
    }

    sm_layout::rebase_image(out, call.usage, heap);

    for (const DefinitionBinding &binding : call.definition_bindings) {
        ChipTaskSlotState &slot = storage[binding.task_index].slot;
        always_assert(slot.task_kind == TaskKind::GRAPH);
        slot.graph_context =
            reinterpret_cast<void *>(definition_block_base + binding.object_offset + sizeof(GraphDefinitionHeader));
    }
}

bool prepared_call_submission_compatible(
    const PreparedCall &call, const std::vector<PreparedArg> &submission, const std::vector<uint64_t> &scalars,
    std::string *why
) {
    auto reject = [why](std::string message) {
        if (why != nullptr) *why = std::move(message);
        return false;
    };
    if (submission.size() != call.args.size()) {
        return reject(
            "argument count changed: prepared " + std::to_string(call.args.size()) + ", submitted " +
            std::to_string(submission.size())
        );
    }
    for (size_t index = 0; index < submission.size(); ++index) {
        const PreparedArg &prepared = call.args[index];
        const PreparedArg &current = submission[index];
        const std::string at = "argument " + std::to_string(index) + ": ";
        if (prepared.kind != current.kind) {
            return reject(at + "address space changed");
        }
        if (prepared.direction != current.direction) {
            return reject(at + "direction changed");
        }
        if (prepared.nbytes != current.nbytes) {
            return reject(
                at + "size changed: prepared " + std::to_string(prepared.nbytes) + ", submitted " +
                std::to_string(current.nbytes)
            );
        }
        // Equal byte counts can still be different geometry, and orchestration
        // reads geometry to shape the graph.
        if (prepared.geometry != current.geometry) {
            return reject(at + "geometry changed, so the prepared graph was built for a different view");
        }
        if (prepared.kind == PreparedArgKind::HostMemory && prepared.slice_offset != current.slice_offset) {
            return reject(at + "temp-buffer slice moved, so the prepared relocations no longer describe it");
        }
        // A caller-owned device address is the one class of address publication may
        // not move, so a submission that changed one cannot be served by this result.
        if (prepared.kind == PreparedArgKind::DeviceMemory && prepared.device_addr != current.device_addr) {
            return reject(at + "caller device address changed");
        }
    }
    if (scalars.size() != call.scalars.size()) {
        return reject(
            "scalar count changed: prepared " + std::to_string(call.scalars.size()) + ", submitted " +
            std::to_string(scalars.size())
        );
    }
    for (size_t index = 0; index < scalars.size(); ++index) {
        if (scalars[index] != call.scalars[index]) {
            return reject(
                "scalar " + std::to_string(index) +
                " changed, and orchestration may have built the graph from its value"
            );
        }
    }
    return true;
}

bool prepared_call_register_valid(uint32_t reg) noexcept { return reg >= 1 && reg <= kPreparedCallRegisterCount; }

PreparedCallPtr prepared_call_register_borrow(uint32_t reg) {
    if (!prepared_call_register_valid(reg)) return nullptr;
    RegisterTable &table = registers();
    std::scoped_lock lock(table.mutex);
    return table.slots[reg];
}

bool prepared_call_register_store(uint32_t reg, PreparedCallPtr call) {
    if (!prepared_call_register_valid(reg)) return false;
    RegisterTable &table = registers();
    std::scoped_lock lock(table.mutex);
    table.slots[reg] = std::move(call);
    return true;
}

bool prepared_call_register_release(uint32_t reg) {
    if (!prepared_call_register_valid(reg)) return false;
    PreparedCallPtr dropped;
    {
        RegisterTable &table = registers();
        std::scoped_lock lock(table.mutex);
        dropped = std::move(table.slots[reg]);
        table.slots[reg] = nullptr;
    }
    return dropped != nullptr;
}

void prepared_call_registers_clear() noexcept {
    std::array<PreparedCallPtr, kPreparedCallRegisterCount + 1> dropped{};
    {
        RegisterTable &table = registers();
        std::scoped_lock lock(table.mutex);
        dropped.swap(table.slots);
    }
}

PreparedCallMetrics prepared_call_metrics() {
    PreparedCallMetrics values;
    {
        Metrics &state = metrics_state();
        std::scoped_lock lock(state.mutex);
        values = state.values;
    }
    // Retention is a property of what the registers hold right now, not a running
    // total: a released result stops costing host memory the moment it is dropped.
    uint64_t retained = 0;
    {
        RegisterTable &table = registers();
        std::scoped_lock lock(table.mutex);
        for (const PreparedCallPtr &call : table.slots) {
            if (call != nullptr) retained += call->retained_host_bytes();
        }
    }
    values.retained_host_bytes = retained;
    return values;
}

void prepared_call_metrics_reset() noexcept {
    Metrics &state = metrics_state();
    std::scoped_lock lock(state.mutex);
    state.values = PreparedCallMetrics{};
}

void prepared_call_note_host_orchestration() noexcept {
    Metrics &state = metrics_state();
    std::scoped_lock lock(state.mutex);
    ++state.values.host_orchestration_entries;
}

void prepared_call_note_definition_pack() noexcept {
    Metrics &state = metrics_state();
    std::scoped_lock lock(state.mutex);
    ++state.values.definition_packs;
}

void prepared_call_note_sealed() noexcept {
    Metrics &state = metrics_state();
    std::scoped_lock lock(state.mutex);
    ++state.values.calls_sealed;
}

void prepared_call_note_published(uint64_t restored_device_bytes, bool reused) noexcept {
    Metrics &state = metrics_state();
    std::scoped_lock lock(state.mutex);
    ++state.values.publications;
    if (reused) ++state.values.reused_publications;
    state.values.last_restored_device_bytes = restored_device_bytes;
}

}  // namespace simpler::hbg

// The runtime-side half of the prepared-call C entries. The shared platform layer
// declares these weak and answers PTO_RUNTIME_ERR_UNSUPPORTED; host_build_graph is
// the runtime that retains a preparation result, so linking this translation unit
// is what turns the entries on.
extern "C" {

int prepared_call_release_impl(uint32_t reg) {
    if (!simpler::hbg::prepared_call_register_valid(reg)) {
        return PTO_RUNTIME_ERR_INTERNAL;
    }
    // Releasing an empty register is not an error: a caller tearing down does not
    // have to know whether a run ever sealed into it.
    (void)simpler::hbg::prepared_call_register_release(reg);
    return 0;
}

int prepared_call_metrics_impl(SimplerPreparedCallMetrics *out) {
    if (out == nullptr) {
        return PTO_RUNTIME_ERR_INTERNAL;
    }
    const simpler::hbg::PreparedCallMetrics metrics = simpler::hbg::prepared_call_metrics();
    out->host_orchestration_entries = metrics.host_orchestration_entries;
    out->definition_packs = metrics.definition_packs;
    out->calls_sealed = metrics.calls_sealed;
    out->publications = metrics.publications;
    out->reused_publications = metrics.reused_publications;
    out->retained_host_bytes = metrics.retained_host_bytes;
    out->last_restored_device_bytes = metrics.last_restored_device_bytes;
    return 0;
}

int prepared_call_metrics_reset_impl(void) {
    simpler::hbg::prepared_call_metrics_reset();
    return 0;
}

int prepared_call_drop_all_impl(void) {
    simpler::hbg::prepared_call_registers_clear();
    return 0;
}

}  // extern "C"
