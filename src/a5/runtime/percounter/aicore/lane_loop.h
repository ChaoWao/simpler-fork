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
 * The percounter AICore executor: one transferable owner schedules each cluster.
 *
 * An idle C/V worker may own cluster scheduling. Commands and returns cross
 * independent SSBUF mailboxes; global claims and dependency words remain GM.
 * AICPU materializes Graph metadata and supervises top-level sinks.
 *
 * Startup and retirement are the runtime's: the lane reports through the
 * handshake, waits for the AICPU's READY and the DATA_MAIN_BASE release, and
 * after EXIT answers the COND nonce exactly as the resident executor does.
 *
 * Header-only because aicore_executor.cpp is compiled by name into unit tests.
 */

#pragma once

#include "aicore/aicore.h"
#include "common/device_phase.h"
#include "common/platform_config.h"
#include "async_completion_types.h"
#include "scheduler/async_completion.h"
#include "dispatch_payload.h"
#include "runtime.h"
#include "scheduler/wire_types.h"
#include "scheduler/scheduler_graph.h"
#include "scheduler/scheduler_layout.h"
#include "scheduler/image_read.h"
#include "scheduler/gm.h"
#include "scheduler/scheduler_topology.h"
#include "scheduler/scheduler_watchdog.h"
#include "aicore/aicore_profiling_state.h"
#include "profiling.h"
#include "aicore/chip_swimlane_collector_aicore.h"

namespace simpler::percounter {
namespace {

typedef void (*PercounterKernelFunc)(__gm__ int64_t *);

/**
 * Read a counter the way the runtime reads any cross-core word.
 *
 * Not a volatile dereference: on silicon a volatile load never observed a
 * remote atomic across two million polls with the producer confirmed to have
 * published. `ld_dev` reaches a coherent point (~98 cycles at realistic
 * fanout). Not `AtomicAdd(ptr, 0)` either: that is a read-modify-write and
 * would queue every poll at the atomic unit.
 */
inline __aicore__ uint64_t percounter_observe(uint64_t counter_address) {
    __gm__ uint64_t *word = reinterpret_cast<__gm__ uint64_t *>(counter_address);
    return percounter_gm_read(*word);
}

/** How often a spinning lane looks at the exit register and the error word. */
inline constexpr uint32_t PERCOUNTER_SPIN_CHECK_INTERVAL = 64;

struct LaneConfig {
    __gm__ ChipSwimlaneActiveHead *profile_head{nullptr};
    ChipSwimlaneAicoreLocalState *profile{nullptr};
    PercounterScalarTrace *scalar{nullptr};
    __gm__ PercounterProfileBuffer *profile_buffer{nullptr};
    __gm__ PercounterTaskTiming *task_records{nullptr};
    uint32_t *profile_task_count{nullptr};
    uint32_t *task_dropped{nullptr};
    mutable PercounterTaskTiming pending_task{};
    mutable bool pending_task_valid{false};
    mutable AicoreWorkWindow work_window{};
    mutable __gm__ ChipSwimlaneAicoreTaskRecord *pending_stream_record{nullptr};
    __gm__ PercounterControl *control;
    uint64_t storage_address;
    __gm__ DispatchPayload *payloads;
    __gm__ PercounterTicket *tickets;
    __gm__ PercounterFanin *fanin;
    __gm__ uint64_t *fanin_addresses;
    __gm__ PercounterCounter *counters;
    __gm__ PercounterCompletionSector *completion_sectors;
    __gm__ PercounterTaskMetadata *metadata;
    __gm__ uint64_t *metadata_ready;
    __gm__ PercounterTaskClaim *task_claims;
    uint64_t task_count;
    uint64_t table_task_count;
    bool has_predicates;
    __gm__ pto2::resident::AsyncGroupData *async_group;
    __gm__ DeferredCompletionEntry *async_entries;
    __gm__ PercounterDeferredGroup *async_completions;
    uint64_t async_completion_capacity;

