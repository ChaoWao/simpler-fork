/*
 * Copyright (c) PyPTO Contributors.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 * -----------------------------------------------------------------------------------------------------------
 */

#pragma once

template <typename T>
inline __aicore__ const __gm__ T *
percounter_calendar_table(const __gm__ uint8_t *image, uint64_t bytes, uint32_t offset, uint64_t count) {
    if (offset < sizeof(CalendarRangeHeader) || offset % alignof(T) != 0 || offset > bytes ||
        count > (bytes - offset) / sizeof(T))
        return nullptr;
    return reinterpret_cast<const __gm__ T *>(image + offset);
}

template <typename Ready>
inline __aicore__ bool percounter_calendar_wait(const LaneConfig &cfg, Ready ready) {
    for (uint32_t iteration = 0; !ready(); ++iteration) {
        if (iteration % PERCOUNTER_SPIN_CHECK_INTERVAL == 0 && percounter_should_stop(cfg)) return false;
        SPIN_WAIT_HINT();
    }
    return true;
}

inline __aicore__ bool percounter_calendar_publish_defer(
    const LaneConfig &cfg, PercounterLaneState *state, uint32_t task
) {
    const uint64_t ref = state->async_head;
    if (ref == 0 || ref > cfg.async_completion_capacity) {
        percounter_fail(cfg, SchedulerGraphResult::ASYNC_REGISTRATION_FAILED, task);
        return false;
    }
    auto &group = cfg.async_completions[ref - 1];
    percounter_gm_store(group.calendar_defer_owner, uint64_t{0});
    auto &head = cfg.counters[task].calendar_defer_head;
    uint64_t previous = percounter_gm_read(head);
    for (;;) {
        percounter_gm_store(group.next, previous);
        percounter_memory_barrier();
        const uint64_t observed = percounter_gm_compare_exchange(head, previous, ref);
        if (observed == previous) break;
        previous = observed;
    }
    state->async_head = 0;
    return true;
}

inline __aicore__ bool percounter_calendar_service_defer(
    const LaneConfig &cfg, uint32_t task, uint64_t *cursor
) {
    const uint64_t ref = *cursor != 0 ? *cursor : percounter_gm_read(cfg.counters[task].calendar_defer_head);
    if (ref == 0) return true;
    if (ref > cfg.async_completion_capacity) {
        percounter_fail(cfg, SchedulerGraphResult::ASYNC_COMPLETION_INVALID, task);
        return false;
    }
    auto &group = cfg.async_completions[ref - 1];
    *cursor = percounter_gm_read(group.next);
    const uint64_t lane = 1 + cfg.cluster_index * PLATFORM_CORES_PER_BLOCKDIM + cfg.cluster_lane;
    auto &owner_word = group.calendar_defer_owner;
    uint64_t owner = percounter_gm_read(owner_word);
    if (owner == 0) {
        owner = percounter_gm_compare_exchange(owner_word, uint64_t{0}, lane);
        if (owner == 0) owner = lane;
    }
    if (owner != lane) return true;

    const uint64_t pending = percounter_gm_read(group.pending_mask);
    uint64_t remaining = pending;
    for (uint64_t conditions = pending; conditions != 0; conditions &= conditions - 1) {
        const uint32_t index = static_cast<uint32_t>(__builtin_ctzll(conditions));
        auto &entry = group.entries[index];
        percounter_observe_cache_line(&entry);
        const CompletionToken token{entry.addr, entry.expected_value, entry.engine, entry.completion_type,
                                    entry.backend_cookie};
        const CompletionPollResult result = percounter_poll_valid_completion(token);
        if (result.state == CompletionPollState::FAILED) {
            percounter_fail(cfg, SchedulerGraphResult::ASYNC_COMPLETION_INVALID, task);
            return false;
        }
        if (result.state == CompletionPollState::READY) remaining &= ~(uint64_t{1} << index);
    }
    if (remaining != pending) percounter_gm_store(group.pending_mask, remaining);
    if (remaining != 0) return true;

    percounter_memory_barrier();
    percounter_issue_completion(percounter_gm_read(group.completion_address), 1);
    const uint64_t issued = percounter_task_event_clock(cfg);
    percounter_memory_barrier();
    percounter_gm_fetch_add_issue(cfg.control->async_settled, uint64_t{1});
    percounter_gm_store(owner_word, UINT64_MAX);
    percounter_record_task_event(cfg, PercounterTaskEventKind::DeferredCompletionIssue, 0, issued, ref);
    return true;
}

