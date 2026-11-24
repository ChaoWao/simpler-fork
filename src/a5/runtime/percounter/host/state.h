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
 * This runtime's scheduler state, built from the default selection's per-task
 * walk.
 *
 * select_percounter_state runs after that walk, reuses its validation and
 * metadata, and builds the percounter region. GRAPH tasks pass the walk
 * untouched and are validated here through their Definitions.
 *
 * A graph percounter cannot run fails the bind. This runtime's AICore object
 * carries no legacy or resident executor, so there is no scheduler to fall back
 * to: a lane would run the percounter loop against a region that was never
 * published.
 *
 * Header-only because the UT targets that compile runtime_maker.cpp list their
 * sources by name.
 */

#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <set>
#include <vector>

#include "common/host_api.h"
#include "common/host_phase_kind.h"
#include "common/unified_log.h"
#include "host_build_graph/runtime.h"
#include "host_build_graph/shared_memory.h"
#include "intrinsic.h"
#include "host/platform_compile_info.h"
#include "callable_entry.h"
#include "image/image_pack.h"
#include "scheduler/argument_metadata.h"
#include "calendar_cache.h"
#include "calendar_image.h"
#include "calendar_trial_dspark.h"
#include "graph_template.h"
#include "region_plan.h"
#include "scheduler/scheduler_layout.h"
#include "profiling.h"
#include "scheduler/scheduler_types.h"