    int32_t cluster_index;
    int32_t cluster_lane;
    __gm__ uint64_t *cluster_ready;
    __gm__ PercounterClusterOwnerSector *cluster_owner;
    PERCOUNTER_SSBUF PercounterClusterSchedulerRegion *ssbuf;
    __gm__ PercounterResourceNode *resource_nodes;
    __gm__ PercounterOrdinaryPiece *ordinary_pieces;
    __gm__ uint32_t *projection_catalog[PERCOUNTER_LIST_COUNT];
    __gm__ uint32_t *projection_task_map;
    __gm__ PercounterSyncBoundary *sync_boundaries;
    mutable SchedulerGraphResult *local_error{nullptr};
};

inline __aicore__ void percounter_publish_stream_records(ChipSwimlaneAicoreLocalState *profile) {
    auto *records = reinterpret_cast<__gm__ uint8_t *>(profile->cached_buf->records);
    const uint64_t bytes = uint64_t{profile->slot_within_buf} * sizeof(ChipSwimlaneAicoreTaskRecord);
    for (uint64_t offset = 0; offset < bytes; offset += 64)
        dcci(records + offset, SINGLE_CACHE_LINE, CACHELINE_OUT);
    dsb((mem_dsb_t)0);
    chip_swimlane_aicore_publish_task_count(profile);
}

inline __aicore__ void percounter_flush_stream_records(ChipSwimlaneAicoreLocalState *profile) {
    if (profile->cached_buf != nullptr && profile->slot_within_buf > 0 &&
        profile->slot_within_buf < PLATFORM_AICORE_BUFFER_SIZE)
        percounter_publish_stream_records(profile);
}

inline __aicore__ void percounter_commit_task_timing(const LaneConfig &cfg) {
    if (!cfg.pending_task_valid) return;
    if (cfg.profile_buffer != nullptr) {
        const uint32_t index = *cfg.profile_task_count;
        if (index < cfg.profile_buffer->task_capacity) {
            auto *record = cfg.task_records + index;
            record->dispatch = cfg.pending_task.dispatch;
            record->start = cfg.pending_task.start;
            record->finish = cfg.pending_task.finish;
            TaskId::assign(record->task_token, cfg.pending_task.task_token);
            ++*cfg.profile_task_count;
        } else if (*cfg.task_dropped != UINT32_MAX) ++*cfg.task_dropped;
    } else {
        auto *record = cfg.pending_stream_record;
        record->start_time = cfg.pending_task.start;
        record->end_time = cfg.pending_task.finish;
        TaskId::assign(record->task_token, cfg.pending_task.task_token);
        record->reg_task_id = cfg.profile->cached_buf_seq * PLATFORM_AICORE_BUFFER_SIZE + cfg.profile->slot_within_buf;
        record->receive_to_start_cycles = static_cast<uint32_t>(cfg.pending_task.start - cfg.pending_task.dispatch);
        if (cfg.profile->slot_within_buf == PLATFORM_AICORE_BUFFER_SIZE) percounter_publish_stream_records(cfg.profile);
    }
    cfg.pending_task_valid = false;
}

/** Where a task's producer counters are: `addresses[0 .. count)`. */
struct PercounterFaninView {
    __gm__ uint64_t *addresses;
    int32_t count;
};

/**
 * Locate `task_id`'s producers.
 *
 * A device load, not a dereference. The table reaches the device by DMA, which
 * does not invalidate this core's scalar data cache, so a plain load can return
 * whatever the line held before the image landed -- on silicon a 64-task chain
 * read a producer address of 0x120000000000 for its 60th task. Invisible in
 * simulation, where every load is coherent.
 */
inline __aicore__ PercounterFaninView percounter_fanin_view(const LaneConfig &cfg, int64_t task_id) {
    const uint64_t record = percounter_gm_read_immutable_word(reinterpret_cast<__gm__ uint32_t *>(&cfg.fanin[task_id]));
    PercounterFaninView view{};
    view.count = static_cast<int32_t>(record & 0xffffffffULL);
    view.addresses = cfg.fanin_addresses + static_cast<int32_t>(record >> 32);
    return view;
}

__attribute__((noinline, cold)) __aicore__ void
percounter_fail(const LaneConfig &cfg, SchedulerGraphResult result, int64_t task_id = -1, uint64_t detail = 0) {
    if (cfg.local_error != nullptr) {
        *cfg.local_error = result;
        return;
    }
    if (percounter_gm_compare_exchange(cfg.control->lane_error_claim, uint64_t{0}, uint64_t{1}) != 0) return;
    percounter_gm_store(cfg.control->lane_error_task, static_cast<uint64_t>(task_id + 1));
    percounter_gm_store(cfg.control->lane_error_detail, detail);
    if (percounter_gm_compare_exchange(cfg.control->lane_error, uint64_t{0}, static_cast<uint64_t>(result)) == 0)
        percounter_gm_store(cfg.control->lane_error_claim, uint64_t{2});
}

/**
 * A task as a lane runs it: where its storage is, what it waits on, and what
 * its completion publishes.
 *
 * A task-table task is its own storage index and has a fanin row of counter
 * addresses. A Graph body task lives in its invocation's storage at its body
 * index; a root waits on its shell's fanin row, any other on its producers'
 * body indices, and a sink also publishes its shell.
 */
struct PercounterTask {
    int64_t id;     // percounter id: the counter it publishes
    int64_t local;  // index into `graph`
    uint64_t storage_address;
    uint32_t storage_count;
    int64_t shell;  // -1 for a task-table task
    bool sink;
    // Bit zero tags partitioned completion. Body references encode index << 1;
    // task-table references encode the aligned primary counter address.
    bool body_fanin;
    int32_t fanin_count;
    __gm__ uint64_t *fanin;
    int64_t body_base;
};

inline __aicore__ SchedulerGraphView percounter_task_graph(const PercounterTask &task) {
    return scheduler_ordinary_graph_view(
        task.storage_address, task.storage_count, task.storage_count > 0 ? task.storage_count - 1 : 0
    );
}

/**
 * Resolve `id` into a task. False, with the lane error set, for an id outside
 * the graph or a body task whose line the AICPU has not written.
 *
 * A body task's line is read after metadata_ready; the AICPU wrote and
 * flushed the line before publishing that token. Invalidation drops what this core
 * held of it from an earlier run. Its counter word changes under other cores'
 * atomics, but nothing here reads it.
 */
inline __aicore__ bool percounter_resolve(const LaneConfig &cfg, int64_t id, PercounterTask *task) {
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Resolve);
    if (id < 0 || static_cast<uint64_t>(id) >= cfg.task_count) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_TASK_ID);
        return false;
    }
    if (static_cast<uint64_t>(id) < cfg.table_task_count) {
        const PercounterFaninView view = percounter_fanin_view(cfg, id);
        *task = PercounterTask{id,
                               id,
                               cfg.storage_address,
                               static_cast<uint32_t>(cfg.table_task_count),
                               -1,
                               false,
                               false,
                               view.count,
                               view.addresses,
                               0};
        return true;
    }
    __gm__ PercounterCounter *line = &cfg.counters[id];
    percounter_observe_cache_line(line);
    const uint64_t storage = line->storage_address;
    const uint32_t body_index = line->body_index;
    const uint32_t flags = line->flags;
    const uint32_t shell = line->shell_task_id;
    if (storage == 0 || static_cast<uint64_t>(shell) >= cfg.table_task_count || static_cast<int64_t>(body_index) > id) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_TASK_ID);
        return false;
    }
    task->id = id;
    task->local = body_index;
    task->storage_address = storage;
    task->storage_count = body_index + 1;
    task->shell = shell;
    task->sink = (flags & PERCOUNTER_BODY_SINK) != 0;
    task->body_base = id - static_cast<int64_t>(body_index);
    if ((flags & PERCOUNTER_BODY_ROOT) != 0) {
        const PercounterFaninView view = percounter_fanin_view(cfg, shell);
        task->body_fanin = false;
        task->fanin_count = view.count;
        task->fanin = view.addresses;
    } else {
        task->body_fanin = true;
        task->fanin_count = static_cast<int32_t>(line->fanin_count);
        task->fanin = scheduler_state_at<uint64_t>(cfg.control, line->fanin_offset);
    }
    return true;
}