inline __aicore__ bool
percounter_calendar_wait_list(const LaneConfig &cfg, const __gm__ CalendarWait *waits, uint32_t count) {
    ProfileScope profile_scope(cfg.scalar, ProfilePhase::Fanin);
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t command = waits[i].read_cmd;
        const uint64_t target = waits[i].target;
        const uint64_t tag = command & CALENDAR_WAIT_TAG_MASK;
        const uint64_t address = command & ~CALENDAR_WAIT_TAG_MASK;
        if (tag == CALENDAR_WAIT_NOTIFY) {
            auto *counter = reinterpret_cast<__gm__ volatile uint32_t *>(address);
            if (!percounter_calendar_wait(cfg, [&]() __aicore__ {
                    return percounter_notify_query(*counter) >= target;
                }))
                return false;
        } else if (tag == CALENDAR_WAIT_GM_DONE) {
            if (!percounter_calendar_wait(cfg, [&]() __aicore__ {
                    return percounter_counter_published(percounter_observe(address));
                }))
                return false;
        } else if (tag == CALENDAR_WAIT_GM_TARGET) {
            if (!percounter_calendar_wait(cfg, [&]() __aicore__ {
                    return percounter_observe(address) >= target;
                }))
                return false;
        } else if (tag == CALENDAR_WAIT_DEFER) {
            const uint32_t task = static_cast<uint32_t>(target >> 32);
            const uint32_t threshold = static_cast<uint32_t>(target);
            if (task >= cfg.table_task_count) {
                percounter_fail(cfg, SchedulerGraphResult::INVALID_TASK_ID, task);
                return false;
            }
            uint64_t cursor = 0;
            bool failed = false;
            if (!percounter_calendar_wait(
                    cfg,
                    [&]() __aicore__ {
                        if (threshold == 0) {
                            auto *counter = reinterpret_cast<__gm__ volatile uint64_t *>(address);
                            if (percounter_counter_published(percounter_gm_read(*counter))) return true;
                        } else {
                            auto *notify = reinterpret_cast<__gm__ volatile uint32_t *>(address);
                            if (percounter_notify_query(*notify) >= threshold) return true;
                        }
                        failed = !percounter_calendar_service_defer(cfg, task, &cursor);
                        return failed;
                    }
                ) ||
                failed)
                return false;
        } else {
            percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS, -1, command);
            return false;
        }
    }
    return true;
}

inline __aicore__ void percounter_calendar_add(uint64_t command) {
    if (command == 0) return;
    if ((command & CALENDAR_ADD_NOTIFY) != 0) {
        uint64_t address = command & ~CALENDAR_ADD_NOTIFY;
#if defined(__CPU_SIM)
        // Simulated notifications keep the counter at the SET/read address.
        address -= percounter_notify_add_address(1) - 1;
#endif
        percounter_notify_add(*reinterpret_cast<__gm__ volatile uint32_t *>(address), 1);
        return;
    }
    percounter_gm_fetch_add_issue(*reinterpret_cast<__gm__ uint64_t *>(command), uint64_t{1});
}