namespace simpler::percounter {

inline void log_unsupported(int32_t total_tasks, const char *reason, int64_t task_id) {
    LOG_ERROR(
        "A5 percounter: graph of %d tasks: task id=%" PRId64 " is %s, which percounter does not run", total_tasks,
        task_id, reason
    );
}

/** The bind's Graph templates and invocations, gathered from the task table. */
struct GraphInputs {
    std::vector<HostGraphTemplate> templates;
    std::vector<CalendarKey> definitions;
    bool synchronous{true};
    std::vector<GraphInvocationInput> invocations;
    GraphLayoutInput layout{};
    uint64_t body_blocks{0};
    const std::byte *section{nullptr};
    uint32_t section_bytes{0};
};

/**
 * Build one template per Definition the GRAPH tasks name, reading the bind's
 * Definition staging block, and one invocation per GRAPH task.
 *
 * Returns the status of the first template that failed, which is MALFORMED for
 * a Definition upstream would refuse and UNSUPPORTED for a body percounter does
 * not run. `failed_task_id` is then the GRAPH task that named it.
 */
inline TemplateResult gather_graph_inputs(
    Runtime *runtime, const HostApi *api, SharedMemoryHandle &host_sm_handle, int32_t total_tasks, uint64_t worker_count,
    std::vector<TaskInput> *inputs, GraphInputs *out, int64_t *failed_task_id
) {
    // Read on the first GRAPH task: a graph without one has no staging block.
    GraphImageView view{};
    std::map<uint32_t, uint32_t> template_of_definition;
    for (int64_t task_id = 0; task_id < total_tasks; ++task_id) {
        ChipTaskSlotState &slot = host_sm_handle.header->tasks.get_slot_state_by_task_id(task_id);
        if (slot.task_kind != TaskKind::GRAPH) continue;
        if (!view.valid()) {
            void *staging = nullptr;
            size_t staging_bytes = 0;
            api->get_graph_definition_staging(&staging, &staging_bytes);
            if (staging == nullptr || staging_bytes > UINT32_MAX) {
                *failed_task_id = task_id;
                return TemplateResult{TemplateStatus::MALFORMED, "the Definition staging block is unavailable", -1};
            }
            view = GraphImageView{static_cast<const std::byte *>(staging), static_cast<uint32_t>(staging_bytes)};
        }
        const uint32_t definition_offset = slot.to_descriptor().graph_definition_offset;
        auto found = template_of_definition.find(definition_offset);
        if (found == template_of_definition.end()) {
            HostGraphTemplate tmpl{};
            const TemplateResult built = read_graph_template(view, definition_offset, worker_count, &tmpl);
            if (built.status != TemplateStatus::OK) {
                *failed_task_id = task_id;
                return built;
            }
            GraphImageValue definition{};
            if (!graph_definition_decode_framed(view, definition_offset, &definition)) {
                *failed_task_id = task_id;
                return TemplateResult{TemplateStatus::MALFORMED, "the Definition framing is invalid", -1};
            }
            for (int32_t i = 0; out->synchronous && i < definition.definition.task_count; ++i) {
                SubTaskDefinition task{};
                if (!graph_definition_load_element(
                        view, definition, definition.definition.off_sub_tasks,
                        definition.definition.task_count, i, &task
                    ))
                    return TemplateResult{TemplateStatus::MALFORMED, "the body task does not decode", i};
                for (int32_t kid : task.kernel_id) {
                    if (kid < 0) continue;
                    try {
                        if (runtime->get_function_async_capability(kid).mode != CoreAsyncMode::SYNCHRONOUS)
                            out->synchronous = false;
                    } catch (const std::exception &) {
                        out->synchronous = false;
                    }
                }
            }
            // The recorded definition contains relative tensor and scalar
            // records. Its bind-local graph_key is not a replay identity.
            const auto bytes = definition.definition.total_bytes;
            CalendarKey structure((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
            std::memcpy(structure.data(), view.section + definition_offset, bytes);
            structure.front() = 0; // graph_key is the first uint64_t in Graph.
            out->definitions.push_back(std::move(structure));
            out->section = view.section;
            out->section_bytes = std::max(out->section_bytes, definition_offset + definition.definition.total_bytes);
            out->layout.templates_bytes += graph_template_bytes(tmpl);
            out->templates.push_back(std::move(tmpl));
            found = template_of_definition.emplace(definition_offset, out->templates.size() - 1).first;
        }
        const HostGraphTemplate &tmpl = out->templates[found->second];
        TaskInput &input = (*inputs)[static_cast<size_t>(task_id)];
        input.graph_invocation = static_cast<int32_t>(out->invocations.size());
        out->invocations.push_back(GraphInvocationInput{task_id, found->second});
        out->layout.body_task_count += tmpl.tasks.size();
        out->body_blocks += tmpl.executable_blocks;
    }
    out->layout.invocation_count = out->invocations.size();
    return TemplateResult{};
}

enum class StateSelection : uint8_t {
    PERCOUNTER,  // the percounter region is built and published; the bind is done
    FAILED,      // this graph does not run on percounter, or building its region failed
};

/**
 * Build this run's percounter region. Every task must have a shape percounter
 * runs: one active subtask (AIC, or AIV0) or a MIX task of two or three, any
 * block count, with or without sync start.
 *
 * `task_metadata` and `inline_completed_task_ids` are the default selection's
 * per-task walk, which has already rejected any malformed task, checked every
 * kernel id against the callable table, and refused a dispatch predicate on any
 * task other than a single-subtask, single-block one.
 */
inline StateSelection select_percounter_state(
    Runtime *runtime, const HostApi *api, SharedMemoryHandle &host_sm_handle, int32_t total_tasks,
    const std::vector<SchedulerTaskMetadata> &task_metadata, const std::vector<int64_t> &inline_completed_task_ids,
    const sm_layout::SegmentOffsets &device_segments, const void *uploaded_sm
) {
    const size_t task_count = static_cast<size_t>(total_tasks);
    std::vector<bool> inline_completed(task_count, false);
    for (int64_t id : inline_completed_task_ids)
        inline_completed[static_cast<size_t>(id)] = true;

    std::vector<TaskInput> inputs(task_count);
    GraphInputs graph{};
    int64_t graph_failed_task = -1;
    const TemplateResult graph_status = gather_graph_inputs(
        runtime, api, host_sm_handle, total_tasks, runtime->get_worker_count(), &inputs, &graph, &graph_failed_task
    );
    if (graph_status.status == TemplateStatus::MALFORMED) {
        LOG_ERROR(
            "A5 percounter: Graph task id=%" PRId64 ": %s (body task %d)", graph_failed_task, graph_status.reason,
            graph_status.body_index
        );
        return StateSelection::FAILED;
    }
    if (graph_status.status == TemplateStatus::UNSUPPORTED) {
        // Named with its body index, as the MALFORMED case above is: the
        // reason belongs to one task of the body, and the GRAPH task id alone
        // does not say which.
        LOG_ERROR(
            "A5 percounter: graph of %d tasks: Graph task id=%" PRId64 " has %s (body task %d), which percounter does "
            "not run",
            total_tasks, graph_failed_task, graph_status.reason, graph_status.body_index
        );
        return StateSelection::FAILED;
    }
    std::vector<int32_t> edges;
    std::vector<size_t> edge_begin(task_count, 0);
    uint64_t order_count[PERCOUNTER_CORE_TYPE_COUNT] = {0, 0};
    uint64_t mix_task_count = 0;
    uint64_t boundary_task_count = 0;
    uint64_t sync_cohort_count = 0;
    uint64_t completion_notify_count = 0;
    // Each GRAPH task places its body's blocks, and holds one of its own that
    // no list carries.
    uint64_t block_total = graph.body_blocks;
    uint64_t execution_count = 0;
    for (size_t t = 0; t < task_count; ++t) {
        const int64_t task_id = static_cast<int64_t>(t);
        ChipTaskSlotState &slot = host_sm_handle.header->tasks.get_slot_state_by_task_id(task_id);
        TaskInput &input = inputs[t];
        input.inline_completed = inline_completed[t];
        if (input.graph_invocation >= 0) {
            const HostGraphTemplate &tmpl =
                graph.templates[graph.invocations[static_cast<size_t>(input.graph_invocation)].template_index];
            order_count[0] += tmpl.orders[0].size();
            order_count[1] += tmpl.orders[1].size();
            mix_task_count += tmpl.mix_task_count;
            boundary_task_count += tmpl.boundary_task_count;
            sync_cohort_count += tmpl.sync_cohort_count;
            ++block_total;
            for (const auto &body_task : tmpl.tasks)
                execution_count += body_task.subtask_total;
        } else if (!input.inline_completed) {
            input.sync_start = slot.task_attrs.requires_sync_start();
            input.has_predicate = slot.task_attrs.has_predicate();
            const uint8_t active_mask = task_metadata[t].active_mask.raw();
            const TaskPlacement placement = task_placement_from_active_mask(active_mask, &input.core_type_index);
            if (placement == TaskPlacement::UNSUPPORTED) {
                log_unsupported(
                    total_tasks, active_mask == 4U ? "an AIV1-only task" : "a task with no subtask", task_id
                );
                return StateSelection::FAILED;
            }
            input.active_mask = 0;
            const auto &descriptor = slot.to_descriptor();
            for (uint32_t lane = 0; lane < PLATFORM_CORES_PER_BLOCKDIM; ++lane)
                if (descriptor.kernel_id[lane] >= 0) input.active_mask |= 1U << lane;
            input.block_num = slot.logical_block_num;
            input.is_mix = placement == TaskPlacement::MIX;
            input.subtask_count = __builtin_popcount(active_mask);
            execution_count += static_cast<uint64_t>(input.block_num) * input.subtask_count;
            // SPMD tasks allocate blocks through a task-private counter.
            const uint64_t entries = 1;
            if (input.block_num > 1 || input.is_mix) ++boundary_task_count;
            if (input.sync_start) ++sync_cohort_count;
            if (percounter_completion_notify_eligible(input)) ++completion_notify_count;
            if (input.is_mix) mix_task_count += entries;
            order_count[input.is_mix ? 1 : input.core_type_index] += entries;
            block_total += static_cast<uint64_t>(input.block_num);
        } else {
            ++block_total;
        }
        const TaskPayload &payload = slot.to_payload();
        input.fanin_count = payload.fanin_count;
        edge_begin[t] = edges.size();
        for (int32_t e = 0; e < payload.fanin_count; ++e)
            edges.push_back(payload.fanin_data()[e]);
    }
    for (size_t t = 0; t < task_count; ++t)
        inputs[t].fanin_ids = inputs[t].fanin_count > 0 ? edges.data() + edge_begin[t] : nullptr;

    std::vector<std::vector<int32_t>> folded_rows;
    std::vector<int64_t> folded;
    if (!percounter_fold_dependency_tasks(inputs, folded_rows, folded)) {
        LOG_ERROR("A5 percounter: invalid dependency-only task fanin");
        return StateSelection::FAILED;
    }
    uint64_t edge_count = edges.size();
    if (!folded.empty()) {
        edge_count = 0;
        for (const auto &input : inputs)
            edge_count += static_cast<uint64_t>(input.fanin_count);
        for (const int64_t id : folded)
            --order_count[inputs[id].core_type_index];
        execution_count -= folded.size();
        LOG_TIMING("A5 percounter: folded %zu dependency-only tasks", folded.size());
    }

    const auto may_defer = [&](uint32_t id) {
        const auto &input = inputs[id];
        for (uint32_t lane = 0; lane < PLATFORM_CORES_PER_BLOCKDIM; ++lane) {
            if ((input.active_mask & (1U << lane)) == 0) continue;
            try {
                const auto capability = runtime->get_function_async_capability(task_metadata[id].kernel_ids[lane]);
                if (capability.mode != CoreAsyncMode::SYNCHRONOUS) return true;
            } catch (const std::exception &) {
                return true;
            }
        }
        return false;
    };

    const int32_t worker_count = runtime->get_worker_count();
    const auto cluster_count = static_cast<uint32_t>(worker_count / PLATFORM_CORES_PER_BLOCKDIM);
    const GraphPlanInput graph_plan{
        graph.templates.data(), graph.templates.size(), graph.invocations.data(), graph.invocations.size()
    };
    std::vector<CalendarPhase> phases;
    std::vector<RangeCalendarInput> range_inputs;
    std::vector<std::shared_ptr<CalendarTraining>> training;
    std::vector<const char *> calendar_events;
    RangeCalendarImage calendar;
    std::vector<DSparkSignalOrder> signals;
    const bool dspark = dspark_trial_signal_order(inputs, task_metadata, graph_plan, worker_count, signals);
    const bool output_roles = dspark;
    const bool mixed = !graph.invocations.empty();
    if (output_roles && !dspark_set_output_masks(inputs, task_metadata, mixed)) {
        LOG_ERROR("A5 calendar: DSpark EP2 output roles do not match the frozen task graph");
        return StateSelection::FAILED;
    }
    if (output_roles && !dspark_set_reduce_shards(inputs, task_metadata, mixed)) {
        LOG_ERROR("A5 calendar: DSpark EP2 reduce shard shapes do not match the frozen task graph");
        return StateSelection::FAILED;
    }
    bool calendar_eligible =
        (graph.synchronous || dspark) && worker_count > 0 && worker_count % PLATFORM_CORES_PER_BLOCKDIM == 0;
    for (uint32_t id = 0; calendar_eligible && id < inputs.size(); ++id) {
        const auto &input = inputs[id];
        if (input.has_predicate || input.published_off_lane ||
            (!dspark && input.graph_invocation < 0 && !input.inline_completed && may_defer(id)))
            calendar_eligible = false;
    }
    if (calendar_eligible && plan_calendar_phases(inputs, graph_plan, &phases) && !phases.empty() &&
        (!dspark || dspark_apply_signal_order(signals, inputs.size(), phases))) {
        if (dspark) LOG_INFO("A5 calendar: DSpark EP2 matched %zu signal groups", signals.size());
        auto cache = std::static_pointer_cast<CalendarCache>(
            api->acquire_callable_calendar_cache(runtime->get_active_callable_id(), make_calendar_cache)
        );
        range_inputs.resize(phases.size());
        training.resize(phases.size());
        calendar_events.resize(phases.size());
        // The last equivalent Graph invocation supplies the timing sample.
        // Earlier invocations in the bind share its immutable seed plan.
        for (size_t p = phases.size(); p != 0; --p) {
            const auto n = p - 1;
            const auto &phase = phases[n];
            auto &entry = range_inputs[n];
            entry.phase = &phase;
            const bool cacheable = cache != nullptr && (dspark || phase.invocation >= 0 || graph.invocations.empty());
            if (!cacheable) {
                auto initial = std::make_shared<CalendarPlan>();
                if (!build_static_calendar(phase.tasks, cluster_count, {}, initial.get())) break;
                entry.plan = std::move(initial);
                continue;
            }
            CalendarKey key;
            if (phase.invocation < 0) {
                key = make_calendar_key(*runtime, inputs, task_metadata);
                key.push_back(phase.task_ids.size());
                key.insert(key.end(), phase.task_ids.begin(), phase.task_ids.end());
                for (size_t id = 0; id < phase.tasks.size(); ++id) {
                    key.push_back(phase.tasks[id].calendar_defer_mask);
                    key.push_back(phase.tasks[id].calendar_output_mask);
                    key.push_back(phase.tasks[id].calendar_shard_round);
                    key.push_back(phase.fanins[id].size());
                    key.insert(key.end(), phase.fanins[id].begin(), phase.fanins[id].end());
                }
            } else {
                const auto &invocation = graph.invocations[phase.invocation];
                const auto &definition = graph.definitions[invocation.template_index];
                const auto &payload =
                    host_sm_handle.header->tasks.get_slot_state_by_task_id(invocation.shell_task_id).to_payload();
                key = {UINT64_MAX, static_cast<uint64_t>(worker_count), definition.size(),
                       static_cast<uint64_t>(payload.scalar_count), static_cast<uint64_t>(payload.tensor_count)};
                key.insert(key.end(), definition.begin(), definition.end());
                append_calendar_argument_key(
                    key, payload.scalar_count, payload.tensor_count,
                    [&](int32_t i) { return payload.scalar_data()[i]; },
                    [&](int32_t i) -> const auto & { return payload.tensor_data()[i]; }
                );
            }
            std::unique_lock<std::mutex> lock(cache->mutex);
            if (const auto found = cache->plans.find(key); found != cache->plans.end()) {
                entry.plan = found->second;
                calendar_events[n] = "HIT";
                continue;
            }
            if (const auto pending = cache->training.find(key); pending != cache->training.end()) {
                if ((entry.plan = pending->second.lock())) {
                    calendar_events[n] = "REUSE";
                    continue;
                }
            }
            auto initial = std::make_shared<CalendarPlan>();
            if (!build_static_calendar(phase.tasks, cluster_count, {}, initial.get())) break;
            const auto sample = std::make_shared<CalendarTraining>();
            sample->cache = cache;
            sample->baseline = initial;
            sample->cluster_count = cluster_count;
            sample->key = std::move(key);
            sample->copy_tasks(phase.tasks);
            cache->training.insert_or_assign(sample->key, initial);
            sample->owns_training = true;
            entry.plan = std::move(initial);
            entry.sample = true;
            training[n] = sample;
            calendar_events[n] = "MISS";
        }
        if (std::all_of(range_inputs.begin(), range_inputs.end(), [](const auto &input) {
                return input.plan != nullptr && !input.plan->logical_row_by_packed.empty();
            }))
            build_range_calendar(range_inputs, graph_plan, static_cast<uint32_t>(task_count), cluster_count, &calendar);
        if (calendar.bytes.empty()) training.clear();
    }

    if (output_roles && calendar.bytes.empty()) {
        LOG_ERROR("A5 calendar: output-role credits require the calendar executor");
        return StateSelection::FAILED;
    }

    PercounterLayout layout{};
    if (!plan_layout(
            task_count, block_total, edge_count, order_count, &layout, mix_task_count, boundary_task_count,
            sync_cohort_count, graph.layout, execution_count, runtime->dev.chip_swimlane_level
        )) {
        LOG_ERROR("A5 percounter: state layout overflow");
        return StateSelection::FAILED;
    }
    uint64_t allocation_size = layout.total_size;
    uint64_t calendar_offset = 0;
    uint32_t phase_slots = 0;
    if (!calendar.bytes.empty()) {
        constexpr uint64_t alignment = 128;
        if (layout.total_size > SIZE_MAX - alignment - calendar.bytes.size()) return StateSelection::FAILED;
        calendar_offset = (layout.total_size + alignment - 1) & ~(alignment - 1);
        allocation_size = calendar_offset + calendar.bytes.size();
        phase_slots = calendar.load<CalendarRangeHeader>(0).phase_slots_count;
    }
    void *device_state = nullptr;
    void *host_base = nullptr;
    if (api->acquire_scheduler_state_storage(
            static_cast<size_t>(allocation_size), alignof(PercounterControl), &device_state, &host_base
        ) != 0 ||
        device_state == nullptr || host_base == nullptr) {
        LOG_ERROR("A5 percounter: failed to obtain %" PRIu64 " scheduler state bytes", layout.total_size);
        return StateSelection::FAILED;
    }
    const uint64_t device_base = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device_state));

    SchedulerCompletionCounterPlan completion_plan;
    if (completion_notify_count != 0) {
        std::string error;
        if (!plan_percounter_completion_counters(inputs, &completion_plan, &error)) {
            LOG_ERROR("A5 percounter: completion counter plan failed: %s", error.c_str());
            return StateSelection::FAILED;
        }
        if (output_roles && !dspark_remove_whole_reduce_credits(completion_plan, inputs)) {
            LOG_ERROR("A5 percounter: DSpark EP2 reduce tasks must use CntNotify");
            return StateSelection::FAILED;
        }
    }

    // Keep sync cohorts first in the lease so allocation pressure does not
    // displace their existing mappings. The AICPU clears both groups before READY.
    std::vector<uint64_t> notify_addresses;
    const uint64_t phase_slot_begin = sync_cohort_count + completion_plan.chains.size();
    const uint64_t notify_count = phase_slot_begin + phase_slots;
    if (notify_count != 0) {
        SchedulerDoneNotifications notifications{};
        if (api->acquire_scheduler_notifications(
                SchedulerNotificationPurpose::COMPLETION_COUNTER, static_cast<size_t>(notify_count), &notifications
            ) == 0) {
            notify_addresses = std::move(notifications.addresses);
            if (notifications.owner != nullptr) runtime->retain_pending_resource(std::move(notifications.owner));
        }
        const auto mapped = [&](size_t index) {
            return index < notify_addresses.size() && a5_cnt_notify_increment_address(notify_addresses[index]) != 0;
        };
        uint64_t mapped_sync = 0;
        for (size_t index = 0; index < sync_cohort_count; ++index)
            mapped_sync += mapped(index);
        size_t mapped_chains = 0;
        for (size_t chain = 0; chain < completion_plan.chains.size(); ++chain)
            mapped_chains += mapped(sync_cohort_count + chain);
        const auto mapped_tasks =
            std::count_if(completion_plan.task_chains.begin(), completion_plan.task_chains.end(), [&](uint32_t chain) {
                return chain != UINT32_MAX && mapped(sync_cohort_count + chain);
            });
        LOG_TIMING(
            "A5 percounter: completion counters planned_chains=%zu mapped_chains=%zu eligible_tasks=%" PRIu64
            " mapped_tasks=%zu mapped_sync=%" PRIu64 " sync_cohorts=%" PRIu64,
            completion_plan.chains.size(), mapped_chains, completion_notify_count, static_cast<size_t>(mapped_tasks),
            mapped_sync, sync_cohort_count
        );
    }

    ExternalAddresses external{};
    external.graph_storage = reinterpret_cast<uint64_t>(runtime->get_gm_sm_ptr()) + device_segments.storage;
    external.graph_storage_count = task_count;
    external.callable_table = runtime->callable_entry_table_addr();
    external.callable_count = static_cast<uint64_t>(runtime->callable_table_len());
    external.sync_notify = notify_addresses.data();
    external.sync_notify_count = std::min<uint64_t>(notify_addresses.size(), sync_cohort_count);
    if (!completion_plan.chains.empty() && notify_addresses.size() > sync_cohort_count) {
        external.completion_plan = &completion_plan;
        external.completion_notify = notify_addresses.data() + sync_cohort_count;
        external.completion_notify_count =
            std::min<uint64_t>(notify_addresses.size() - sync_cohort_count, completion_plan.chains.size());
    }
    BuildResult build{};
    if (!build_tables(
            inputs.data(), task_count, layout, host_base, device_base,
            static_cast<uint32_t>(runtime->get_worker_count() / PLATFORM_CORES_PER_BLOCKDIM), external, &build,
            graph_plan, calendar.bytes.empty(), may_defer
        )) {
        LOG_ERROR(
            "A5 percounter: %s (task id=%" PRId64 ", fanin index=%d)", build_status_name(build.status), build.task_id,
            build.fanin_index
        );
        return StateSelection::FAILED;
    }

    const auto *terminal_control = region_at<PercounterControl>(host_base, layout.control_offset);
    if (terminal_control->terminal.step_count != 0)
        LOG_TIMING(
            "A5 percounter: terminal pipeline %u steps, first=%u, last=%u, prerequisites=%u",
            terminal_control->terminal.step_count, terminal_control->terminal.steps[0],
            terminal_control->terminal.steps[terminal_control->terminal.step_count - 1],
            terminal_control->terminal.prerequisite_count
        );
    if (task_count != 0 && uploaded_sm == nullptr) return StateSelection::FAILED;
    const auto address_space = std::strcmp(get_platform(), "a5sim") == 0 ? SchedulerCallableAddressSpace::HOST :
                                                                           SchedulerCallableAddressSpace::DEVICE;
    const uint64_t host_image = reinterpret_cast<uint64_t>(uploaded_sm);
    const uint64_t device_image = reinterpret_cast<uint64_t>(runtime->get_gm_sm_ptr());
    const auto image_address = [&](const void *address, uint64_t bytes, uint64_t &device_address) {
        device_address = 0;
        if (bytes == 0) return true;
        const uint64_t source = reinterpret_cast<uint64_t>(address);
        if (source < host_image || source - host_image > runtime->dev.sm_image_bytes ||
            bytes > runtime->dev.sm_image_bytes - (source - host_image) ||
            source - host_image > UINT64_MAX - device_image)
            return false;
        device_address = device_image + (source - host_image);
        return true;
    };
    auto *counters = region_at<PercounterCounter>(host_base, layout.counters_offset);
    region_at<PercounterControl>(host_base, layout.control_offset)->arguments_address =
        device_image + device_segments.scalar_pool;
    for (size_t id = 0; id < task_count; ++id) {
        const auto &input = inputs[id];
        if (input.graph_invocation >= 0 || input.inline_completed || input.published_off_lane || input.active_mask == 0)
            continue;
        const auto &payload = task_image::payload(uploaded_sm, device_segments, id);
        if (payload.tensor_count < 0 || payload.scalar_count < 0) return StateSelection::FAILED;
        const uint32_t tensors = static_cast<uint32_t>(payload.tensor_count);
        const uint32_t scalars = static_cast<uint32_t>(payload.scalar_count);
        uint64_t arguments_address = 0;
        const void *arguments =
            reinterpret_cast<const uint8_t *>(uploaded_sm) + device_segments.scalar_pool + id * sizeof(DispatchPayload);
        if (!image_address(arguments, sizeof(DispatchPayload), arguments_address)) return StateSelection::FAILED;
        uint64_t functions[PLATFORM_CORES_PER_BLOCKDIM]{};
        for (uint32_t lane = 0; lane < PLATFORM_CORES_PER_BLOCKDIM; ++lane)
            if (input.active_mask & (1U << lane))
                functions[lane] = scheduler_resolve_callable_entry(
                    runtime->get_function_bin_addr(task_metadata[id].kernel_ids[lane]), address_space
                );
        const auto prepared = percounter_prepare_argument_metadata(
            counters[id], functions, input.active_mask, arguments_address, tensors, scalars
        );
        if (prepared != SchedulerGraphResult::OK) {
            LOG_ERROR(
                "A5 percounter: invalid argument metadata task=%zu status=%u", id, static_cast<unsigned>(prepared)
            );
            return StateSelection::FAILED;
        }
    }

    if (graph.section_bytes != 0) {
        uint32_t source = 0;
        uint64_t base = 0;
        if (api->publish_graph_section(graph.section, graph.section_bytes, &source, &base) != 0) {
            LOG_ERROR("A5 percounter: failed to publish %u Graph Definition bytes", graph.section_bytes);
            return StateSelection::FAILED;
        }
        runtime->publish_graph_section(static_cast<GraphSectionSource>(source), base, graph.section_bytes);
    }
    if (!calendar.bytes.empty()) {
        auto *control = region_at<PercounterControl>(host_base, layout.control_offset);
        control->calendar_offset = calendar_offset - layout.control_offset;
        control->image_bytes = allocation_size;
        if (!bind_range_calendar(calendar, device_base + calendar_offset, device_base, layout, host_base,
                                 notify_addresses, phase_slot_begin))
            return StateSelection::FAILED;
        if (output_roles) {
            uint32_t masked = 0;
            uint32_t omitted = 0;
            uint32_t expected = 0;
            for (size_t id = 0; id < inputs.size(); ++id) {
                const auto mask = inputs[id].calendar_output_mask;
                if (mask == 0) continue;
                if (counters[id].completion_notify_address == 0) {
                    LOG_ERROR("A5 calendar: DSpark EP2 output roles require a bound CntNotify slot");
                    return StateSelection::FAILED;
                }
                ++masked;
                expected += inputs[id].block_num * (inputs[id].subtask_count - __builtin_popcount(mask));
            }
            for (const auto &patch : calendar.dispatches) {
                if (patch.output_mask == 0 || (patch.output_mask & (1U << patch.role)) != 0) continue;
                if (calendar.load<CalendarDispatch>(patch.offset).done_add_cmd != 0) return StateSelection::FAILED;
                omitted += inputs[patch.global_task].block_num;
            }
            uint64_t waits = 0;
            for (const auto &range : range_inputs)
                for (const auto &fanins : range.plan->waits.task_fanins) waits += fanins.size();
            LOG_INFO("A5 calendar: output roles tasks=%u omitted_adds=%u fanins=%" PRIu64, masked, omitted, waits);
            if (masked != 80 || omitted != expected || omitted != 3840) return StateSelection::FAILED;
            std::set<std::pair<uint32_t, uint32_t>> reducer_shards;
            for (const auto &patch : calendar.dispatches)
                if (patch.shard_add_index < DS_PARK_REDUCE_SHARDS)
                    reducer_shards.emplace(patch.global_task, patch.shard_add_index);
            const auto reducers = reducer_shards.size();
            const auto readers = std::count_if(calendar.waits.begin(), calendar.waits.end(),
                [](const auto &patch) { return patch.kind == RangeCalendarImage::WaitKind::REDUCE_SHARD; });
            const auto range_end_slots = std::count_if(calendar.phase_ends.begin(), calendar.phase_ends.end(),
                [](const auto &end) { return end.slot_offset != 0; });
            if (reducers != 160 || readers != 160 || phase_slots != range_end_slots + DS_PARK_REDUCE_SHARDS)
                return StateSelection::FAILED;
            LOG_INFO("A5 calendar: combine_reduce sharded publishes=1600 readers=2560 slots=4");
        }
        auto *host_calendar = region_at<std::byte>(host_base, calendar_offset);
        std::memcpy(host_calendar, calendar.bytes.data(), calendar.bytes.size());
        runtime->add_pending_metadata(
            static_cast<std::byte *>(device_state) + calendar_offset, host_calendar, calendar.bytes.size(),
            HostPhaseKind::Count, {}
        );
        for (size_t p = 0; p < range_inputs.size(); ++p) {
            if (calendar_events[p] == nullptr) continue;
            const auto &plan = *range_inputs[p].plan;
            LOG_INFO("A5 calendar: %s plan=%016" PRIx64 " rows=%zu pid=%d",
                     calendar_events[p], calendar_plan_id(plan), plan.logical_row_by_packed.size(), getpid());
        }
        const auto header = calendar.load<CalendarRangeHeader>(0);
        LOG_INFO("A5 calendar: ranges=%u phases=%zu graph_invocations=%zu phase_slots=%u",
                 header.range_count, phases.size(), graph.invocations.size(), phase_slots);
        if (dspark || header.sample_index_offset != 0) {
            const auto address = device_base + layout.control_offset + offsetof(PercounterControl, async_registered);
            runtime->on_pending_execution_complete(
                [training = std::move(training), offsets = std::move(calendar.sample_offsets),
                 timings_base = device_base + calendar_offset, dspark, address](const HostApi &run_api) {
                    if (dspark) {
                        uint64_t counts[2]{};
                        if (run_api.copy_from_device(counts, reinterpret_cast<const void *>(address), sizeof(counts)) == 0)
                            LOG_INFO("A5 calendar: EP2 defer registered=%" PRIu64 " settled=%" PRIu64, counts[0], counts[1]);
                    }
                    for (size_t p = 0; p < training.size(); ++p) {
                        if (training[p] == nullptr) continue;
                        try {
                            training[p]->finish(run_api, reinterpret_cast<const void *>(timings_base + offsets[p]));
                        } catch (const std::exception &error) {
                            LOG_WARN("A5 calendar: could not retain schedule: %s", error.what());
                        }
                    }
                }
            );
        }
    }
    retain_profile_state(runtime, api, device_state, layout, folded, inline_completed_task_ids);
    runtime->publish_scheduler_bootstrap(SCHEDULER_RUNTIME_MODE_PERCOUNTER, device_base + layout.control_offset);
    if (api != nullptr) api->record_scheduler_runtime_mode(SCHEDULER_RUNTIME_MODE_PERCOUNTER);
    runtime->add_pending_metadata(
        device_state, host_base, static_cast<size_t>(layout.upload_size), HostPhaseKind::Count, {}
    );
    LOG_INFO(
        "A5 percounter: selected for %d tasks (%" PRIu64 " AIC, %" PRIu64 " AIV and %" PRIu64
        " MIX list entries, %" PRIu64 " sync cohorts, %zu edges, %zu Graph invocations of %zu Definitions with %" PRIu64
        " body tasks; %" PRIu64 " of %" PRIu64 " region bytes uploaded)",
        total_tasks, terminal_control->order_count[0], terminal_control->order_count[1],
        terminal_control->mix_task_count, sync_cohort_count, edges.size(), graph.invocations.size(),
        graph.templates.size(), graph.layout.body_task_count, layout.upload_size, layout.total_size
    );
    return StateSelection::PERCOUNTER;
}

}  // namespace simpler::percounter
