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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "region_plan.h"
#include "scheduler/calendar_wire.h"

namespace simpler::percounter::range_calendar_detail {

struct CalendarWaitPlan {
    std::vector<std::vector<int32_t>> task_fanins;
    std::vector<bool> row_waits;
};

inline bool calendar_event_order(
    const std::vector<std::vector<size_t>> &parents, std::vector<size_t> &order, std::vector<size_t> &ranks
) {
    std::vector<std::vector<size_t>> successors(parents.size());
    std::vector<size_t> pending(parents.size());
    order.reserve(parents.size());
    for (size_t node = 0; node < parents.size(); ++node) {
        pending[node] = parents[node].size();
        if (pending[node] == 0) order.push_back(node);
        for (const size_t parent : parents[node])
            successors[parent].push_back(node);
    }
    ranks.resize(parents.size());
    for (size_t i = 0; i < order.size(); ++i) {
        const size_t node = order[i];
        ranks[node] = i;
        for (const size_t successor : successors[node])
            if (--pending[successor] == 0) order.push_back(successor);
    }
    return order.size() == parents.size();
}

inline bool plan_calendar_waits(
    const std::vector<TaskInput> &tasks, const std::vector<CalendarRow> &rows, const std::vector<uint32_t> &indexes,
    size_t join_count, CalendarWaitPlan &result
) {
    const size_t row_count = rows.size();
    const size_t gate_base = row_count;
    const size_t done_base = gate_base + row_count + join_count;
    const size_t task_count = tasks.size();

    const size_t cohort_base = done_base + task_count;
    std::vector<std::vector<size_t>> parents(cohort_base + task_count);
    const size_t worker_count = indexes.size() - 1;
    const size_t cluster_count = worker_count / 3;
    std::vector<uint32_t> row_workers(row_count);
    std::vector<std::vector<uint32_t>> last_rows(task_count);
    std::vector<size_t> gate_tasks(row_count + join_count, task_count);
    const auto gate_of = [&](size_t row) -> size_t {
        return gate_base + (rows[row].local_join == CALENDAR_NO_JOIN ? row : row_count + rows[row].local_join);
    };
    for (size_t worker = 0; worker + 1 < indexes.size(); ++worker) {
        const size_t begin = indexes[worker];
        for (size_t row = begin; row < indexes[worker + 1]; ++row) {
            const auto id = percounter_order_task_id(rows[row].entry);
            if (id < 0 || static_cast<size_t>(id) >= task_count ||
                (rows[row].local_join != CALENDAR_NO_JOIN && rows[row].local_join >= join_count))
                return false;
            const auto task = static_cast<size_t>(id);
            const size_t gate = gate_of(row);
            auto &owner = gate_tasks[gate - gate_base];
            if (owner != task_count && owner != task) return false;
            owner = task;
            row_workers[row] = static_cast<uint32_t>(worker);
            const auto output = tasks[task].calendar_output_mask;
            const auto lane = worker < cluster_count ? 0 : 1 + (worker - cluster_count) % 2;
            if (output == 0 || (output & (1U << lane)) != 0) {
                auto &last = last_rows[task];
                if (!last.empty() && row_workers[last.back()] == worker) {
                    last.back() = static_cast<uint32_t>(row);
                } else {
                    last.push_back(static_cast<uint32_t>(row));
                }
                parents[done_base + task].push_back(row);
            }
            parents[row].push_back(gate);
            if (row != begin) {
                const size_t previous = row - 1;
                parents[gate].push_back(previous);
                if (tasks[task].sync_start) parents[cohort_base + task].push_back(previous);
            }
        }
    }

    for (size_t gate = gate_base; gate < done_base; ++gate) {
        const size_t task = gate_tasks[gate - gate_base];
        if (task == task_count) continue;
        const auto &input = tasks[task];
        if (input.sync_start) parents[gate].push_back(cohort_base + task);
        for (int32_t edge = 0; edge < input.fanin_count; ++edge) {
            const auto pred = static_cast<size_t>(input.fanin_ids[edge]);
            if (!tasks[pred].inline_completed) parents[gate].push_back(done_base + pred);
        }
    }
    result.task_fanins.resize(task_count);
    result.row_waits.resize(row_count);
    constexpr size_t max_prefix_bytes = 64 * 1024 * 1024;
    if (parents.size() > max_prefix_bytes / sizeof(uint32_t) / worker_count) {
        for (size_t task = 0; task < task_count; ++task) {
            const auto &input = tasks[task];
            for (int32_t i = 0; i < input.fanin_count; ++i) {
                const auto &producer = tasks[input.fanin_ids[i]];
                if (!producer.inline_completed && !calendar_uses_shard(producer, input))
                    result.task_fanins[task].push_back(input.fanin_ids[i]);
            }
        }
        for (size_t row = 0; row < row_count; ++row)
            result.row_waits[row] = !result.task_fanins[gate_tasks[gate_of(row) - gate_base]].empty();
        return true;
    }

    std::vector<size_t> event_order, ranks;
    if (!calendar_event_order(parents, event_order, ranks)) return false;
    std::vector<std::vector<size_t>> ordered_inputs(task_count);
    std::vector<std::vector<bool>> kept(task_count);
    for (size_t task = 0; task < task_count; ++task) {
        const auto &input = tasks[task];
        auto &order = ordered_inputs[task];
        order.resize(input.fanin_count);
        kept[task].resize(input.fanin_count);
        std::iota(order.begin(), order.end(), size_t{0});
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return ranks[done_base + input.fanin_ids[a]] > ranks[done_base + input.fanin_ids[b]];
        });
    }

    // An event's prefix contains the latest row that it orders on every
    // worker. Earlier rows on that worker are part of the same prefix.
    std::vector<uint32_t> prefixes(parents.size() * worker_count, 0);
    const auto prefix = [&](size_t node) {
        return prefixes.data() + node * worker_count;
    };
    const auto merge = [&](uint32_t *dst, size_t node) {
        const uint32_t *src = prefix(node);
        for (size_t worker = 0; worker < worker_count; ++worker)
            dst[worker] = std::max(dst[worker], src[worker]);
    };

    std::vector<bool> gate_waits(gate_tasks.size(), false);
    for (const size_t node : event_order) {
        auto *ordered = prefix(node);
        if (node < gate_base || node >= done_base) {
            for (const size_t parent : parents[node])
                merge(ordered, parent);
            if (node < gate_base) ordered[row_workers[node]] = static_cast<uint32_t>(node + 1);
            continue;
        }

        const size_t id = gate_tasks[node - gate_base];
        if (id == task_count) continue;
        // START requires every participating worker's previous row to
        // retire before any block enters its kernel. Its resource prefix
        // therefore already covers any input completed in that prefix.
        for (const size_t parent : parents[node]) {
            if (parent < gate_base || parent >= cohort_base) merge(ordered, parent);
        }
        for (const size_t edge : ordered_inputs[id]) {
            const auto pred = static_cast<size_t>(tasks[id].fanin_ids[edge]);
            // The shard check proves only part of the producer's output.
            // No whole-producer worker prefix follows from that check.
            if (tasks[pred].inline_completed || calendar_uses_shard(tasks[pred], tasks[id])) continue;
            const auto &required = last_rows[pred];
            // A deferred row can retire before its completion is published.
            if (tasks[pred].calendar_defer_mask == 0 && !required.empty() &&
                std::all_of(required.begin(), required.end(), [&](uint32_t row) {
                    return ordered[row_workers[row]] >= row + 1;
                }))
                continue;
            kept[id][edge] = true;
            gate_waits[node - gate_base] = true;
            merge(ordered, done_base + pred);
        }
    }

    for (size_t task = 0; task < task_count; ++task) {
        for (size_t edge = 0; edge < kept[task].size(); ++edge) {
            if (kept[task][edge]) result.task_fanins[task].push_back(tasks[task].fanin_ids[edge]);
        }
    }
    for (size_t row = 0; row < row_count; ++row)
        result.row_waits[row] = gate_waits[gate_of(row) - gate_base];
    return true;
}

}  // namespace simpler::percounter::range_calendar_detail