inline __aicore__ bool percounter_calendar_start(
    const LaneConfig &cfg, PercounterLaneState *state, const __gm__ CalendarExecutionExtra *extra, uint32_t block,
    uint32_t blocks
) {
    const uint32_t sync = extra->sync_index;
    if (sync >= cfg.control->sync_cohort_count || blocks < 2) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS, extra->task_id, sync);
        return false;
    }
    const auto notify = percounter_sync_notify(cfg, state, sync);
    percounter_memory_barrier();
    percounter_start_arrive(cfg, notify, sync, block);
    return percounter_calendar_wait(cfg, [&]() __aicore__ {
        return percounter_start_ready(cfg, notify, sync, blocks, block);
    });
}

inline __aicore__ bool percounter_calendar_enter(
    const LaneConfig &cfg, PercounterLaneState *state, const __gm__ CalendarExecutionExtra *extra,
    const __gm__ CalendarLocalJoin *join, const __gm__ CalendarWait *waits, uint32_t wait_count,
    uint32_t sequence_base, uint32_t block, uint32_t blocks, bool sync
) {
    if (join == nullptr) {
        if (!percounter_calendar_wait_list(cfg, waits, wait_count)) return false;
        return !sync || percounter_calendar_start(cfg, state, extra, block, blocks);
    }

    const uint32_t lane = static_cast<uint32_t>(cfg.cluster_lane);
    const uint32_t lead = join->leader_lane;
    uint32_t arrivals[PLATFORM_CORES_PER_BLOCKDIM]{};
    for (uint32_t peer = 0; peer < PLATFORM_CORES_PER_BLOCKDIM; ++peer) {
        const uint32_t ordinal = join->arrive[peer];
        if (ordinal != 0) arrivals[peer] = sequence_base + ordinal;
    }
    auto *signal = &cfg.ssbuf->mix[0];
    // Tokens are per-lane ordinals across phases. A newer token also
    // satisfies an observer still polling an older arrival or release.
    if (lead != lane) {
        percounter_ssbuf_store_release(&signal->enter_ready[lane], arrivals[lane]);
        return percounter_calendar_wait(cfg, [&]() __aicore__ {
            return percounter_ssbuf_load_field(&signal->enter_idle[lead]) >= arrivals[lead];
        });
    }
    if (!percounter_calendar_wait(cfg, [&]() __aicore__ {
            for (uint32_t peer = 0; peer < PLATFORM_CORES_PER_BLOCKDIM; ++peer)
                if (peer != lane && arrivals[peer] != 0 &&
                    percounter_ssbuf_load_field(&signal->enter_ready[peer]) < arrivals[peer])
                    return false;
            return true;
        }))
        return false;
    // Only the selected role checks data dependencies after all local roles
    // have reached the same calendar block.
    if (!percounter_calendar_wait_list(cfg, waits, wait_count)) return false;
    if (sync && !percounter_calendar_start(cfg, state, extra, block, blocks)) return false;
    percounter_ssbuf_store_release(&signal->enter_idle[lane], arrivals[lane]);
    return true;
}

