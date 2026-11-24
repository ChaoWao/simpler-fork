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

#pragma once

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include "calendar_phases.h"
#include "calendar_plan.h"

namespace simpler::percounter {

struct RangeCalendarInput {
    const CalendarPhase *phase;
    std::shared_ptr<const CalendarPlan> plan;
    bool sample;
};

struct RangeCalendarImage {
    enum class WaitKind { COMPLETION, DEFER, PHASE_END, MATERIALIZED, REDUCE_SHARD };

    struct WaitPatch {
        uint32_t offset;
        WaitKind kind;
        uint32_t index;
        uint32_t target;
    };

    struct DispatchPatch {
        uint32_t offset;
        uint32_t global_task;
        int32_t shell;
        uint32_t role;
        uint32_t output_mask;
        uint32_t shard_add_index;
    };

    struct ExtraPatch {
        uint32_t offset;
        uint32_t global_task;
        int32_t shell;
    };

    struct PhaseEnd {
        uint32_t task{UINT32_MAX};
        uint32_t slot_offset{0};
        uint32_t contributors{0};
    };

    struct RangeTail {
        uint32_t range;
        uint32_t phase;
    };

    std::vector<std::byte> bytes;
    std::vector<WaitPatch> waits;
    std::vector<DispatchPatch> dispatches;
    std::vector<ExtraPatch> extras;
    std::vector<PhaseEnd> phase_ends;
    std::vector<RangeTail> tails;
    std::vector<uint32_t> sample_offsets;
    std::vector<uint32_t> graph_shells;
    bool reduce_shards{false};

    template <class T>
    uint32_t allocate(size_t count) {
        if (count == 0) return 0;
        const uint64_t offset = (bytes.size() + alignof(T) - 1) & ~uint64_t{alignof(T) - 1};
        if (offset == 0 || offset > UINT32_MAX || count > (UINT32_MAX - offset) / sizeof(T)) return 0;
        bytes.resize(offset + count * sizeof(T));
        return static_cast<uint32_t>(offset);
    }