/** An aligned primary address with a low-bit partitioned-completion tag. */
inline __aicore__ uint64_t percounter_producer(const LaneConfig &cfg, const PercounterTask &task, int32_t i) {
    const uint64_t value = percounter_gm_read(task.fanin[i]);
    if (!task.body_fanin) return value;
    return reinterpret_cast<uint64_t>(&cfg.counters[static_cast<uint64_t>(task.body_base) + (value >> 1)].value) |
           (value & PERCOUNTER_COMPLETION_PARTITIONED);
}

inline __aicore__ uint64_t
percounter_completion_address(const LaneConfig &cfg, int64_t task_id, uint32_t block, uint32_t blocks) {
    if (blocks == 1) return reinterpret_cast<uint64_t>(&cfg.counters[task_id].value);
    return reinterpret_cast<uint64_t>(
        &cfg.completion_sectors
             [task_id * PERCOUNTER_COMPLETION_SECTOR_COUNT + (block & (PERCOUNTER_COMPLETION_SECTOR_COUNT - 1))]
                 .value
    );
}

// Argument metadata is validated and published before metadata_ready.
inline __aicore__ bool percounter_prepare_subtask(
    const LaneConfig &cfg, int64_t task_id, uint32_t subtask_slot, int32_t block_idx, int32_t block_num,
    __gm__ DispatchPayload *payload
) {
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Prepare);
    percounter_observe_data_cache(reinterpret_cast<__gm__ void *>(payload));
    const auto *metadata = &cfg.counters[task_id];
    const uint64_t function_address = metadata->function_addresses[subtask_slot];
    if (function_address == 0) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_CALLABLE, task_id, subtask_slot);
        return false;
    }
    const uint32_t counts = metadata->argument_counts;
    const uint32_t tensors = counts & 0xffffU;
    const uint32_t scalars = counts >> 16;
    const uint64_t tensor_address = metadata->tensors_address;
    const auto *scalar_data = reinterpret_cast<__gm__ uint64_t *>(metadata->scalars_address);
    payload->function_bin_addr = function_address;
    for (uint32_t i = 0; i < tensors; ++i)
        payload->args[i] = tensor_address + uint64_t{i} * TASK_IMAGE_TENSOR_STRIDE;
    for (uint32_t i = 0; i < scalars; ++i)
        payload->args[tensors + i] = scalar_data[i];
    payload->local_context.block_idx = block_idx;
    payload->local_context.block_num = block_num;
    payload->global_context.sub_block_id = subtask_slot == 2 ? 1 : 0;
    percounter_writeback_dispatch_payload(payload, tensors + scalars);
    return true;
}