inline __aicore__ bool percounter_calendar_execute_range(
    const LaneConfig &cfg, PercounterLaneState *state, const __gm__ uint8_t *image, uint64_t bytes,
    const __gm__ CalendarRange &range, uint32_t sample_offset, uint32_t worker_row_begin
) {
    const uint32_t row_count = range.row_count;
    const uint32_t entry_wait_count = range.entry_wait_count;
    const uint32_t binding_offset = range.binding_offset;
    const uint32_t sequence_base = range.sequence_base;
    const auto *rows = percounter_calendar_table<CalendarExecutionRow>(image, bytes, range.rows_offset, row_count);
    if ((row_count != 0 && (rows == nullptr || binding_offset < sizeof(CalendarRangeHeader) ||
                            binding_offset >= bytes || binding_offset % alignof(CalendarDispatch) != 0))) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
        return false;
    }
    const __gm__ uint8_t *binding = image + binding_offset;
    __gm__ CalendarTiming *samples = nullptr;
    if (sample_offset != 0) {
        samples = const_cast<__gm__ CalendarTiming *>(
            percounter_calendar_table<CalendarTiming>(image, bytes, sample_offset, row_count)
        );
        if (samples == nullptr) {
            percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
            return false;
        }
    }
    if (entry_wait_count != 0) {
        const auto *waits =
            percounter_calendar_table<CalendarWait>(image, bytes, range.entry_wait_offset, entry_wait_count);
        if (waits == nullptr) {
            percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
            return false;
        }
        if (!percounter_calendar_wait_list(cfg, waits, entry_wait_count)) return false;
        // AICPU publishes an invocation's late binding before its entry watermark.
        percounter_memory_barrier();
    }

    const bool profile_tasks = cfg.profile != nullptr || cfg.profile_buffer != nullptr;
    for (uint32_t i = 0; i < row_count; ++i) {
        if (((worker_row_begin + i) % PERCOUNTER_SPIN_CHECK_INTERVAL) == 0 && percounter_should_stop(cfg)) return false;
        const auto *row = rows + i;
        const uint16_t flags = row->flags;
        const uint32_t block = row->block_index;
        const uint32_t blocks = row->block_count;
        const uint32_t role = flags & CALENDAR_EXEC_ROLE_MASK;
        const bool sync = (flags & CALENDAR_EXEC_SYNC_START) != 0;
        const bool sink = (flags & CALENDAR_EXEC_SINK) != 0;
        const bool control_row = (flags & CALENDAR_EXEC_CONTROL) != 0;
        const auto *dispatch = reinterpret_cast<const __gm__ CalendarDispatch *>(binding + row->dispatch_offset);
        const auto *waits = reinterpret_cast<const __gm__ CalendarWait *>(binding + dispatch->waits_offset);
        const auto *extra =
            profile_tasks || sync || sink || (flags & CALENDAR_EXEC_DEFER) != 0 ?
                reinterpret_cast<const __gm__ CalendarExecutionExtra *>(binding + dispatch->extra_offset) :
                nullptr;
        const bool permit_defer = (flags & CALENDAR_EXEC_DEFER) != 0;
        bool deferred = false;
        const uint32_t mix_delta = row->mix_delta;
        const auto *join = mix_delta != 0 ? reinterpret_cast<const __gm__ CalendarLocalJoin *>(
                                                reinterpret_cast<const __gm__ uint8_t *>(row) + mix_delta
                                            ) :
                                            nullptr;
        const __gm__ volatile uint64_t &done_command = dispatch->done_add_cmd;
        const uint64_t done_add_cmd = done_command;
        if (!percounter_calendar_enter(cfg, state, extra, join, waits, row->wait_count, sequence_base, block, blocks, sync))
            return false;

        const uint64_t entry = profile_tasks ? percounter_order_entry(extra->task_id, block, blocks) : 0;
        percounter_record_task_event(cfg, PercounterTaskEventKind::Ready, entry, percounter_task_event_clock(cfg));
        __gm__ CalendarTiming *sample = samples != nullptr ? samples + i : nullptr;
        if (control_row) {
            percounter_memory_barrier();
            if (sample != nullptr) {
                const uint64_t cycles = get_sys_cnt_aicore();
                sample->start = cycles;
                sample->end = cycles;
            }
        } else {
            PercounterTask task{};
            task.shell = -1;
            if (profile_tasks || permit_defer) {
                task.id = extra->task_id;
                task.local = extra->local_task_id;
                task.shell = extra->shell_task_id;
            }
            task.arguments_address = dispatch->args_address;
            task.function_address = dispatch->kernel_entry;
            if (task.function_address == 0 || task.arguments_address == 0) {
                percounter_fail(cfg, SchedulerGraphResult::INVALID_CALLABLE, task.id, role);
                return false;
            }
            if (permit_defer) {
                const uint64_t completion = percounter_completion_address(cfg, task.id);
                if (!percounter_run_task(cfg, state, task, block, blocks, role, completion, &deferred, entry, sample))
                    return false;
                if (deferred && !percounter_calendar_publish_defer(cfg, state, static_cast<uint32_t>(task.id)))
                    return false;
            } else if (!percounter_run_task<false>(cfg, state, task, block, blocks, role, 0, &deferred, entry, sample)) {
                return false;
            }
        }
        if (!deferred) {
            ProfileScope profile_scope(cfg.scalar, ProfilePhase::Complete);
            percounter_calendar_add(done_add_cmd);
            const uint64_t issued = percounter_task_event_clock(cfg);
            if (sink) percounter_calendar_add(extra->shell_add_cmd);
            percounter_record_task_event(cfg, PercounterTaskEventKind::CompletionIssue, entry, issued, done_add_cmd != 0);
        }
        percounter_commit_task_timing(cfg);
    }

    percounter_calendar_add(range.tail_add_cmd);
    return true;
}

