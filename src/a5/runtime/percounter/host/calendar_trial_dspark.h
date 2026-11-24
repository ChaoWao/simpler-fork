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

#include "calendar_phases.h"

namespace simpler::percounter {

struct DSparkSignalOrder {
    uint32_t notify;
    uint32_t poll;
    uint32_t first_wait;
    uint32_t ep_wait_count;
    bool blocks_worker;
};

inline bool dspark_has_ancestor(const std::vector<TaskInput> &tasks, uint32_t node, uint32_t ancestor) {
    std::vector<bool> visited(node + 1, false);
    std::vector<uint32_t> pending{node};
    while (!pending.empty()) {
        const auto task = pending.back();
        pending.pop_back();
        if (task == ancestor) return true;
        if (task < ancestor || visited[task]) continue;
        visited[task] = true;
        const auto &row = tasks[task];
        for (int32_t i = 0; i < row.fanin_count; ++i)
            pending.push_back(static_cast<uint32_t>(row.fanin_ids[i]));
    }
    return false;
}

inline bool dspark_trial_signal_order(
    const std::vector<TaskInput> &tasks, const std::vector<SchedulerTaskMetadata> &metadata,
    const GraphPlanInput &graphs, uint32_t workers, std::vector<DSparkSignalOrder> &out
) {
    if (workers != 96 || (graphs.invocation_count != 0 && graphs.invocation_count != 3)) return false;
    const bool mixed = graphs.invocation_count != 0;
    if (mixed && (graphs.template_count == 0 || graphs.template_count > 3)) return false;
    for (size_t i = 0; i < graphs.template_count; ++i) {
        const auto &body = graphs.templates[i];
        if (body.tasks.size() != 2 || body.has_predicates || body.fanin_offsets != std::vector<int32_t>{0, 0, 1} ||
            body.fanin_indices != std::vector<int32_t>{0})
            return false;
        const auto &cube = body.tasks[0].metadata;
        const auto &vector = body.tasks[1].metadata;
        if (cube.active_mask != 1 || cube.block_num != 8 || vector.active_mask != 2 || vector.block_num != 12 ||
            cube.sync_index != PERCOUNTER_NO_SYNC || vector.sync_index != PERCOUNTER_NO_SYNC)
            return false;
    }
    const int32_t ep_send = mixed ? 81 : 79;
    const int32_t draft_send = mixed ? 186 : 185;
    const int32_t tp_send = mixed ? 144 : 142;
    const auto vector_id = [&](uint32_t task) { return metadata[task].kernel_ids[1]; };
    const auto is_vector = [&](uint32_t id, int32_t kernel, int32_t blocks) {
        return id < tasks.size() && tasks[id].active_mask == 2 && !tasks[id].sync_start &&
               tasks[id].block_num == blocks && vector_id(id) == kernel;
    };

    std::vector<std::vector<uint32_t>> users(tasks.size());
    for (uint32_t id = 0; id < tasks.size(); ++id) {
        const auto &task = tasks[id];
        if (task.has_predicate || task.published_off_lane) return false;
        if (task.inline_completed) continue;
        for (int32_t i = 0; i < task.fanin_count; ++i) {
            const int32_t pred = task.fanin_ids[i];
            if (pred < 0 || static_cast<uint32_t>(pred) >= id) return false;
            users[pred].push_back(id);
        }
    }
    uint32_t ep_count = 0, draft_count = 0, tp_count = 0, consumers = 0;
    int32_t ep_width = 0, tp_width = 0;
    std::vector<DSparkSignalOrder> result;
    for (uint32_t id = 0; id < tasks.size(); ++id) {
        if (tasks[id].inline_completed || tasks[id].graph_invocation >= 0) continue;
        const int32_t kid = vector_id(id);
        if (kid == ep_send + 1 || kid == draft_send + 1 || kid == tp_send + 1) ++consumers;
        if (kid != ep_send && kid != draft_send && kid != tp_send) continue;
        const int32_t width = tasks[id].block_num;
        auto &group_width = kid == tp_send ? tp_width : ep_width;
        if ((width != 2 && width != 8) || (group_width != 0 && width != group_width) ||
            !is_vector(id, kid, width) || !is_vector(id + 1, kid + 1, 1))
            return false;
        group_width = width;
        if (!result.empty() && !dspark_has_ancestor(tasks, id, result.back().notify)) return false;
        if (kid == tp_send) {
            result.push_back({id, id + 1, 0, 0, true});
            ++tp_count;
            continue;
        }
        const auto &successor = users[id + 1];
        if (successor.size() != 1 || !dspark_has_ancestor(tasks, successor.front(), id)) return false;
        for (uint32_t source = 1; source < static_cast<uint32_t>(width); ++source) {
            const uint32_t wait = id + 1 + source;
            if (!is_vector(wait, kid + 1, 1) || users[wait] != successor) return false;
        }
        result.push_back({id, successor.front(), id + 1, static_cast<uint32_t>(width), false});
        if (kid == ep_send) ++ep_count;
        else ++draft_count;
    }
    if (ep_count != 80 || draft_count != 6 || tp_count != 4 || consumers != (ep_count + draft_count) * static_cast<uint32_t>(ep_width) + tp_count) return false;
    out = std::move(result);
    return true;
}

inline bool dspark_set_output_masks(
    std::vector<TaskInput> &tasks, const std::vector<SchedulerTaskMetadata> &metadata, bool mixed
) {
    const int32_t w2_cube = mixed ? 87 : 85;
    const int32_t w13_cube = mixed ? 84 : 82;
    std::vector<uint32_t> sole_user(tasks.size(), UINT32_MAX);
    constexpr uint32_t multiple_users = UINT32_MAX - 1U;
    for (uint32_t task = 0; task < tasks.size(); ++task) {
        const auto &input = tasks[task];
        if (input.inline_completed) continue;
        for (int32_t i = 0; i < input.fanin_count; ++i) {
            const auto producer = static_cast<uint32_t>(input.fanin_ids[i]);
            auto &user = sole_user[producer];
            user = user == UINT32_MAX || user == task ? task : multiple_users;
        }
    }

    uint32_t w2_count = 0;
    uint32_t w13_count = 0;
    for (uint32_t task = 0; task < tasks.size(); ++task) {
        auto &input = tasks[task];
        const auto &kernel = metadata[task].kernel_ids;
        if (input.inline_completed || (kernel[0] != w2_cube && kernel[0] != w13_cube)) continue;
        const bool w2 = kernel[0] == w2_cube;
        const int32_t vec = kernel[0] + 1;
        const auto user = sole_user[task];
        if (!input.is_mix || input.active_mask != 7 || input.block_num != 32 ||
            input.subtask_count != 3 || input.sync_start || input.calendar_defer_mask != 0 ||
            kernel[1] != vec || kernel[2] != vec || user >= tasks.size() ||
            tasks[user].active_mask != 2 || metadata[user].kernel_ids[1] != vec + 1)
            return false;
        input.calendar_output_mask = w2 ? 1U : 3U;
        if (w2) ++w2_count;
        else ++w13_count;
    }
    return w2_count == 40 && w13_count == 40;
}

inline bool dspark_set_reduce_shards(
    std::vector<TaskInput> &tasks, const std::vector<SchedulerTaskMetadata> &metadata, bool mixed
) {
    const int32_t reduce_kernel = mixed ? 90 : 88;
    std::vector<std::vector<uint32_t>> users(tasks.size());
    for (uint32_t task = 0; task < tasks.size(); ++task) {
        const auto &input = tasks[task];
        if (input.inline_completed) continue;
        for (int32_t i = 0; i < input.fanin_count; ++i) users[input.fanin_ids[i]].push_back(task);
    }

    uint32_t round = 0;
    uint32_t previous_consumer = 0;
    for (uint32_t producer = 0; producer < tasks.size(); ++producer) {
        auto &input = tasks[producer];
        if (input.inline_completed || metadata[producer].kernel_ids[1] != reduce_kernel) continue;
        const auto &targets = users[producer];
        if (targets.size() != 1 || input.block_num != 40 || input.sync_start || input.active_mask != 2 ||
            input.calendar_defer_mask != 0 || input.calendar_output_mask != 0)
            return false;
        const auto consumer = targets.front();
        auto &next = tasks[consumer];
        if (metadata[consumer].kernel_ids[1] != reduce_kernel + 1 || next.active_mask != 2 || next.block_num != 64 ||
            next.sync_start || next.calendar_shard_round != 0 ||
            (round != 0 && !dspark_has_ancestor(tasks, producer, previous_consumer)))
            return false;
        input.calendar_shard_round = ++round;
        input.calendar_shard_producer = true;
        next.calendar_shard_round = round;
        previous_consumer = consumer;
    }
    return round == 40;
}

// Preserve the original slot assignment for all unrelated completion chains.
inline bool dspark_remove_whole_reduce_credits(
    SchedulerCompletionCounterPlan &plan, const std::vector<TaskInput> &tasks
) {
    std::vector<uint32_t> removed(plan.chains.size(), 0);
    uint32_t reduced = 0;
    for (uint32_t id = 0; id < tasks.size(); ++id) {
        auto &chain = plan.task_chains[id];
        if (tasks[id].calendar_shard_producer) {
            if (chain >= plan.chains.size()) return false;
            removed[chain] += tasks[id].block_num;
            reduced += tasks[id].block_num;
            plan.thresholds[id] = 0;
            chain = UINT32_MAX;
        } else if (chain < plan.chains.size()) {
            if (plan.thresholds[id] <= removed[chain]) return false;
            plan.thresholds[id] -= removed[chain];
        }
    }
    for (size_t chain = 0; chain < plan.chains.size(); ++chain)
        plan.chains[chain].limit -= removed[chain];
    return reduced == 1600;
}

inline bool dspark_apply_signal_order(
    const std::vector<DSparkSignalOrder> &signals, size_t ordinary_tasks, std::vector<CalendarPhase> &phases
) {
    std::vector<int32_t> phase_of(ordinary_tasks, -1);
    std::vector<uint32_t> local(ordinary_tasks, 0);
    for (size_t p = 0; p < phases.size(); ++p) {
        auto &phase = phases[p];
        if (phase.invocation >= 0) continue;
        for (uint32_t n = 0; n < phase.task_ids.size(); ++n) {
            phase_of[phase.task_ids[n]] = p;
            local[phase.task_ids[n]] = n;
        }
    }
    for (const auto &signal : signals) {
        const int32_t p = phase_of[signal.notify];
        if (p < 0 || phase_of[signal.poll] != p) return false;
        auto &phase = phases[p];
        if (signal.blocks_worker) {
            // No rank waits for the peer before this rank has published the same round.
            auto &fanin = phase.fanins[local[signal.poll]];
            const auto producer = static_cast<int32_t>(local[signal.notify]);
            if (std::find(fanin.begin(), fanin.end(), producer) == fanin.end()) fanin.push_back(producer);
        } else {
            for (uint32_t source = 0; source < signal.ep_wait_count; ++source) {
                const uint32_t wait = signal.first_wait + source;
                if (phase_of[wait] != p) return false;
                phase.tasks[local[wait]].calendar_defer_mask = 2;
            }
        }
    }
    for (auto &phase : phases) {
        if (phase.invocation >= 0) continue;
        calendar_phase_detail::bind_fanins(phase);
        for (size_t id = 0; id < phase.tasks.size(); ++id) {
            const auto &task = phase.tasks[id];
            if (task.calendar_shard_round == 0 || task.calendar_shard_producer) continue;
            const auto &fanins = phase.fanins[id];
            if (std::none_of(fanins.begin(), fanins.end(), [&](int32_t parent) {
                    return calendar_uses_shard(phase.tasks[parent], task);
                }))
                return false;
        }
    }
    return true;
}

}  // namespace simpler::percounter