    template <class T>
    void store(uint32_t offset, const T &value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    template <class T>
    T load(uint32_t offset) const {
        T result;
        std::memcpy(&result, bytes.data() + offset, sizeof(T));
        return result;
    }
};

namespace range_calendar_detail {

struct alignas(128) PhaseCounter {
    uint64_t value;
};

struct Template {
    CalendarHeader header;
    std::vector<uint32_t> indexes;
    std::vector<CalendarRow> rows;
    std::vector<CalendarLocalJoin> joins;
    std::vector<std::array<uint32_t, 3>> binding_indexes;
    uint32_t binding_count{0};
    uint32_t rows_offset{0};
    uint32_t joins_offset{0};
};

inline constexpr uint32_t ep_defer_rotation_count = 8;

inline uint32_t
defer_wait_variants(const CalendarPhase &phase, const CalendarPlan &plan, uint32_t task) {
    const auto &input = phase.tasks[task];
    if (phase.invocation >= 0 || input.active_mask != 2 || input.sync_start) return 1;
    const auto &parents = plan.waits.task_fanins[task];
    const auto deferred = std::count_if(parents.begin(), parents.end(), [&](uint32_t parent) {
        return phase.tasks[parent].calendar_defer_mask != 0;
    });
    return deferred == ep_defer_rotation_count ? ep_defer_rotation_count : 1;
}

inline bool decode_template(
    RangeCalendarImage &image, const CalendarPhase &phase, const CalendarPlan &plan, const HostGraphTemplate *graph,
    uint32_t clusters, Template &result
) {
    if (plan.bytes.size() < sizeof(result.header)) return false;
    std::memcpy(&result.header, plan.bytes.data(), sizeof(result.header));
    const auto &header = result.header;
    if (header.worker_count != clusters * 3 || header.row_count == 0 ||
        !calendar_detail::read_table(plan, header.index_offset, header.worker_count + 1, &result.indexes) ||
        !calendar_detail::read_table(plan, header.row_offset, header.row_count, &result.rows) ||
        !calendar_detail::read_table(plan, header.join_offset, header.join_count, &result.joins))
        return false;

    result.rows_offset = image.allocate<CalendarExecutionRow>(header.row_count);
    result.joins_offset = image.allocate<CalendarLocalJoin>(result.joins.size());
    if (result.rows_offset == 0 || (!result.joins.empty() && result.joins_offset == 0) ||
        result.indexes.front() != 0 || result.indexes.back() != header.row_count)
        return false;
    if (!result.joins.empty())
        std::memcpy(image.bytes.data() + result.joins_offset, result.joins.data(),
                    result.joins.size() * sizeof(CalendarLocalJoin));

    const auto &waits = plan.waits;
    if (waits.row_waits.size() != header.row_count || waits.task_fanins.size() != phase.tasks.size()) return false;
    result.binding_indexes.resize(phase.tasks.size());
    for (size_t task = 0; task < phase.tasks.size(); ++task) {
        // A core may cache the last dispatch in a published slice while
        // AICPU is still preparing the next one. Keep slices off the
        // same cache line so observing a newer watermark suffices.
        if (graph != nullptr && task % PERCOUNTER_GRAPH_SLICE_TASKS == 0)
            result.binding_count += result.binding_count % 2;
        result.binding_indexes[task].fill(UINT32_MAX);
        const auto mask = phase.tasks[task].active_mask;
        for (uint32_t role = 0; role < 3; ++role) {
            if ((mask & (1U << role)) || (mask == 0 && role == 1)) {
                result.binding_indexes[task][role] = result.binding_count;
                const uint32_t shards = phase.tasks[task].calendar_shard_round != 0 ? DS_PARK_REDUCE_SHARDS : 1;
                result.binding_count += shards * defer_wait_variants(phase, plan, task);
            }
        }
    }
    for (uint32_t worker = 0; worker < header.worker_count; ++worker) {
        const uint32_t begin = result.indexes[worker];
        const uint32_t end = result.indexes[worker + 1];
        if (begin > end || end > header.row_count) return false;
        const uint32_t lane = worker < clusters ? 0 : 1 + (worker - clusters) % 2;
        for (uint32_t i = begin; i < end; ++i) {
            const auto &row = result.rows[i];
            const uint32_t task = static_cast<uint32_t>(percounter_order_task_id(row.entry));
            if (task >= phase.tasks.size()) return false;
            const auto &input = phase.tasks[task];
            const uint32_t role = input.is_mix || lane == 0 ? lane : 1;
            const uint32_t base_binding = result.binding_indexes[task][role];
            const bool shard_wait = input.calendar_shard_round != 0 && !input.calendar_shard_producer;
            if (base_binding == UINT32_MAX || waits.task_fanins[task].size() + shard_wait > UINT16_MAX) return false;
            const uint32_t block = static_cast<uint32_t>(percounter_order_block_idx(row.entry));
            const uint32_t rotations = defer_wait_variants(phase, plan, task);
            const uint32_t binding = base_binding + calendar_shard(input, block) * rotations + worker % rotations;
            const uint32_t offset = result.rows_offset + i * sizeof(CalendarExecutionRow);
            uint32_t mix_delta = 0;
            uint16_t wait_count = shard_wait + (waits.row_waits[i] ?
                static_cast<uint16_t>(waits.task_fanins[task].size()) : 0);
            if (row.local_join != CALENDAR_NO_JOIN) {
                if (row.local_join >= result.joins.size()) return false;
                mix_delta = result.joins_offset + row.local_join * sizeof(CalendarLocalJoin) - offset;
                if (result.joins[row.local_join].leader_lane != lane) wait_count = 0;
            }
            uint16_t flags = static_cast<uint16_t>(role);
            if (input.sync_start) flags |= CALENDAR_EXEC_SYNC_START;
            if ((input.calendar_defer_mask & (1U << role)) != 0) flags |= CALENDAR_EXEC_DEFER;
            if (input.active_mask == 0) flags |= CALENDAR_EXEC_CONTROL;
            if (graph != nullptr && (graph->tasks[task].flags & PERCOUNTER_BODY_SINK)) flags |= CALENDAR_EXEC_SINK;
            image.store(offset, CalendarExecutionRow{
                static_cast<uint32_t>(binding * sizeof(CalendarDispatch)), mix_delta,
                static_cast<uint16_t>(percounter_order_block_idx(row.entry)),
                static_cast<uint16_t>(percounter_order_block_num(row.entry)), wait_count, flags
            });
        }
    }
    return true;
}

struct RangeRecord {
    CalendarRange range;
    uint32_t timing;
    int32_t end_phase{-1};
};

inline uint32_t physical_task(uint32_t local, const CalendarPhase &phase, const std::vector<uint32_t> &body_base) {
    return phase.invocation < 0 ? phase.task_ids[local] : body_base[phase.invocation] + phase.task_ids[local];
}

inline uint32_t sole_sink(const CalendarPhase &phase) {
    std::vector<bool> consumed(phase.tasks.size(), false);
    for (const auto &row : phase.fanins)
        for (const int32_t input : row)
            consumed[input] = true;
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < consumed.size(); ++i) {
        if (consumed[i]) continue;
        if (result != UINT32_MAX) return UINT32_MAX;
        result = i;
    }
    return result;
}

}  // namespace range_calendar_detail

inline bool build_range_calendar(
    const std::vector<RangeCalendarInput> &inputs, const GraphPlanInput &graphs, uint32_t outer_count,
    uint32_t clusters, RangeCalendarImage *out
) {
    using namespace range_calendar_detail;
    if (inputs.empty() || clusters == 0 || clusters > PLATFORM_MAX_BLOCKDIM || out == nullptr) return false;
    RangeCalendarImage image;
    image.bytes.resize(sizeof(CalendarRangeHeader));
    std::vector<uint32_t> body_base;
    uint32_t total_tasks = outer_count;
    for (size_t i = 0; i < graphs.invocation_count; ++i) {
        const auto &graph = graphs.invocations[i];
        const size_t tasks = graphs.templates[graph.template_index].tasks.size();
        if (tasks > UINT32_MAX - total_tasks) return false;
        body_base.push_back(total_tasks);
        total_tasks += static_cast<uint32_t>(tasks);
    }
    const uint32_t patch_offset = graphs.invocation_count == 0 ? 0 :
        image.allocate<CalendarDispatchPatch>(total_tasks);
    if (graphs.invocation_count != 0 && patch_offset == 0) return false;

    const uint32_t workers = clusters * 3;
    std::vector<std::vector<RangeRecord>> ranges(workers);
    std::map<const CalendarPlan *, Template> templates;
    uint32_t sequence_base = 0;
    uint32_t slot_count = 0;
    for (uint32_t p = 0; p < inputs.size(); ++p) {
        const auto &input = inputs[p];
        const auto &phase = *input.phase;
        std::vector<bool> polled(phase.tasks.size(), false);
        for (const auto &fanins : input.plan->waits.task_fanins)
            for (const auto pred : fanins)
                if (pred >= 0 && static_cast<size_t>(pred) < polled.size()) polled[pred] = true;
        for (size_t id = 0; id < phase.tasks.size(); ++id)
            if (phase.tasks[id].calendar_defer_mask != 0 && !polled[id]) return false;
        const auto *invocation = phase.invocation < 0 ? nullptr : &graphs.invocations[phase.invocation];
        const auto *graph = invocation == nullptr ? nullptr : &graphs.templates[invocation->template_index];
        const int32_t shell = invocation == nullptr ? -1 : static_cast<int32_t>(invocation->shell_task_id);
        if (shell >= 0) image.graph_shells.push_back(static_cast<uint32_t>(shell));
        auto [entry, inserted] = templates.try_emplace(input.plan.get());
        auto &tmpl = entry->second;
        if (inserted && !decode_template(image, phase, *input.plan, graph, clusters, tmpl)) return false;

        // Each Graph's patch writes own cache lines. Immutable range/wait
        // records may be read by other cores while it is being materialized.
        if (graph != nullptr) image.bytes.resize((image.bytes.size() + 63U) & ~size_t{63U});
        const uint32_t bindings = image.allocate<CalendarDispatch>(tmpl.binding_count);
        if (bindings == 0) return false;
        if (graph != nullptr) image.bytes.resize((image.bytes.size() + 63U) & ~size_t{63U});

        for (uint32_t task = 0; task < phase.tasks.size(); ++task) {
            const uint32_t global = physical_task(task, phase, body_base);
            const auto &task_input = phase.tasks[task];
            const bool sharded = task_input.calendar_shard_round != 0;
            if (sharded && graph != nullptr) return false;
            image.reduce_shards |= sharded;
            const bool shard_wait = sharded && !task_input.calendar_shard_producer;
            const auto &producers = input.plan->waits.task_fanins[task];
            const uint32_t rotations = defer_wait_variants(phase, *input.plan, task);
            std::array<uint32_t, DS_PARK_REDUCE_SHARDS * ep_defer_rotation_count> wait_offsets{};
            std::array<uint32_t, ep_defer_rotation_count> deferred{};
            if (rotations > 1) {
                uint32_t n = 0;
                for (uint32_t parent : producers)
                    if (phase.tasks[parent].calendar_defer_mask != 0) deferred[n++] = parent;
                if (n != rotations) return false;
            }
            const uint32_t wait_variants = (shard_wait ? DS_PARK_REDUCE_SHARDS : 1) * rotations;
            for (uint32_t group = 0; group < wait_variants; ++group) {
                const uint32_t wait_offset = image.allocate<CalendarWait>(producers.size() + shard_wait);
                if ((!producers.empty() || shard_wait) && wait_offset == 0) return false;
                wait_offsets[group] = wait_offset;
                if (shard_wait) {
                    image.waits.push_back({wait_offset, RangeCalendarImage::WaitKind::REDUCE_SHARD, group / rotations,
                                           DS_PARK_REDUCE_BLOCKS_PER_SHARD * task_input.calendar_shard_round});
                }
                uint32_t next = group % rotations;
                for (uint32_t i = 0; i < producers.size(); ++i) {
                    uint32_t producer = producers[i];
                    const bool is_deferred = phase.tasks[producer].calendar_defer_mask != 0;
                    if (is_deferred && rotations > 1) {
                        producer = deferred[next];
                        next = (next + 1) % rotations;
                    }
                    const auto kind = is_deferred ?
                        RangeCalendarImage::WaitKind::DEFER : RangeCalendarImage::WaitKind::COMPLETION;
                    image.waits.push_back({
                        static_cast<uint32_t>(wait_offset + (i + shard_wait) * sizeof(CalendarWait)),
                        kind, physical_task(producer, phase, body_base), 0
                    });
                }
            }
            const uint32_t extra_offset = image.allocate<CalendarExecutionExtra>(1);
            if (extra_offset == 0) return false;
            image.store(extra_offset, CalendarExecutionExtra{global, phase.task_ids[task], shell, PERCOUNTER_NO_SYNC, 0});
            image.extras.push_back({extra_offset, global, shell});
            CalendarDispatchPatch patch{};
            const uint32_t variants = (sharded ? DS_PARK_REDUCE_SHARDS : 1) * rotations;
            for (uint32_t group = 0; group < variants; ++group) {
                const uint32_t wait_offset = wait_offsets[shard_wait ? group : group % rotations];
                for (uint32_t role = 0; role < 3; ++role) {
                    const uint32_t index = tmpl.binding_indexes[task][role];
                    if (index == UINT32_MAX) continue;
                    const auto offset = static_cast<uint32_t>(bindings + (index + group) * sizeof(CalendarDispatch));
                    image.store(offset, CalendarDispatch{0, 0, 0, wait_offset == 0 ? 0 : wait_offset - bindings,
                                                         extra_offset - bindings});
                    const uint32_t shard_add_index = task_input.calendar_shard_producer ? group / rotations : UINT32_MAX;
                    image.dispatches.push_back({offset, global, shell, role, task_input.calendar_output_mask, shard_add_index});
                    if (graph != nullptr && task_input.active_mask != 0) patch.dispatch_offsets[role] = offset;
                }
            }
            if (graph != nullptr) image.store(patch_offset + global * sizeof(CalendarDispatchPatch), patch);
        }

        const uint32_t samples = input.sample ? image.allocate<CalendarTiming>(tmpl.header.row_count) : 0;
        if (input.sample && samples == 0) return false;
        image.sample_offsets.push_back(samples);

        RangeCalendarImage::PhaseEnd end{};
        if (shell >= 0) {
            end.task = static_cast<uint32_t>(shell);
        } else if (std::none_of(phase.tasks.begin(), phase.tasks.end(), [](const TaskInput &task) {
                       return task.calendar_output_mask != 0;
                   })) {
            if (const uint32_t sink = sole_sink(phase); sink != UINT32_MAX)
                end.task = physical_task(sink, phase, body_base);
        }
        if (p + 1 < inputs.size() && end.task == UINT32_MAX) {
            end.slot_offset = image.allocate<PhaseCounter>(1);
            if (end.slot_offset == 0) return false;
            ++slot_count;
        }

        uint32_t longest = 0;
        for (uint32_t worker = 0; worker < workers; ++worker) {
            const uint32_t first = tmpl.indexes[worker];
            const uint32_t last = tmpl.indexes[worker + 1];
            longest = std::max(longest, last - first);
            uint32_t need = 0;
            const auto required = [&](uint32_t row) {
                const uint32_t task = static_cast<uint32_t>(percounter_order_task_id(tmpl.rows[row].entry));
                return std::min(static_cast<uint32_t>(phase.tasks.size()),
                                (task / PERCOUNTER_GRAPH_SLICE_TASKS + 1) * PERCOUNTER_GRAPH_SLICE_TASKS);
            };
            for (uint32_t begin = first; begin < last;) {
                need = invocation == nullptr ? 0 : std::max(need, required(begin));
                uint32_t limit = begin + 1;
                while (limit < last && (invocation == nullptr || required(limit) <= need))
                    ++limit;
                const bool previous_phase = begin == first && p != 0;
                const uint32_t num_waits = previous_phase + (invocation != nullptr);
                const uint32_t wait_offset = image.allocate<CalendarWait>(num_waits);
                if (num_waits != 0 && wait_offset == 0) return false;
                if (previous_phase)
                    image.waits.push_back({wait_offset, RangeCalendarImage::WaitKind::PHASE_END, p - 1, 0});
                if (invocation != nullptr)
                    image.waits.push_back({
                        static_cast<uint32_t>(wait_offset + previous_phase * sizeof(CalendarWait)),
                        RangeCalendarImage::WaitKind::MATERIALIZED, static_cast<uint32_t>(shell), need
                    });
                const bool tail = limit == last && end.slot_offset != 0;
                if (tail) ++end.contributors;
                ranges[worker].push_back({
                    {static_cast<uint32_t>(tmpl.rows_offset + begin * sizeof(CalendarExecutionRow)),
                     limit - begin, bindings, wait_offset, num_waits, sequence_base, 0},
                    samples == 0 ? 0 : static_cast<uint32_t>(samples + begin * sizeof(CalendarTiming)),
                    tail ? static_cast<int32_t>(p) : -1
                });
                begin = limit;
            }
        }
        image.phase_ends.push_back(end);
        if (longest == 0 || longest > UINT32_MAX - sequence_base) return false;
        sequence_base += longest;
    }

    uint32_t total_ranges = 0;
    bool sampled = false;
    for (const auto &worker : ranges) {
        if (worker.size() > UINT32_MAX - total_ranges) return false;
        total_ranges += static_cast<uint32_t>(worker.size());
        sampled |= std::any_of(worker.begin(), worker.end(), [](const auto &range) { return range.timing != 0; });
    }
    const uint32_t worker_offset = image.allocate<uint32_t>(workers + 1);
    const uint32_t ranges_offset = image.allocate<CalendarRange>(total_ranges);
    const uint32_t sample_indexes = image.allocate<uint32_t>(sampled ? total_ranges : 0);
    if (image.reduce_shards) slot_count += DS_PARK_REDUCE_SHARDS;
    const uint32_t phase_slots = image.allocate<uint64_t>(slot_count);
    if (worker_offset == 0 || ranges_offset == 0 || (sampled && sample_indexes == 0) ||
        (slot_count != 0 && phase_slots == 0))
        return false;
    uint32_t index = 0;
    for (uint32_t worker = 0; worker < workers; ++worker) {
        image.store(worker_offset + worker * sizeof(uint32_t), index);
        for (const auto &record : ranges[worker]) {
            const auto offset = static_cast<uint32_t>(ranges_offset + index * sizeof(CalendarRange));
            image.store(offset, record.range);
            if (sampled) image.store(sample_indexes + index * sizeof(uint32_t), record.timing);
            if (record.end_phase >= 0) image.tails.push_back({offset, static_cast<uint32_t>(record.end_phase)});
            ++index;
        }
    }
    image.store(worker_offset + workers * sizeof(uint32_t), index);
    image.store(0, CalendarRangeHeader{
        workers, worker_offset, ranges_offset, total_ranges, patch_offset, sample_indexes, phase_slots, slot_count
    });
    *out = std::move(image);
    return true;
}

inline uint64_t range_add_command(uint64_t device, const PercounterCounter &counter, uint32_t id) {
    if (counter.completion_notify_address != 0)
        return a5_cnt_notify_increment_address(counter.completion_notify_address) | CALENDAR_ADD_NOTIFY;
    return device + uint64_t{id} * sizeof(PercounterCounter);
}

inline CalendarWait range_completion_wait(uint64_t device, const PercounterCounter &counter, uint32_t id) {
    if (counter.completion_notify_address != 0)
        return {counter.completion_notify_address | CALENDAR_WAIT_NOTIFY, counter.completion_notify_target};
    return {device + uint64_t{id} * sizeof(PercounterCounter), 1};
}

inline bool bind_range_calendar(
    RangeCalendarImage &image, uint64_t calendar_device, uint64_t base, const PercounterLayout &layout, void *host,
    const std::vector<uint64_t> &notifications, size_t first_phase_slot
) {
    auto *counters = region_at<PercounterCounter>(host, layout.counters_offset);
    const uint64_t device_counters = base + layout.counters_offset;
    const auto *metadata = region_at<PercounterTaskMetadata>(host, layout.metadata_offset);
    std::map<uint32_t, uint64_t> ready_address;
    if (layout.graph_invocation_count != 0) {
        auto *records = region_at<PercounterGraphInvocation>(host, layout.graph_invocations_offset);
        std::map<uint32_t, PercounterGraphInvocation> by_shell;
        for (uint32_t i = 0; i < layout.graph_invocation_count; ++i)
            by_shell.emplace(static_cast<uint32_t>(records[i].shell_task_id), records[i]);
        if (by_shell.size() != image.graph_shells.size()) return false;
        for (size_t i = 0; i < image.graph_shells.size(); ++i) {
            const uint32_t shell = image.graph_shells[i];
            const auto record = by_shell.find(shell);
            if (record == by_shell.end()) return false;
            records[i] = record->second;
            ready_address.emplace(shell, base + layout.graph_invocations_offset +
                                         i * sizeof(PercounterGraphInvocation) +
                                         offsetof(PercounterGraphInvocation, calendar_ready));
        }
    }
    const auto header = image.load<CalendarRangeHeader>(0);
    std::vector<CalendarWait> phase_waits;
    std::vector<uint64_t> phase_add;
    size_t slot = 0;
    for (const auto &end : image.phase_ends) {
        if (end.task != UINT32_MAX) {
            phase_waits.push_back(range_completion_wait(device_counters, counters[end.task], end.task));
            phase_add.push_back(0);
        } else if (end.slot_offset != 0) {
            const size_t index = first_phase_slot + slot;
            const uint64_t set = index < notifications.size() ? notifications[index] : 0;
            const uint64_t add = a5_cnt_notify_increment_address(set);
            image.store(header.phase_slots_offset + static_cast<uint32_t>(slot * sizeof(uint64_t)),
                        add == 0 ? uint64_t{0} : set);
            ++slot;
            phase_waits.push_back({
                add != 0 ? set | CALENDAR_WAIT_NOTIFY : (calendar_device + end.slot_offset) | CALENDAR_WAIT_GM_TARGET,
                end.contributors
            });
            phase_add.push_back(add != 0 ? add | CALENDAR_ADD_NOTIFY : calendar_device + end.slot_offset);
        }
    }
    std::array<uint64_t, DS_PARK_REDUCE_SHARDS> shard_set{}, shard_add{};
    if (image.reduce_shards) {
        for (uint32_t group = 0; group < DS_PARK_REDUCE_SHARDS; ++group, ++slot) {
            const size_t index = first_phase_slot + slot;
            if (index >= notifications.size()) return false;
            const uint64_t set = notifications[index];
            const uint64_t add = a5_cnt_notify_increment_address(set);
            if (add == 0) return false;
            shard_set[group] = set;
            shard_add[group] = add | CALENDAR_ADD_NOTIFY;
            image.store(header.phase_slots_offset + static_cast<uint32_t>(slot * sizeof(uint64_t)), set);
        }
    }
    if (slot != header.phase_slots_count) return false;

    for (const auto &patch : image.waits) {
        switch (patch.kind) {
        case RangeCalendarImage::WaitKind::COMPLETION:
            image.store(patch.offset, range_completion_wait(device_counters, counters[patch.index], patch.index));
            break;
        case RangeCalendarImage::WaitKind::DEFER: {
            auto wait = range_completion_wait(device_counters, counters[patch.index], patch.index);
            wait.read_cmd = (wait.read_cmd & ~CALENDAR_WAIT_TAG_MASK) | CALENDAR_WAIT_DEFER;
            wait.target = (uint64_t{patch.index} << 32) |
                (counters[patch.index].completion_notify_address == 0 ? 0 : wait.target);
            image.store(patch.offset, wait);
            break;
        }
        case RangeCalendarImage::WaitKind::PHASE_END:
            if (patch.index >= phase_waits.size()) return false;
            image.store(patch.offset, phase_waits[patch.index]);
            break;
        case RangeCalendarImage::WaitKind::REDUCE_SHARD:
            if (patch.index >= shard_set.size() || shard_set[patch.index] == 0) return false;
            image.store(patch.offset, CalendarWait{shard_set[patch.index] | CALENDAR_WAIT_NOTIFY, patch.target});
            break;
        case RangeCalendarImage::WaitKind::MATERIALIZED:
            if (const auto ready = ready_address.find(patch.index); ready != ready_address.end())
                image.store(patch.offset, CalendarWait{ready->second | CALENDAR_WAIT_GM_TARGET, patch.target});
            else
                return false;
            break;
        }
    }
    for (const auto &patch : image.dispatches) {
        auto record = image.load<CalendarDispatch>(patch.offset);
        const auto &counter = counters[patch.global_task];
        const bool output = patch.output_mask == 0 || counter.completion_notify_address == 0 ||
                            (patch.output_mask & (1U << patch.role)) != 0;
        if (patch.shard_add_index < shard_add.size())
            record.done_add_cmd = shard_add[patch.shard_add_index];
        else
            record.done_add_cmd = output ? range_add_command(device_counters, counter, patch.global_task) : 0;
        if (patch.shell < 0) {
            record.kernel_entry = counter.function_addresses[patch.role];
            record.args_address = counter.arguments_address;
        }
        image.store(patch.offset, record);
    }
    for (const auto &patch : image.extras) {
        auto extra = image.load<CalendarExecutionExtra>(patch.offset);
        extra.sync_index = metadata[patch.global_task].sync_index;
        if (patch.shell >= 0)
            extra.shell_add_cmd = range_add_command(device_counters, counters[patch.shell], patch.shell);
        image.store(patch.offset, extra);
    }
    for (const auto &tail : image.tails) {
        if (tail.phase >= phase_add.size()) return false;
        auto range = image.load<CalendarRange>(tail.range);
        range.tail_add_cmd = phase_add[tail.phase];
        image.store(tail.range, range);
    }
    return true;
}

}  // namespace simpler::percounter