/**
 * Record `count` finished subtasks of `task`; called after their store
 * barriers. A body sink's subtasks also count toward its shell, whose counter
 * is preset against every sink subtask of the body.
 */
inline __aicore__ void percounter_publish_completion(
    const LaneConfig &cfg, const PercounterTask &task, uint64_t completion_address, uint64_t count
) {
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Complete);
    percounter_gm_fetch_add_issue(*reinterpret_cast<__gm__ uint64_t *>(completion_address), count);
    if (task.sink) percounter_gm_fetch_add_issue(cfg.counters[task.shell].value, count);
}

/** True when the lane should stop: EXIT is signalled or some lane has failed. */
inline __aicore__ bool percounter_should_stop(const LaneConfig &cfg) {
    return static_cast<uint32_t>(read_reg(RegId::DATA_MAIN_BASE)) == AICORE_EXIT_SIGNAL ||
           percounter_gm_read(cfg.control->lane_error) != 0;
}

inline __aicore__ __gm__ pto2::resident::AsyncGroupData *
percounter_init_async_record(__gm__ PercounterControl *control, int block_idx) {
    using pto2::resident::AsyncGroupData;
    const uint64_t lane = static_cast<uint64_t>(block_idx);
    auto *record = scheduler_state_at<AsyncGroupData>(
        control, percounter_gm_read(control->async_groups_offset) + lane * sizeof(AsyncGroupData)
    );
    record->run_epoch = percounter_gm_read(control->async_run_epoch);
    record->task_ref = 0;
    record->group_index = static_cast<uint32_t>(lane);
    record->block_index = 0;
    record->subtask_slot = 0;
    record->capacity = PERCOUNTER_ASYNC_CAPACITY;
    record->entries_bytes = PERCOUNTER_ASYNC_CAPACITY * sizeof(DeferredCompletionEntry);
    record->count = 0;
    record->error_code = SIMPLER_ERROR_NONE;
    record->reservation_serial = 0;
    record->reservation_limit = 0;
    record->reservation_begin = 0;
    record->reservation_written = 0;
    record->state = pto2::resident::AsyncPublicationState::OPEN;
    record->sdma_guards_offset = 0;
    record->sdma_guard_count = 0;
    record->entries_offset = percounter_gm_read(control->async_entries_offset) + lane * record->entries_bytes;
    for (uint64_t offset = 0; offset < sizeof(AsyncGroupData); offset += 64)
        dcci(reinterpret_cast<__gm__ uint8_t *>(record) + offset, SINGLE_CACHE_LINE, CACHELINE_OUT);
    dsb((mem_dsb_t)0);
    return record;
}