inline __aicore__ void percounter_calendar_loop(const LaneConfig &cfg) {
    const uint64_t offset = cfg.control->calendar_offset;
    const uint64_t image_bytes = cfg.control->image_bytes;
    if (cfg.has_predicates || cfg.control->terminal.step_count != 0 || cfg.aiv_per_cluster != 2 ||
        offset < sizeof(PercounterControl) || offset % alignof(CalendarRangeHeader) != 0 || offset > image_bytes ||
        image_bytes - offset < sizeof(CalendarRangeHeader)) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
        return;
    }
    const auto *calendar = scheduler_state_at<CalendarRangeHeader>(cfg.control, offset);
    const auto *image = reinterpret_cast<const __gm__ uint8_t *>(calendar);
    const uint64_t bytes = image_bytes - offset;
    const uint32_t workers = calendar->worker_count;
    const uint32_t range_count = calendar->range_count;
    const auto *indices =
        percounter_calendar_table<uint32_t>(image, bytes, calendar->worker_index_offset, uint64_t{workers} + 1);
    const auto *ranges = percounter_calendar_table<CalendarRange>(image, bytes, calendar->range_offset, range_count);
    const uint32_t sample_offset = calendar->sample_index_offset;
    const auto *sample_indices =
        sample_offset == 0 ? nullptr : percounter_calendar_table<uint32_t>(image, bytes, sample_offset, range_count);
    if (workers != cfg.cluster_count * PLATFORM_CORES_PER_BLOCKDIM || indices == nullptr || ranges == nullptr ||
        (sample_offset != 0 && sample_indices == nullptr)) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
        return;
    }

    const uint32_t lane = static_cast<uint32_t>(cfg.cluster_lane);
    const uint32_t cluster = static_cast<uint32_t>(cfg.cluster_index);
    const uint32_t worker = lane == 0 ? cluster : cfg.cluster_count + cluster * cfg.aiv_per_cluster + lane - 1;
    const uint32_t begin = indices[worker];
    const uint32_t end = indices[worker + 1];
    if (begin > end || end > range_count) {
        percounter_fail(cfg, SchedulerGraphResult::INVALID_ARGUMENTS);
        return;
    }
    PercounterLaneState state{};
    uint32_t worker_row_begin = 0;
    for (uint32_t i = begin; i < end; ++i) {
        if (!percounter_calendar_execute_range(
                cfg, &state, image, bytes, ranges[i], sample_indices != nullptr ? sample_indices[i] : 0,
                worker_row_begin
            ))
            return;
        worker_row_begin += ranges[i].row_count;
    }
    if (sample_indices == nullptr) return;
    // Sample writeback must complete before the lane retires, off the phase gates.
    for (uint32_t i = begin; i < end; ++i) {
        const uint32_t sample_span = sample_indices[i];
        if (sample_span == 0) continue;
        const auto *samples = reinterpret_cast<const __gm__ CalendarTiming *>(image + sample_span);
        for (uint32_t j = 0; j < ranges[i].row_count; ++j)
            dcci(const_cast<__gm__ CalendarTiming *>(samples + j), SINGLE_CACHE_LINE, CACHELINE_OUT);
    }
    dsb((mem_dsb_t)0);
}