/** Inputs were acquired while preparing a command whose owner proved readiness. */
template <typename EnterKernel>
inline __aicore__ bool percounter_run_prepared(
    const LaneConfig &cfg, const PercounterTask &task, __gm__ DispatchPayload *payload, EnterKernel enter_kernel,
    bool *skipped
) {
    *skipped = false;
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Run);
    const bool timing_enabled = cfg.profile != nullptr || cfg.profile_buffer != nullptr;
    const uint64_t receive_time = timing_enabled ? get_sys_cnt_aicore() : 0;
    if (cfg.has_predicates) {
        const SchedulerPredicateResult verdict =
            scheduler_evaluate_task_predicate(percounter_task_graph(task), task.local);
        if (verdict == SchedulerPredicateResult::MALFORMED) {
            percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
            return false;
        }
        if (verdict == SchedulerPredicateResult::FAIL) {
            *skipped = true;
            return true;
        }
    }
    if (payload->function_bin_addr == 0) {
        *skipped = true;
        return true;
    }

    __gm__ ChipSwimlaneAicoreTaskRecord *record = nullptr;
    if (cfg.profile != nullptr) {
        record = chip_swimlane_aicore_wait_task_record(cfg.profile_head, cfg.profile);
        if (record == nullptr) {
            percounter_fail(cfg, SchedulerGraphResult::TIMEOUT);
            return false;
        }
    }

    if (!enter_kernel()) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS, task.id);
        return false;
    }
    // The barrier is the publication fence a consumer's spin depends on: the
    // kernel's outputs are in memory before the atomic that announces them.
    PercounterKernelFunc kernel = reinterpret_cast<PercounterKernelFunc>(payload->function_bin_addr);
    const bool first_kernel = cfg.work_window.start_cycle == 0;
    const uint64_t start_time = timing_enabled || first_kernel ? get_sys_cnt_aicore() : 0;
    if (first_kernel) cfg.work_window.start_cycle = start_time;
    if constexpr (profile_compiled) {
        if (cfg.scalar != nullptr) ++cfg.scalar->depth;
    }
    kernel(reinterpret_cast<__gm__ int64_t *>(payload->args));
    OUT_OF_ORDER_STORE_BARRIER();
    if constexpr (profile_compiled) {
        if (cfg.scalar != nullptr) --cfg.scalar->depth;
    }
    const uint64_t end_time = get_sys_cnt_aicore();
    cfg.work_window.end_cycle = end_time;
    if (timing_enabled) {
        cfg.pending_task.finish = end_time;
        cfg.pending_task.dispatch = receive_time;
        cfg.pending_task.start = start_time;
        const TaskId token = task.shell < 0 ? TaskId::make_global(static_cast<int32_t>(task.id)) :
                                              TaskId::make_sub_task(task.shell, task.local);
        TaskId::assign(cfg.pending_task.task_token, token);
        cfg.pending_stream_record = record;
        cfg.pending_task_valid = true;
    }
    return true;
}

inline __aicore__ bool percounter_metadata_ready(const LaneConfig &cfg, int64_t id) {
    if (static_cast<uint64_t>(id) < cfg.table_task_count) return true;
    return percounter_gm_read(cfg.metadata_ready[id]) != 0;
}

}  // namespace
}  // namespace simpler::percounter

#include "cluster_loop.h"

namespace simpler::percounter {
namespace {

using PercounterLaneState = PercounterClusterLane;

inline __aicore__ bool percounter_step(const LaneConfig &cfg, PercounterLaneState *state, bool *executed) {
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Step);
    return state != nullptr && state->step(executed);
}

template <int32_t ClusterLane>
inline __aicore__ void percounter_lane_loop(LaneConfig &cfg) {
    cfg.cluster_lane = ClusterLane;
    if (percounter_should_stop(cfg)) return;
    PercounterLaneState state(cfg);
    for (uint64_t iteration = 0; !state.done(); ++iteration) {
        bool executed = false;
        if (!percounter_step(cfg, &state, &executed)) break;
        if constexpr (profile_compiled) {
            if (cfg.scalar != nullptr) cfg.scalar->end_iteration(executed);
        }
        if ((iteration % PERCOUNTER_SPIN_CHECK_INTERVAL) == 0 && percounter_should_stop(cfg)) {
            state.stop();
            break;
        }
    }
    percounter_commit_task_timing(cfg);
}

inline __aicore__ AicoreWorkWindow
run_percounter_lane(__gm__ PercounterControl *control, int block_idx, CoreType core_type) {
    // The caller invalidates the data cache after observing AICPU READY.
    // Configuration is immutable after that publication; use cached loads here.
    // Progress, dependency and exit words retain their bypass/atomic accesses.
    const bool is_aic = core_type == CoreType::AIC;
    SchedulerClusterCoordinate coordinate{-1, -1};
    const uint64_t cluster_count = control->cluster_count;
    const uint64_t aiv_per_cluster = control->aiv_per_cluster;
    if (!scheduler_cluster_coordinate_from_worker(
            block_idx, is_aic, static_cast<int32_t>(cluster_count), static_cast<int32_t>(aiv_per_cluster), &coordinate
        )) {
        // A lane outside the topology the AICPU published has no payload slot.
        percounter_gm_compare_exchange(
            control->lane_error, uint64_t{0}, static_cast<uint64_t>(SchedulerGraphResult::INVALID_ARGUMENTS)
        );
        return {};
    }

    LaneConfig cfg{};
    PercounterScalarTrace scalar{};
    PercounterScalarBinding scalar_binding;
    uint32_t task_count = 0;
    uint32_t task_dropped = 0;
    const uint64_t level = control->profiling_level;
    if (level >= 2) {
        cfg.profile_buffer =
            scheduler_state_at<PercounterProfileBuffer>(control, control->profile_buffers_offset) + block_idx;
        cfg.task_records = scheduler_state_at<PercounterTaskTiming>(control, cfg.profile_buffer->task_records_offset);
        cfg.profile_task_count = &task_count;
        cfg.task_dropped = &task_dropped;
        cfg.profile_buffer->worker_id = block_idx;
        cfg.profile_buffer->physical_core_id = get_physical_core_id();
        cfg.profile_buffer->core_type = static_cast<int32_t>(core_type);
        cfg.profile_buffer->cluster_index = coordinate.cluster_index;
        cfg.profile_buffer->cluster_lane = coordinate.cluster_lane;
        if constexpr (profile_compiled) {
            scalar.buffer = cfg.profile_buffer;
            scalar.records = scheduler_state_at<uint64_t>(control, scalar.buffer->scalar_records_offset);
            scalar.window_start = percounter_scalar_clock();
            scalar.function_min_cycles = level >= 4 ? 0 : PLATFORM_PROF_SYS_CNT_FREQ / 1000000;
            cfg.scalar = &scalar;
            scalar_binding.attach(&scalar);
        }
    }
    ProfileScope setup_scope(cfg.scalar, ProfilePhase::LaneSetup);
    cfg.control = control;
    const uint64_t storage_count = control->graph_storage_count;
    cfg.storage_address = control->graph_storage_address;
    cfg.payloads = scheduler_state_at<DispatchPayload>(
        control, control->dispatch_payloads_offset +
                     static_cast<uint64_t>(block_idx) * PERCOUNTER_PAYLOAD_SLOT_COUNT * sizeof(DispatchPayload)
    );
    cfg.tickets = scheduler_state_at<PercounterTicket>(control, control->tickets_offset);
    cfg.metadata = scheduler_state_at<PercounterTaskMetadata>(control, control->metadata_offset);
    cfg.metadata_ready = scheduler_state_at<uint64_t>(control, control->metadata_ready_offset);
    cfg.task_claims = scheduler_state_at<PercounterTaskClaim>(control, control->task_claims_offset);
    cfg.resource_nodes = scheduler_state_at<PercounterResourceNode>(control, control->resource_nodes_offset);
    cfg.ordinary_pieces = scheduler_state_at<PercounterOrdinaryPiece>(control, control->ordinary_pieces_offset);
    cfg.projection_task_map = scheduler_state_at<uint32_t>(control, control->projection_task_map_offset);
    cfg.sync_boundaries = scheduler_state_at<PercounterSyncBoundary>(control, control->sync_boundaries_offset);
    for (uint32_t catalog = 0; catalog < PERCOUNTER_LIST_COUNT; ++catalog)
        cfg.projection_catalog[catalog] =
            scheduler_state_at<uint32_t>(control, control->projection_catalog_offset[catalog]);
    cfg.fanin = scheduler_state_at<PercounterFanin>(control, control->fanin_offset);
    cfg.fanin_addresses = scheduler_state_at<uint64_t>(control, control->fanin_addr_offset);
    cfg.counters = scheduler_state_at<PercounterCounter>(control, control->counters_offset);
    cfg.completion_sectors =
        scheduler_state_at<PercounterCompletionSector>(control, control->completion_sectors_offset);
    cfg.task_count = control->task_count;
    cfg.table_task_count = control->table_task_count;
    if (cfg.table_task_count != storage_count) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_TASK_COUNT);
        return {};
    }
    cfg.has_predicates = control->has_predicates != 0;
    cfg.async_group = percounter_init_async_record(control, block_idx);
    cfg.async_entries = scheduler_state_at<DeferredCompletionEntry>(
        control, control->async_entries_offset +
                     static_cast<uint64_t>(block_idx) * PERCOUNTER_ASYNC_CAPACITY * sizeof(DeferredCompletionEntry)
    );
    cfg.async_completions = scheduler_state_at<PercounterDeferredGroup>(control, control->async_completions_offset);
    cfg.async_completion_capacity = control->async_completion_capacity;
    cfg.cluster_index = coordinate.cluster_index;
    cfg.cluster_lane = coordinate.cluster_lane;

    cfg.cluster_ready = scheduler_state_at<uint64_t>(control, control->cluster_ready_offset) + coordinate.cluster_index;
    cfg.cluster_owner = scheduler_state_at<PercounterClusterOwnerSector>(control, control->cluster_owners_offset) +
                        coordinate.cluster_index;
    cfg.ssbuf = percounter_ssbuf_region(platform_ssbuf_hardware_base_address());
    dsb((mem_dsb_t)0);

    ChipSwimlaneAicoreLocalState profile{};
    if (level == 1 && SIMPLER_GET_DFX_FLAG(get_aicore_profiling_flag(), SIMPLER_DFX_FLAG_CHIP_SWIMLANE)) {
        cfg.profile_head = get_chip_swimlane_aicore_head();
        if (cfg.profile_head != nullptr) cfg.profile = &profile;
    }
    setup_scope.close();
    if (cfg.cluster_lane == 0) percounter_lane_loop<0>(cfg);
    else if (cfg.cluster_lane == 1) percounter_lane_loop<1>(cfg);
    else percounter_lane_loop<2>(cfg);
    percounter_commit_task_timing(cfg);
    if (cfg.profile != nullptr) percounter_flush_stream_records(&profile);
    if (cfg.profile_buffer != nullptr) {
        cfg.profile_buffer->task_committed = task_count;
        cfg.profile_buffer->task_dropped = task_dropped;
        auto *records = reinterpret_cast<__gm__ uint8_t *>(cfg.task_records);
        for (uint64_t offset = 0; offset < uint64_t{task_count} * sizeof(PercounterTaskTiming); offset += 64)
            dcci(records + offset, SINGLE_CACHE_LINE, CACHELINE_OUT);
        if constexpr (profile_compiled) scalar.finish(percounter_scalar_clock());
        dcci(cfg.profile_buffer, SINGLE_CACHE_LINE, CACHELINE_OUT);
        dcci(reinterpret_cast<__gm__ uint8_t *>(cfg.profile_buffer) + 64, SINGLE_CACHE_LINE, CACHELINE_OUT);
        dsb((mem_dsb_t)0);
    }
    return cfg.work_window;
}

/**
 * A percounter AICore, from its report to the COND nonce acknowledgement.
 *
 * The same startup and retirement as the resident executor, minus the
 * scheduler bootstrap: the host already built every table, so a lane goes from
 * READY straight to the register release and into its loop.
 */
inline __aicore__ void percounter_aicore_execute(__gm__ Runtime *runtime, int block_idx, CoreType core_type) {
    __gm__ Handshake *handshake = (__gm__ Handshake *)(&runtime->dev.workers[block_idx]);
    __gm__ PercounterControl *control =
        reinterpret_cast<__gm__ PercounterControl *>(runtime->dev.scheduler_bootstrap.worker_context_base);
    percounter_observe_cache_line(handshake);

    const uint64_t report_epoch = get_aicore_report_epoch();
    handshake->work_start_cycle = 0;
    handshake->work_end_cycle = 0;
    if (report_epoch != 0) {
        aicore_stage_native_report(
            handshake, get_physical_core_id(), core_type, static_cast<uint32_t>(block_idx) + 1, 0
        );
        OUT_OF_ORDER_STORE_BARRIER();
        handshake->report_epoch = report_epoch;
    } else {
        handshake->physical_core_id = get_physical_core_id();
        handshake->core_type = core_type;
        OUT_OF_ORDER_STORE_BARRIER();
        handshake->aicore_done = block_idx + 1;
    }
    dcci(handshake, SINGLE_CACHE_LINE, CACHELINE_OUT);
    dsb((mem_dsb_t)0);

    // The host prefills the timeout, so it bounds this wait before the AICPU
    // has written anything.
    percounter_observe_data_cache(reinterpret_cast<__gm__ void *>(control));
    const uint64_t timeout_cycles = percounter_gm_read(control->scheduler_timeout_cycles);
    bool released = false;
    uint32_t signal = static_cast<uint32_t>(read_reg(RegId::DATA_MAIN_BASE));
    const uint64_t ready_wait_start = get_sys_cnt_aicore();
    while (handshake->aicpu_ready != SCHEDULER_RUNTIME_MODE_PERCOUNTER && signal != AICORE_EXIT_SIGNAL) {
        percounter_observe_cache_line(handshake);
        if (scheduler_watchdog_expired(ready_wait_start, get_sys_cnt_aicore(), timeout_cycles)) {
            percounter_gm_compare_exchange(
                control->lane_error, uint64_t{0}, static_cast<uint64_t>(SchedulerGraphResult::TIMEOUT)
            );
            signal = AICORE_EXIT_SIGNAL;
            break;
        }
        signal = static_cast<uint32_t>(read_reg(RegId::DATA_MAIN_BASE));
        SPIN_WAIT_HINT();
    }
    if (signal != AICORE_EXIT_SIGNAL) {
        // The AICPU published the topology before READY; re-observe the block.
        percounter_observe_data_cache(reinterpret_cast<__gm__ void *>(control));
        const uint64_t release_wait_start = get_sys_cnt_aicore();
        while (signal != AICPU_IDLE_TASK_ID && signal != AICORE_EXIT_SIGNAL) {
            if (scheduler_watchdog_expired(release_wait_start, get_sys_cnt_aicore(), timeout_cycles)) {
                percounter_gm_compare_exchange(
                    control->lane_error, uint64_t{0}, static_cast<uint64_t>(SchedulerGraphResult::TIMEOUT)
                );
                break;
            }
            signal = static_cast<uint32_t>(read_reg(RegId::DATA_MAIN_BASE));
            SPIN_WAIT_HINT();
        }
        released = signal == AICPU_IDLE_TASK_ID;
    }
    if (released) {
        write_reg(RegId::COND, AICORE_IDLE_VALUE);
        const auto work_window = run_percounter_lane(control, block_idx, core_type);
        handshake->work_start_cycle = work_window.start_cycle;
        handshake->work_end_cycle = work_window.end_cycle;
        dcci(handshake, SINGLE_CACHE_LINE, CACHELINE_OUT);
        dsb((mem_dsb_t)0);
    }
    // A lane arrives here when its share of the run is done, which can be long
    // before the run ends, or after a local watchdog ended a wait above. The
    // AICPU sends EXIT on every outcome once its supervisor returns, and the
    // supervisor has its own progress watchdog, so this wait needs none. EXIT
    // is published only after the return gate is reset, so wait for it before
    // acknowledging, and complete the preceding GM writes first.
    wait_for_aicpu_exit_signal();
    OUT_OF_ORDER_STORE_BARRIER();
    acknowledge_aicore_cond_nonce_and_wait(&runtime->dev.teardown_gates[block_idx]);
}

}  // namespace
}  // namespace simpler::percounter
