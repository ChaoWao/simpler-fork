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

#include "inner_kernel.h"
#undef SPIN_WAIT_HINT
#include "runtime_types.h"

uint64_t get_aicore_report_epoch();
#include "lane_loop.h"
#include "graph_template.h"
#include "scheduler/argument_metadata.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace {
using namespace simpler::percounter;
alignas(64) std::array<uint8_t, 4096> registers{};
std::function<void(int64_t *)> execute;
std::function<void()> on_poll;
void kernel(int64_t *args) { execute(args); }

TaskInput task(uint32_t mask, int blocks = 1, bool sync = false) {
    TaskInput result{};
    result.active_mask = mask;
    result.is_mix = __builtin_popcount(mask) > 1;
    result.core_type_index = mask == 1 ? 0 : 1;
    result.subtask_count = __builtin_popcount(mask);
    result.block_num = blocks;
    result.sync_start = sync;
    return result;
}

// Uses the same region offsets and physical worker topology as run_percounter_lane.
struct Fixture {
    std::vector<TaskInput> inputs;
    std::vector<ChipTaskStorage> storage;
    std::vector<uint64_t> scalars;
    std::vector<uint8_t> region;
    PercounterLayout layout{};
    PercounterControl *control{};
    PercounterCounter *counters{};
    std::array<uint64_t, 2> callable{0, reinterpret_cast<uint64_t>(&kernel)};
    std::vector<DispatchPayload *> payloads;
    std::vector<PercounterClusterSchedulerRegion> signals;
    std::vector<LaneConfig> cfg;
    std::vector<std::unique_ptr<PercounterLaneState>> state;
    std::vector<std::array<int64_t, 3>> calls;
    int running_lane{-1};

    explicit Fixture(
        std::vector<TaskInput> in = {task(1), task(1), task(1)}, uint32_t clusters = 2, const GraphPlanInput &graph = {}
    ) :
        inputs(std::move(in)),
        storage(inputs.size()),
        scalars(inputs.size()),
        payloads(clusters * 3),
        signals(clusters),
        cfg(clusters * 3) {
        registers.fill(0);
        on_poll = {};
        uint64_t counts[2]{}, mix = 0, boundaries = 0, sync = 0, blocks = 0, edges = 0, executions = 0;
        uint64_t body_tasks = 0, template_bytes = 0;
        for (size_t t = 0; t < graph.template_count; ++t)
            template_bytes += graph_template_bytes(graph.templates[t]);
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto &t = inputs[i];
            blocks += t.block_num;
            edges += t.fanin_count;
            if (t.graph_invocation >= 0) {
                const auto &body = graph.templates[graph.invocations[t.graph_invocation].template_index];
                body_tasks += body.tasks.size();
                blocks += body.executable_blocks;
                for (uint32_t c = 0; c < 2; ++c)
                    counts[c] += body.orders[c].size();
                mix += body.mix_task_count;
                boundaries += body.boundary_task_count;
                sync += body.sync_cohort_count;
                for (const auto &body_task : body.tasks)
                    executions += body_task.subtask_total;
            } else if (!t.inline_completed && !t.published_off_lane) {
                ++counts[t.is_mix ? 1 : t.core_type_index];
                mix += t.is_mix;
                boundaries += t.block_num > 1 || t.is_mix;
                sync += t.sync_start;
                executions += uint64_t{static_cast<uint32_t>(t.block_num)} * t.subtask_count;
            }
            scalars[i] = i;
            storage[i].task.task_id = TaskId::make_global(i);
            storage[i].payload.scalar_count = 1;
            storage[i].payload.scalars.set(&scalars[i]);
            for (uint32_t lane = 0; lane < 3; ++lane)
                storage[i].task.kernel_id[lane] = (t.active_mask & (1U << lane)) ? 1 : -1;
        }
        const GraphLayoutInput sizes{body_tasks, graph.invocation_count, template_bytes};
        EXPECT_TRUE(
            plan_layout(inputs.size(), blocks, edges, counts, &layout, mix, boundaries, sync, sizes, executions)
        );
        region.resize(layout.total_size + 127);
        auto *base = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(region.data()) + 127) & ~uintptr_t{127});
        ExternalAddresses external{
            reinterpret_cast<uint64_t>(storage.data()), storage.size(), reinterpret_cast<uint64_t>(callable.data()),
            callable.size()
        };
        BuildResult result{};
        EXPECT_TRUE(build_tables(
            inputs.data(), inputs.size(), layout, base, reinterpret_cast<uint64_t>(base), clusters, external, &result,
            graph
        )) << build_status_name(result.status);
        control = reinterpret_cast<PercounterControl *>(base);
        control->cluster_count = clusters;
        control->aiv_per_cluster = 2;
        counters = region_at<PercounterCounter>(base, layout.counters_offset);
        for (size_t i = 0; i < inputs.size(); ++i)
            if (inputs[i].graph_invocation < 0) EXPECT_EQ(publish_arguments(i), SchedulerGraphResult::OK);
        for (uint32_t i = 0; i < cfg.size(); ++i) {
            auto &c = cfg[i];
            const uint32_t cluster = i / 3, lane = i % 3;
            const uint32_t worker = lane == 0 ? cluster : clusters + cluster * 2 + lane - 1;
            c.control = control;
            c.storage_address = external.graph_storage;
            c.payloads = scheduler_state_at<DispatchPayload>(
                control, control->dispatch_payloads_offset +
                             uint64_t{worker} * PERCOUNTER_PAYLOAD_SLOT_COUNT * sizeof(DispatchPayload)
            );
            payloads[i] = c.payloads;
            for (uint32_t slot = 0; slot < PERCOUNTER_PAYLOAD_SLOT_COUNT; ++slot) {
                auto &p = c.payloads[slot];
                p.args[PAYLOAD_LOCAL_CONTEXT_INDEX] = reinterpret_cast<uint64_t>(&p.local_context);
                p.args[PAYLOAD_GLOBAL_CONTEXT_INDEX] = reinterpret_cast<uint64_t>(&p.global_context);
            }
            c.task_count = control->task_count;
            c.table_task_count = control->table_task_count;
            c.has_predicates = control->has_predicates != 0;
            c.fanin = region_at<PercounterFanin>(base, layout.fanin_offset);
            c.fanin_addresses = region_at<uint64_t>(base, layout.fanin_addr_offset);
            c.counters = counters;
            c.completion_sectors = region_at<PercounterCompletionSector>(base, layout.completion_sectors_offset);
            c.tickets = region_at<PercounterTicket>(base, layout.tickets_offset);
            c.metadata = region_at<PercounterTaskMetadata>(base, layout.metadata_offset);
            c.metadata_ready = region_at<uint64_t>(base, layout.metadata_ready_offset);
            c.task_claims = region_at<PercounterTaskClaim>(base, layout.task_claims_offset);
            c.resource_nodes = region_at<PercounterResourceNode>(base, layout.resource_nodes_offset);
            c.ordinary_pieces = region_at<PercounterOrdinaryPiece>(base, layout.ordinary_pieces_offset);
            c.projection_task_map = region_at<uint32_t>(base, layout.projection_task_map_offset);
            c.sync_boundaries = region_at<PercounterSyncBoundary>(base, layout.sync_boundaries_offset);
            for (uint32_t catalog = 0; catalog < 2; ++catalog)
                c.projection_catalog[catalog] = region_at<uint32_t>(base, layout.projection_catalog_offset[catalog]);
            c.cluster_index = cluster;
            c.cluster_lane = lane;
            c.cluster_ready = region_at<uint64_t>(base, layout.cluster_ready_offset) + cluster;
            c.cluster_owner = region_at<PercounterClusterOwnerSector>(base, layout.cluster_owners_offset) + cluster;
            c.ssbuf = &signals[cluster];
            c.async_group = percounter_init_async_record(control, worker);
            c.async_entries = scheduler_state_at<DeferredCompletionEntry>(
                control, control->async_entries_offset +
                             uint64_t{worker} * PERCOUNTER_ASYNC_CAPACITY * sizeof(DeferredCompletionEntry)
            );
            c.async_completions = region_at<PercounterDeferredGroup>(base, layout.async_completions_offset);
            c.async_completion_capacity = control->async_completion_capacity;
            state.push_back(std::make_unique<PercounterLaneState>(c));
        }
        execute = [this](int64_t *args) {
            const auto *local = reinterpret_cast<LocalContext *>(args[PAYLOAD_LOCAL_CONTEXT_INDEX]);
            calls.push_back({args[0], local->block_idx, running_lane});
        };
    }
    SchedulerGraphResult publish_arguments(size_t id) {
        const auto &payload = storage[id].payload;
        uint64_t functions[3]{};
        for (uint32_t lane = 0; lane < 3; ++lane) {
            const auto index = storage[id].task.kernel_id[lane];
            if (index >= 0 && static_cast<size_t>(index) < callable.size()) functions[lane] = callable[index];
        }
        return percounter_prepare_argument_metadata(
            counters[id], functions, inputs[id].active_mask, reinterpret_cast<uint64_t>(payload.tensor_data()),
            reinterpret_cast<uint64_t>(payload.scalar_data()), payload.tensor_count, payload.scalar_count
        );
    }
    uint64_t &component(size_t id, uint32_t sector) const {
        if (inputs[id].inline_completed || inputs[id].published_off_lane || inputs[id].block_num == 1)
            return counters[id].value;
        return cfg[0]
            .completion_sectors
                [id * PERCOUNTER_COMPLETION_SECTOR_COUNT + (sector & (PERCOUNTER_COMPLETION_SECTOR_COUNT - 1))]
            .value;
    }
    bool completed(size_t id) const {
        for (uint32_t sector = 0; sector < PERCOUNTER_COMPLETION_SECTOR_COUNT; ++sector)
            if (!percounter_counter_published(component(id, sector))) return false;
        return true;
    }
    void expect_completed(size_t id) const { EXPECT_TRUE(completed(id)) << id; }
    PercounterTask resolve(int id) {
        PercounterTask result{};
        EXPECT_TRUE(percounter_resolve(cfg[0], id, &result));
        return result;
    }
    bool run(int lane, const PercounterTask &task, DispatchPayload *payload) {
        bool skipped = false;
        return percounter_run_prepared(
            cfg[lane], task, payload,
            [] {
                return true;
            },
            &skipped
        );
    }
    bool raw_step(uint32_t lane) {
        running_lane = lane;
        bool executed = false;
        return percounter_step(cfg[lane], state[lane].get(), &executed);
    }
    void step(uint32_t lane) {
        ASSERT_TRUE(raw_step(lane));
        ASSERT_EQ(control->lane_error, 0U);
    }
    void rounds(uint32_t count) {
        for (uint32_t round = 0; round < count; ++round)
            for (uint32_t lane = 0; lane < cfg.size(); ++lane)
                step(lane);
    }
    template <typename Predicate>
    void drain_until(Predicate done, const char *expected) {
        for (uint32_t round = 0; round < 10000; ++round) {
            for (uint32_t lane = 0; lane < cfg.size(); ++lane)
                step(lane);
            if (done()) return;
            if (control->lane_error != 0) break;
        }
        FAIL() << "protocol did not reach " << expected << "; error=" << control->lane_error
               << " calls=" << calls.size() << " defer=" << control->async_registered << ':' << control->async_settled;
    }
    bool sinks_complete() const {
        const auto *sinks = scheduler_state_at<uint64_t>(control, control->sinks_offset);
        for (uint64_t i = 0; i < control->sink_count; ++i) {
            const uint64_t id = sinks[i] >> 1;
            if ((sinks[i] & PERCOUNTER_COMPLETION_PARTITIONED) == 0) {
                if (!percounter_counter_published(counters[id].value)) return false;
            } else {
                for (uint32_t sector = 0; sector < PERCOUNTER_COMPLETION_SECTOR_COUNT; ++sector)
                    if (!percounter_counter_published(
                            cfg[0].completion_sectors[id * PERCOUNTER_COMPLETION_SECTOR_COUNT + sector].value
                        ))
                        return false;
            }
        }
        return true;
    }
    void drain() {
        drain_until(
            [this] {
                return std::all_of(state.begin(), state.end(), [](const auto &s) {
                    return s->done();
                });
            },
            "all workers retired"
        );
        EXPECT_TRUE(sinks_complete());
        EXPECT_EQ(control->async_registered, control->async_settled);
    }
    void defer(uint32_t lane, int32_t *signal) {
        cfg[lane].async_entries[0] = {
            reinterpret_cast<uint64_t>(signal), 0, 1, COMPLETION_ENGINE_SDMA, COMPLETION_TYPE_COUNTER, 0
        };
        cfg[lane].async_group->count = 1;
    }
};

TEST(PercounterResourceOrder, NarrowReadySyncUsesCapacityWhileWiderSyncWaits) {
    std::vector<ResourceOrderTask> tasks(3);
    for (auto &node : tasks)
        node.active_mask = 2;
    tasks[0].blocks = 2;
    tasks[1].blocks = 3;
    tasks[1].sync = true;
    tasks[2].blocks = 2;
    tasks[2].sync = true;
    const auto plan = make_resource_order(tasks, 2);
    ASSERT_TRUE(plan.has_value());
    const std::vector<uint32_t> order{0, 2, 1};
    EXPECT_EQ(plan->tasks, order);
    EXPECT_EQ(plan->first_start[2], 0U);
    EXPECT_EQ(plan->first_start[1], 1U);
    EXPECT_EQ(plan->duration, 2U);
}

TEST(PercounterResourceOrder, MixUsesOnlyItsActualParticipants) {
    for (uint8_t mix : {uint8_t{5}, uint8_t{6}}) {
        std::vector<ResourceOrderTask> tasks(2);
        tasks[0].active_mask = mix == 5 ? 2 : 1;
        tasks[1].active_mask = mix;
        const auto plan = make_resource_order(tasks, 1);
        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan->first_start[0], 0U);
        EXPECT_EQ(plan->first_start[1], 0U);
        EXPECT_EQ(plan->duration, 1U);
    }
}

TEST(PercounterResourceOrder, GraphEnvelopeReservesUsedTypesAndReleasesSuccessors) {
    std::vector<ResourceOrderTask> tasks(3);
    tasks[0].active_mask = 1;
    tasks[1].active_mask = 2;
    tasks[1].reserve_types = true;
    tasks[1].duration = tasks[1].depth = 3;
    tasks[2].active_mask = 1;
    tasks[2].predecessors = {1};
    const auto vector_body = make_resource_order(tasks, 2);
    ASSERT_TRUE(vector_body.has_value());
    EXPECT_EQ(vector_body->first_start[0], 0U);
    EXPECT_EQ(vector_body->first_start[1], 0U);
    EXPECT_EQ(vector_body->first_start[2], 3U);
    EXPECT_EQ(vector_body->duration, 4U);
    tasks[1].active_mask = 3;
    const auto mixed_body = make_resource_order(tasks, 2);
    ASSERT_TRUE(mixed_body.has_value());
    EXPECT_EQ(mixed_body->first_start[0], 3U);
    EXPECT_EQ(mixed_body->first_start[1], 0U);
    EXPECT_EQ(mixed_body->first_start[2], 3U);
    EXPECT_EQ(mixed_body->duration, 4U);
}

TEST(PercounterResourceOrder, DeclaredTopologyControlsVirtualCapacity) {
    std::vector<ResourceOrderTask> tasks(2);
    tasks[0].active_mask = tasks[1].active_mask = 1;
    tasks[0].blocks = 2;
    for (uint32_t clusters = 1; clusters <= 3; ++clusters) {
        const auto plan = make_resource_order(tasks, clusters);
        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan->first_start[1], 3U - clusters);
    }
}

TEST(PercounterResourceOrder, RejectsInvalidTopologyEdgesAndTimeOverflow) {
    std::vector<ResourceOrderTask> tasks(2);
    tasks[0].active_mask = tasks[1].active_mask = 1;
    EXPECT_FALSE(make_resource_order(tasks, 0).has_value());
    tasks[0].predecessors = {1};
    EXPECT_FALSE(make_resource_order(tasks, 2).has_value());
    tasks[0].predecessors.clear();
    tasks[0].sync = true;
    tasks[0].blocks = 3;
    EXPECT_FALSE(make_resource_order(tasks, 2).has_value());
    tasks[0].sync = false;
    tasks[0].blocks = 1;
    tasks[0].reserve_types = true;
    tasks[0].duration = UINT64_MAX;
    tasks[1].predecessors = {0};
    EXPECT_FALSE(make_resource_order(tasks, 2).has_value());
}

TEST(PercounterResourceOrder, EverySmallDagKeepsEdgesAndWholeSyncStarts) {
    const std::array<uint8_t, 6> masks{{1, 2, 3, 5, 6, 7}};
    for (uint32_t clusters : {1U, 2U}) {
        for (uint32_t graph = 0; graph < 64; ++graph) {
            for (uint32_t shape = 0; shape < masks.size(); ++shape) {
                std::vector<ResourceOrderTask> tasks(4);
                uint32_t edge = 0;
                for (uint32_t id = 0; id < tasks.size(); ++id) {
                    tasks[id].active_mask = masks[(shape + id) % masks.size()];
                    tasks[id].blocks = id % 2 + 1;
                    tasks[id].sync = clusters == 2 && id % 2 != 0;
                    for (uint32_t producer = 0; producer < id; ++producer, ++edge)
                        if (graph & (1U << edge)) tasks[id].predecessors.push_back(producer);
                }
                const auto plan = make_resource_order(tasks, clusters);
                ASSERT_TRUE(plan.has_value());
                ASSERT_EQ(plan->tasks.size(), tasks.size());
                std::array<uint32_t, 4> rank{};
                uint32_t seen = 0;
                for (uint32_t position = 0; position < plan->tasks.size(); ++position) {
                    ASSERT_LT(plan->tasks[position], rank.size());
                    ASSERT_EQ(seen & (1U << plan->tasks[position]), 0U);
                    seen |= 1U << plan->tasks[position];
                    rank[plan->tasks[position]] = position;
                }
                EXPECT_EQ(seen, 15U);
                for (uint32_t id = 0; id < tasks.size(); ++id) {
                    EXPECT_GT(plan->finish[id], plan->first_start[id]);
                    if (tasks[id].sync) EXPECT_EQ(plan->finish[id], plan->first_start[id] + 1);
                    for (uint32_t producer : tasks[id].predecessors) {
                        EXPECT_LT(rank[producer], rank[id]);
                        EXPECT_LE(plan->finish[producer], plan->first_start[id]);
                    }
                }
            }
        }
    }
}

}  // namespace

volatile uint8_t *sim_get_reg_base() {
    if (on_poll) on_poll();
    return registers.data();
}
uint32_t sim_get_physical_core_id() { return 0; }
uint64_t platform_ssbuf_hardware_base_address() { return 0; }
uint32_t get_aicore_profiling_flag() { return 0; }
ChipSwimlaneActiveHead *get_chip_swimlane_aicore_head() { return nullptr; }

TEST(PercounterScalarStorage, PreservesMixedIntervalsWithoutLogicalCount) {
    std::array<uint64_t, 8> slots{};
    PercounterScalarTrace trace{};
    trace.records = slots.data();
    trace.depth = 2;
    trace.append_memory(ProfilePhase::SsbufLoad, 100, 117);
    trace.write({200, uint64_t{UINT32_MAX} + 300, 42, static_cast<uint32_t>(ProfilePhase::Prepare), 7});
    trace.append_memory(ProfilePhase::DsbAll, 500, 500);

    uint32_t cursor = 0;
    SchedulerScalarRecord record{};
    ASSERT_TRUE(
        percounter_decode_scalar_storage(slots.data(), trace.storage_words, cursor, trace.window_start, record)
    );
    EXPECT_EQ(record.start_time, 100);
    EXPECT_EQ(record.end_time, 117);
    EXPECT_EQ(record.task_ref, -1);
    EXPECT_EQ(record.count, 1);
    EXPECT_EQ(record.kind, static_cast<uint32_t>(ProfilePhase::SsbufLoad) | (3U << 16));
    ASSERT_TRUE(
        percounter_decode_scalar_storage(slots.data(), trace.storage_words, cursor, trace.window_start, record)
    );
    EXPECT_EQ(record.start_time, 200);
    EXPECT_EQ(record.end_time, uint64_t{UINT32_MAX} + 300);
    EXPECT_EQ(record.task_ref, 42);
    EXPECT_EQ(record.count, 7);
    ASSERT_TRUE(
        percounter_decode_scalar_storage(slots.data(), trace.storage_words, cursor, trace.window_start, record)
    );
    EXPECT_EQ(record.start_time, 500);
    EXPECT_EQ(record.end_time, 500);
    EXPECT_EQ(cursor, trace.storage_words);
    EXPECT_EQ(trace.dropped, 0);
}

TEST(PercounterScalarStorage, RejectsWholeExtendedRecordAtCapacityBoundary) {
    constexpr uint32_t capacity = PERCOUNTER_SCALAR_WORD_CAPACITY;
    std::vector<uint64_t> slots(capacity + 1);
    slots.back() = 0xfeed;
    PercounterScalarTrace trace{};
    trace.records = slots.data();
    trace.storage_words = capacity - 1;
    trace.write({100, 200, 42, static_cast<uint32_t>(ProfilePhase::Prepare), 1});
    EXPECT_EQ(trace.storage_words, capacity - 1);
    EXPECT_EQ(trace.dropped, 1);
    EXPECT_EQ(static_cast<uint32_t>(slots[capacity - 1]), 0);
    trace.append_memory(ProfilePhase::SsbufLoad, 300, 400);
    EXPECT_EQ(trace.storage_words, capacity);
    EXPECT_EQ(static_cast<uint32_t>(slots[capacity - 1]), 300);
    trace.append_memory(ProfilePhase::SsbufLoad, 500, 600);
    EXPECT_EQ(trace.storage_words, capacity);
    EXPECT_EQ(trace.dropped, 2);
    EXPECT_EQ(static_cast<uint32_t>(slots[capacity - 1]), 300);
    EXPECT_EQ(slots.back(), 0xfeed);
}

TEST(PercounterScalarStorage, InvalidIntervalsNeverConsumeStorage) {
    std::array<uint64_t, 4> slots{};
    PercounterScalarTrace trace{};
    trace.records = slots.data();
    trace.append_memory(ProfilePhase::SsbufLoad, 200, 100);
    EXPECT_EQ(trace.storage_words, 0);
    EXPECT_EQ(trace.dropped, 1);
    trace.dropped = UINT32_MAX;
    trace.append_memory(ProfilePhase::SsbufLoad, 200, 100);
    EXPECT_EQ(trace.dropped, UINT32_MAX);
}

TEST(PercounterScalarStorage, CompactWordsPreserveTicksAndUseFullWidthForLongIntervals) {
    std::array<uint64_t, 24> words{};
    PercounterScalarTrace trace{};
    trace.records = words.data();
    trace.window_start = 1000;
    trace.depth = 2;
    trace.append_memory(ProfilePhase::SsbufLoad, 1007, 1012);
    EXPECT_EQ(words[0], UINT64_C(0x031b000500000007));
    trace.append_memory(ProfilePhase::SsbufLoad, 2000, 2000 + uint64_t{UINT16_MAX} + 1);
    trace.append_memory(ProfilePhase::SsbufLoad, uint64_t{UINT32_MAX} + 1001, uint64_t{UINT32_MAX} + 1002);
    trace.depth = 127;
    trace.append_memory(ProfilePhase::SsbufLoad, 3000, 3010);
    EXPECT_EQ(trace.storage_words, 16);
    uint32_t cursor = 0;
    SchedulerScalarRecord record{};
    ASSERT_TRUE(percounter_decode_scalar_storage(words.data(), trace.storage_words, cursor, 1000, record));
    EXPECT_EQ(record.start_time, 1007);
    EXPECT_EQ(record.end_time, 1012);
    EXPECT_EQ(record.kind >> 16, 3);
    ASSERT_TRUE(percounter_decode_scalar_storage(words.data(), trace.storage_words, cursor, 1000, record));
    EXPECT_EQ(record.end_time, 2000 + uint64_t{UINT16_MAX} + 1);
    ASSERT_TRUE(percounter_decode_scalar_storage(words.data(), trace.storage_words, cursor, 1000, record));
    EXPECT_EQ(record.start_time, uint64_t{UINT32_MAX} + 1001);
    ASSERT_TRUE(percounter_decode_scalar_storage(words.data(), trace.storage_words, cursor, 1000, record));
    EXPECT_EQ(record.kind >> 16, 128);
    EXPECT_EQ(cursor, trace.storage_words);
}

TEST(PercounterScalarStorage, RejectsPartialExtendedRowsAndOverflowingAbsoluteTime) {
    const std::array<uint64_t, 4> partial{PERCOUNTER_SCALAR_EXTENDED, 1, 2, 3};
    uint32_t cursor = 0;
    SchedulerScalarRecord record{};
    EXPECT_FALSE(percounter_decode_scalar_storage(partial.data(), partial.size(), cursor, 0, record));
    const uint64_t offset = 1;
    cursor = 0;
    EXPECT_FALSE(percounter_decode_scalar_storage(&offset, 1, cursor, UINT64_MAX, record));
    const uint64_t duration = UINT64_C(1) << 32;
    cursor = 0;
    EXPECT_FALSE(percounter_decode_scalar_storage(&duration, 1, cursor, UINT64_MAX, record));
}

TEST(PercounterPayload, PreparationBindsCurrentImageWithoutReplacingContextPointers) {
    Fixture f;
    auto task = f.resolve(0);
    auto &payload = f.payloads[0][0];
    const auto local = payload.args[PAYLOAD_LOCAL_CONTEXT_INDEX];
    const auto global = payload.args[PAYLOAD_GLOBAL_CONTEXT_INDEX];
    ASSERT_TRUE(percounter_prepare_subtask(f.cfg[0], task.id, 0, 3, 5, &payload));
    EXPECT_EQ(payload.function_bin_addr, reinterpret_cast<uint64_t>(&kernel));
    EXPECT_EQ(payload.args[0], f.scalars[0]);
    EXPECT_EQ(payload.args[PAYLOAD_LOCAL_CONTEXT_INDEX], local);
    EXPECT_EQ(payload.args[PAYLOAD_GLOBAL_CONTEXT_INDEX], global);
    EXPECT_EQ(payload.local_context.block_idx, 3);
    EXPECT_EQ(payload.local_context.block_num, 5);
    f.storage[0].payload.scalars.set(nullptr);
    EXPECT_EQ(f.publish_arguments(0), SchedulerGraphResult::INVALID_ARGUMENTS);
}

TEST(PercounterPayload, KernelWindowExcludesSkippedWorkAndSharesProfileEndpoints) {
    Fixture f({task(1)});
    int64_t value = 0;
    auto &predicate = f.storage[0].payload.predicate;
    predicate.addr = reinterpret_cast<uint64_t>(&value);
    predicate.target = 1;
    predicate.elem_size = sizeof(value);
    predicate.op = static_cast<PredicateOp>(1);
    auto &cfg = f.cfg[0];
    cfg.has_predicates = true;
    auto task = f.resolve(0);
    auto *payload = &f.payloads[0][0];
    ASSERT_TRUE(percounter_prepare_subtask(cfg, task.id, 0, 0, 1, payload));
    int calls = 0;
    uint64_t inside_kernel = 0;
    execute = [&](int64_t *) {
        ++calls;
        inside_kernel = get_sys_cnt_aicore();
    };

    ASSERT_TRUE(f.run(0, task, payload));
    EXPECT_EQ(cfg.work_window.start_cycle, 0U);
    EXPECT_EQ(cfg.work_window.end_cycle, 0U);
    value = 1;
    const uint64_t callable = payload->function_bin_addr;
    payload->function_bin_addr = 0;
    ASSERT_TRUE(f.run(0, task, payload));
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(cfg.work_window.start_cycle, 0U);
    EXPECT_EQ(cfg.work_window.end_cycle, 0U);

    payload->function_bin_addr = callable;
    ASSERT_TRUE(f.run(0, task, payload));
    const AicoreWorkWindow first = cfg.work_window;
    EXPECT_EQ(calls, 1);
    EXPECT_GT(first.start_cycle, 0U);
    EXPECT_LE(first.start_cycle, inside_kernel);
    EXPECT_GE(first.end_cycle, inside_kernel);
    EXPECT_FALSE(cfg.pending_task_valid);

    value = 0;
    ASSERT_TRUE(f.run(0, task, payload));
    EXPECT_EQ(cfg.work_window.start_cycle, first.start_cycle);
    EXPECT_EQ(cfg.work_window.end_cycle, first.end_cycle);

    PercounterProfileBuffer profile{};
    cfg.profile_buffer = &profile;
    value = 1;
    ASSERT_TRUE(f.run(0, task, payload));
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(cfg.work_window.start_cycle, first.start_cycle);
    EXPECT_GE(cfg.pending_task.start, first.end_cycle);
    EXPECT_LE(cfg.pending_task.start, inside_kernel);
    EXPECT_GE(cfg.work_window.end_cycle, inside_kernel);
    EXPECT_EQ(cfg.work_window.end_cycle, cfg.pending_task.finish);
    EXPECT_TRUE(cfg.pending_task_valid);
}

TEST(PercounterPayload, PreparedMetadataRejectsInvalidSpansBeforePublication) {
    PercounterCounter metadata{};
    const uint64_t functions[3]{1, 2, 0};
    EXPECT_EQ(
        percounter_prepare_argument_metadata(metadata, functions, 1, 0, 0, 0, 1),
        SchedulerGraphResult::INVALID_ARGUMENTS
    );
    EXPECT_EQ(
        percounter_prepare_argument_metadata(metadata, functions, 1, 0x1000, 0, MAX_TENSOR_ARGS + 1, 0),
        SchedulerGraphResult::INVALID_ARGUMENTS
    );
    EXPECT_EQ(
        percounter_prepare_argument_metadata(metadata, functions, 1, 0, UINT64_MAX - 7, 0, 2),
        SchedulerGraphResult::INVALID_ARGUMENTS
    );
    EXPECT_EQ(
        percounter_prepare_argument_metadata(metadata, functions, 7, 0, 0, 0, 0), SchedulerGraphResult::INVALID_CALLABLE
    );
    EXPECT_EQ(metadata.argument_counts, 0U);
    EXPECT_EQ(metadata.function_addresses[0], 0U);
    EXPECT_EQ(
        percounter_prepare_argument_metadata(metadata, functions, 3, 0x1000, 0x2000, 2, 1), SchedulerGraphResult::OK
    );
    EXPECT_EQ(metadata.tensors_address, 0x1000U);
    EXPECT_EQ(metadata.scalars_address, 0x2000U);
    EXPECT_EQ(metadata.argument_counts, (1U << 16) | 2U);
}

TEST(PercounterPayload, ProfilingLevelsReserveOnlyTheirRecordTails) {
    const uint64_t counts[2]{1, 0};
    std::array<PercounterLayout, 5> layouts{};
    for (uint32_t level = 0; level < layouts.size(); ++level)
        ASSERT_TRUE(plan_layout(1, 16, 0, counts, &layouts[level], 0, 1, 0, {}, 0, level));
    EXPECT_EQ(layouts[0].total_size, layouts[1].total_size);
    EXPECT_EQ(layouts[1].profile_buffers_offset, 0);
    EXPECT_NE(layouts[2].profile_buffers_offset, 0);
    EXPECT_NE(layouts[2].task_records_offset, 0);
    EXPECT_EQ(layouts[2].scalar_records_offset, 0);
    EXPECT_EQ(layouts[3].upload_size, layouts[2].upload_size);
    EXPECT_EQ(layouts[3].total_size, layouts[4].total_size);
    EXPECT_GT(layouts[3].scalar_records_offset, layouts[3].upload_size);
    EXPECT_EQ(
        layouts[3].total_size - layouts[3].scalar_records_offset,
        SCHEDULER_WORKER_CAPACITY * SCHEDULER_SCALAR_RECORD_CAPACITY * sizeof(SchedulerScalarRecord)
    );
}

TEST(PercounterPayload, TaskTimingPartitionsOwnWholeCacheLines) {
    PercounterLayout layout{};
    const uint64_t counts[2]{1, 0};
    ASSERT_TRUE(plan_layout(1, 3, 0, counts, &layout, 0, 1, 0, {}, 0, 2));
    std::vector<uint8_t> storage(layout.upload_size + 127);
    const auto address = (reinterpret_cast<uintptr_t>(storage.data()) + 127) & ~uintptr_t{127};
    auto *base = reinterpret_cast<void *>(address);
    TaskInput task{};
    task.active_mask = 1;
    task.block_num = 3;
    task.subtask_count = 1;
    ASSERT_TRUE(build_tables(&task, 1, layout, base, address, 2, {}));
    const auto *buffers = region_at<PercounterProfileBuffer>(base, layout.profile_buffers_offset);
    for (uint32_t worker = 0; worker < SCHEDULER_WORKER_CAPACITY; ++worker) {
        EXPECT_EQ(buffers[worker].task_records_offset % 64, 0) << worker;
        if (worker + 1 < SCHEDULER_WORKER_CAPACITY)
            EXPECT_LE(
                buffers[worker].task_records_offset + 3 * sizeof(PercounterTaskTiming),
                buffers[worker + 1].task_records_offset
            );
    }
}

TEST(PercounterPayload, TaskTimingPreparesIdentityAndBatchesCommittedCount) {
    Fixture f;
    auto buffer = std::make_unique<ChipSwimlaneAicoreTaskBuffer>();
    ChipSwimlaneActiveHead head{};
    head.current_buf_ptr = reinterpret_cast<uint64_t>(buffer.get());
    ChipSwimlaneAicoreLocalState profile{};
    f.cfg[0].profile_head = &head;
    f.cfg[0].profile = &profile;
    auto task = f.resolve(0);
    ASSERT_TRUE(percounter_prepare_subtask(f.cfg[0], task.id, 0, 0, 1, &f.payloads[0][0]));
    int calls = 0;
    execute = [&](int64_t *) {
        ++calls;
    };
    for (uint32_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(f.run(0, task, &f.payloads[0][0]));
        percounter_commit_task_timing(f.cfg[0]);
    }
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(buffer->count, 0U);
    percounter_flush_stream_records(&profile);
    EXPECT_EQ(buffer->count, 3U);
    EXPECT_EQ(head.total_record_count, 0U);
    for (uint32_t i = 0; i < 3; ++i) {
        const auto &record = buffer->records[i];
        EXPECT_EQ(TaskId::to_uint64(record.task_token), TaskId::to_uint64(TaskId::make_global(0)));
        EXPECT_EQ(record.reg_task_id, i + 1);
        EXPECT_GT(record.start_time, 0U);
        EXPECT_GE(record.end_time, record.start_time);
    }
}

TEST(PercounterClusterLoop, OrdinaryCubeAndVectorUseRealWorkerStorage) {
    Fixture f({task(1), task(2), task(1), task(2)});
    f.drain();
    ASSERT_EQ(f.calls.size(), 4U);
    for (const auto &call : f.calls) {
        const auto mask = f.inputs[call[0]].active_mask;
        EXPECT_EQ(call[2] % 3 == 0, mask == 1);
        EXPECT_EQ(call[1], 0);
        f.expect_completed(call[0]);
    }
}

TEST(PercounterClusterLoop, EveryMixMaskAndRaggedSpmdExecutesExactlyOnce) {
    Fixture f({task(3, 5), task(7, 2, true), task(6, 3), task(5, 3), task(7, 5)});
    f.drain();
    std::vector<std::array<int, 3>> actual;
    for (const auto &call : f.calls)
        actual.push_back({int(call[0]), int(call[1]), int(call[2] % 3)});
    std::sort(actual.begin(), actual.end());
    std::vector<std::array<int, 3>> expected;
    for (size_t id = 0; id < f.inputs.size(); ++id) {
        f.expect_completed(id);
        for (int block = 0; block < f.inputs[id].block_num; ++block)
            for (uint32_t lane = 0; lane < 3; ++lane)
                if (f.inputs[id].active_mask & (1U << lane)) expected.push_back({int(id), block, int(lane)});
    }
    EXPECT_EQ(actual, expected);
}

TEST(PercounterClusterLoop, OrdinaryVectorUsesSourceOneOnPhysicalRightVector) {
    Fixture f({task(2, 7)}, 1);
    f.drain();
    ASSERT_EQ(f.calls.size(), 7U);
    bool used_right = false;
    std::array<bool, 7> seen{};
    for (const auto &call : f.calls) {
        EXPECT_NE(call[2] % 3, 0);
        used_right |= call[2] % 3 == 2;
        ASSERT_LT(uint64_t(call[1]), seen.size());
        EXPECT_FALSE(seen[call[1]]);
        seen[call[1]] = true;
    }
    EXPECT_TRUE(used_right);
    f.expect_completed(0);
}

TEST(PercounterClusterLoop, FaninWaitsForBothExternalProducers) {
    int32_t predecessors[2]{0, 1};
    auto external = task(1);
    external.published_off_lane = true;
    auto consumer = task(7);
    consumer.fanin_count = 2;
    consumer.fanin_ids = predecessors;
    Fixture f({external, external, consumer});
    f.rounds(8);
    EXPECT_TRUE(f.calls.empty());
    f.counters[0].value = PERCOUNTER_COUNTER_PUBLISHED;
    f.rounds(8);
    EXPECT_TRUE(f.calls.empty());
    f.counters[1].value = PERCOUNTER_COUNTER_PUBLISHED;
    f.drain();
    ASSERT_EQ(f.calls.size(), 3U);
    for (const auto &call : f.calls)
        EXPECT_EQ(call[0], 2);
}

TEST(PercounterClusterLoop, SingleResourceCompletionPrecedesOwnerRetirement) {
    for (const uint32_t mask : {1U, 2U}) {
        SCOPED_TRACE(mask);
        const uint32_t worker = mask == 1 ? 0 : 2;
        int32_t producer = 0;
        auto consumer = task(mask);
        consumer.fanin_count = 1;
        consumer.fanin_ids = &producer;
        Fixture f({task(mask), consumer}, 1);
        f.step(1);
        EXPECT_FALSE(f.completed(0));
        f.step(worker);
        ASSERT_EQ(f.calls.size(), 1U);
        EXPECT_EQ(f.calls.front()[0], 0);
        EXPECT_NE(f.signals[0].resources[worker].running, PERCOUNTER_CLUSTER_INVALID_REF);
        EXPECT_EQ(percounter_completion_logical(f.component(0, 0)), 1);
        f.drain();
        ASSERT_EQ(f.calls.size(), 2U);
        EXPECT_EQ(f.calls.back()[0], 1);
        EXPECT_EQ(percounter_completion_logical(f.component(0, 0)), 1);
    }
}

TEST(PercounterClusterLoop, OwnerEntersItsMixBeforeDispatchingIndependentWork) {
    Fixture f({task(3), task(2)}, 1);
    f.step(1);
    f.step(2);
    EXPECT_TRUE(f.calls.empty());

    f.step(0);
    ASSERT_EQ(f.calls.size(), 1U);
    EXPECT_EQ(f.calls.front()[0], 0);
    EXPECT_EQ(f.calls.front()[2], 0);
    f.step(1);
    ASSERT_EQ(f.calls.size(), 2U);
    EXPECT_EQ(f.calls.back()[0], 0);
    EXPECT_EQ(f.calls.back()[2], 1);

    f.drain();
    ASSERT_EQ(f.calls.size(), 3U);
    EXPECT_EQ(f.calls.back()[0], 1);
}

TEST(PercounterClusterLoop, ConsumerWaitsForEveryProducerBlock) {
    int32_t producer = 0;
    auto consumer = task(2);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &producer;
    Fixture f({task(1, 7), consumer});
    uint32_t produced = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 0) ++produced;
        else EXPECT_EQ(produced, 7U);
        const auto *local = reinterpret_cast<LocalContext *>(args[PAYLOAD_LOCAL_CONTEXT_INDEX]);
        f.calls.push_back({args[0], local->block_idx, f.running_lane});
    };
    f.drain();
    EXPECT_EQ(produced, 7U);
    EXPECT_EQ(f.calls.size(), 8U);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, SyncStartWaitsForAllBlocksToBeClaimed) {
    Fixture f({task(7, 2, true)}, 2);
    f.step(1);
    f.step(0);
    f.step(2);
    EXPECT_TRUE(f.calls.empty());
    execute = [&](int64_t *args) {
        uint64_t claims = 0;
        for (const auto &sector : f.cfg[0].task_claims[0].sectors)
            claims += sector.next;
        EXPECT_GE(claims, 2U);
        const auto *local = reinterpret_cast<LocalContext *>(args[PAYLOAD_LOCAL_CONTEXT_INDEX]);
        f.calls.push_back({args[0], local->block_idx, f.running_lane});
    };
    f.drain();
    EXPECT_EQ(f.calls.size(), 6U);
}

TEST(PercounterClusterLoop, DependencyOnlyControlNodePublishesWithoutKernel) {
    int32_t producer = 0;
    auto control = task(0);
    control.subtask_count = 1;
    control.fanin_count = 1;
    control.fanin_ids = &producer;
    int32_t token = 1;
    auto consumer = task(1);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &token;
    Fixture f({task(1), control, consumer}, 1);
    f.drain();
    ASSERT_EQ(f.calls.size(), 2U);
    EXPECT_EQ(f.calls[0][0], 0);
    EXPECT_EQ(f.calls[1][0], 2);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, FalsePredicateCompletesWithoutEnteringKernel) {
    Fixture f({task(7)}, 1);
    int64_t value = 0;
    auto &p = f.storage[0].payload.predicate;
    p.addr = reinterpret_cast<uint64_t>(&value);
    p.target = 1;
    p.elem_size = sizeof(value);
    p.op = static_cast<PredicateOp>(1);
    for (auto &c : f.cfg)
        c.has_predicates = true;
    f.drain();
    EXPECT_TRUE(f.calls.empty());
    f.expect_completed(0);
}

TEST(PercounterClusterLoop, MalformedPredicateReportsErrorAndDoesNotEnterKernel) {
    Fixture f({task(1)}, 1);
    f.storage[0].payload.predicate.addr = 1;
    f.storage[0].payload.predicate.elem_size = 3;
    f.storage[0].payload.predicate.op = static_cast<PredicateOp>(1);
    for (auto &c : f.cfg)
        c.has_predicates = true;
    for (uint32_t round = 0; round < 20 && f.control->lane_error == 0; ++round)
        for (uint32_t lane = 0; lane < f.cfg.size(); ++lane)
            f.raw_step(lane);
    EXPECT_NE(f.control->lane_error, 0U);
    EXPECT_TRUE(f.calls.empty());
    EXPECT_FALSE(f.completed(0));
}

TEST(PercounterClusterLoop, SuccessorSettlesDeferAfterProducerReleasedResource) {
    int32_t producer = 0;
    auto consumer = task(1);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &producer;
    Fixture f({task(1), consumer, task(2)}, 1);
    int32_t signal = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 0) f.defer(f.running_lane, &signal);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.control->async_registered == 1 && f.calls.size() == 2;
        },
        "producer and independent task"
    );
    EXPECT_FALSE(f.completed(0));
    EXPECT_FALSE(f.completed(1));
    EXPECT_EQ(f.control->async_settled, 0U);
    f.rounds(8);
    EXPECT_EQ(f.calls.size(), 2U);
    signal = 1;
    f.drain();
    ASSERT_EQ(f.calls.size(), 3U);
    EXPECT_EQ(f.calls.back()[0], 1);
    f.expect_completed(0);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, TerminalSinkSettlesDeferWithoutAUserSuccessor) {
    Fixture f({task(7)}, 1);
    int32_t signal = 0;
    execute = [&](int64_t *args) {
        if (f.running_lane == 2) f.defer(2, &signal);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.control->async_registered == 1;
        },
        "MIX defer publication"
    );
    f.rounds(8);
    EXPECT_FALSE(f.completed(0));
    EXPECT_TRUE(std::any_of(f.state.begin(), f.state.end(), [](const auto &s) {
        return !s->done();
    }));
    signal = 1;
    f.drain();
    f.expect_completed(0);
    EXPECT_EQ(f.calls.size(), 3U);
}

TEST(PercounterClusterLoop, MultipleSuccessorsSettleEachDeferredGroupExactlyOnce) {
    int32_t producer = 0;
    auto c = task(2);
    c.fanin_count = 1;
    c.fanin_ids = &producer;
    Fixture f({task(1), c, c}, 2);
    int32_t signal = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 0) f.defer(f.running_lane, &signal);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.control->async_registered == 1;
        },
        "deferred producer"
    );
    f.rounds(10);
    EXPECT_EQ(f.calls.size(), 1U);
    signal = 1;
    f.drain();
    EXPECT_EQ(f.calls.size(), 3U);
    EXPECT_EQ(f.control->async_registered, 1U);
    EXPECT_EQ(f.control->async_settled, 1U);
}

namespace {
// Models the AICPU materialization publication boundary using the serialized
// host template. The scheduler itself still claims and executes every body block.
struct MaterializedBody {
    std::vector<ChipTaskStorage> storage;
    std::vector<uint64_t> scalars;
    MaterializedBody(Fixture &f, const HostGraphTemplate &body, uint32_t invocation_index = 0) :
        storage(body.tasks.size()),
        scalars(body.tasks.size()) {
        auto *invocation =
            scheduler_state_at<PercounterGraphInvocation>(f.control, f.control->graph_invocations_offset) +
            invocation_index;
        auto *header = scheduler_state_at<PercounterGraphTemplate>(f.control, invocation->template_offset);
        auto *tasks = scheduler_state_at<PercounterBodyTask>(f.control, header->tasks_offset);
        auto *rows = scheduler_state_at<int32_t>(f.control, header->fanin_offsets_offset);
        invocation->storage_address = reinterpret_cast<uint64_t>(storage.data());
        for (uint32_t i = 0; i < storage.size(); ++i) {
            const uint64_t id = invocation->body_base + i;
            scalars[i] = id;
            auto &s = storage[i];
            s.task.task_id = TaskId::make_sub_task(invocation->shell_task_id, i);
            s.payload.scalar_count = 1;
            s.payload.scalars.set(&scalars[i]);
            uint64_t functions[3]{};
            for (uint32_t lane = 0; lane < 3; ++lane) {
                const bool active = (body.tasks[i].metadata.active_mask & (1U << lane)) != 0;
                s.task.kernel_id[lane] = active ? 1 : -1;
                functions[lane] = active ? reinterpret_cast<uint64_t>(&kernel) : 0;
            }
            auto &counter = f.counters[id];
            counter.storage_address = invocation->storage_address;
            counter.body_index = i;
            counter.flags = tasks[i].flags;
            counter.shell_task_id = invocation->shell_task_id;
            counter.fanin_count = rows[i + 1] - rows[i];
            counter.fanin_offset = header->fanin_indices_offset + uint64_t(rows[i]) * sizeof(uint64_t);
            counter.invocation = invocation_index;
            EXPECT_EQ(
                percounter_prepare_argument_metadata(
                    counter, functions, body.tasks[i].metadata.active_mask, 0, reinterpret_cast<uint64_t>(&scalars[i]),
                    0, 1
                ),
                SchedulerGraphResult::OK
            );
        }
    }
    void publish(Fixture &f) {
        auto *invocation =
            scheduler_state_at<PercounterGraphInvocation>(f.control, f.control->graph_invocations_offset);
        for (uint64_t i = 0; i < storage.size(); ++i)
            f.cfg[0].metadata_ready[invocation->body_base + i] = 1;
    }
};
}  // namespace

TEST(PercounterClusterLoop, GraphWaitsForMaterializationThenRunsBodyAndShellSuccessor) {
    const BodyTaskShape shapes[]{{1, false, false, 5, 5}, {7, true, false, 2, 6}};
    const int32_t rows[]{0, 0, 1}, edges[]{0};
    HostGraphTemplate body;
    ASSERT_EQ(build_graph_template(shapes, 2, rows, edges, 1, 6, &body).status, TemplateStatus::OK);
    auto shell = task(1);
    shell.graph_invocation = 0;
    int32_t dependency = 0;
    auto consumer = task(2);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &dependency;
    const GraphInvocationInput invocation{0, 0};
    const GraphPlanInput graph{&body, 1, &invocation, 1};
    Fixture f({shell, consumer}, 2, graph);
    EXPECT_EQ(percounter_completion_logical(f.counters[0].value), 1 - 6);
    f.rounds(8);
    EXPECT_TRUE(f.calls.empty());
    MaterializedBody materialized(f, body);
    f.rounds(8);
    EXPECT_TRUE(f.calls.empty());
    materialized.publish(f);
    uint32_t roots = 0, sinks = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 2) ++roots;
        if (args[0] == 3) {
            EXPECT_EQ(roots, 5U);
            ++sinks;
        }
        if (args[0] == 1) EXPECT_EQ(sinks, 6U);
        const auto *local = reinterpret_cast<LocalContext *>(args[PAYLOAD_LOCAL_CONTEXT_INDEX]);
        f.calls.push_back({args[0], local->block_idx, f.running_lane});
    };
    f.drain();
    EXPECT_EQ(roots, 5U);
    EXPECT_EQ(sinks, 6U);
    EXPECT_EQ(f.calls.size(), 12U);
    f.expect_completed(0);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, GraphDeferredSinkKeepsShellAndOuterSuccessorIncomplete) {
    const BodyTaskShape shape{7, false, false, 1, 3};
    const int32_t rows[]{0, 0};
    HostGraphTemplate body;
    ASSERT_EQ(build_graph_template(&shape, 1, rows, nullptr, 0, 3, &body).status, TemplateStatus::OK);
    auto shell = task(1);
    shell.graph_invocation = 0;
    int32_t dependency = 0;
    auto consumer = task(1);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &dependency;
    const GraphInvocationInput invocation{0, 0};
    const GraphPlanInput graph{&body, 1, &invocation, 1};
    Fixture f({shell, consumer}, 1, graph);
    MaterializedBody materialized(f, body);
    materialized.publish(f);
    int32_t signal = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 2 && f.running_lane == 2) f.defer(2, &signal);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.control->async_registered == 1;
        },
        "Graph deferred sink"
    );
    f.rounds(8);
    EXPECT_FALSE(f.completed(0));
    EXPECT_FALSE(f.completed(1));
    EXPECT_EQ(f.calls.size(), 3U);
    signal = 1;
    f.drain();
    EXPECT_EQ(f.calls.size(), 4U);
    f.expect_completed(0);
    f.expect_completed(1);
    EXPECT_TRUE(percounter_counter_published(f.counters[2].value));
}

TEST(PercounterClusterLoop, BusyOrdinaryVectorPreventsMixEntryUntilItReturns) {
    Fixture f({task(2), task(7)}, 1);
    bool in_vector = false, observed_overlap = false;
    execute = [&](int64_t *args) {
        if (args[0] == 0) {
            in_vector = true;
            const int executing = f.running_lane;
            for (uint32_t round = 0; round < 6; ++round)
                for (uint32_t lane = 0; lane < 3; ++lane)
                    if (int(lane) != executing) f.step(lane);
            f.running_lane = executing;
            in_vector = false;
            observed_overlap = true;
        } else EXPECT_FALSE(in_vector);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain();
    EXPECT_TRUE(observed_overlap);
    EXPECT_EQ(f.calls.size(), 4U);
    f.expect_completed(0);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, TerminalSettlesIndependentSignalsOutOfRegistrationOrder) {
    Fixture f({task(1, 3)}, 1);
    std::array<int32_t, 3> signals{};
    execute = [&](int64_t *args) {
        const auto *local = reinterpret_cast<LocalContext *>(args[PAYLOAD_LOCAL_CONTEXT_INDEX]);
        f.defer(f.running_lane, &signals[local->block_idx]);
        f.calls.push_back({args[0], local->block_idx, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.control->async_registered == 3;
        },
        "three independent deferred blocks"
    );
    signals[1] = 1;
    f.drain_until(
        [&] {
            return f.control->async_settled == 1;
        },
        "second signal settled first"
    );
    EXPECT_FALSE(f.completed(0));
    signals[0] = signals[2] = 1;
    f.drain();
    f.expect_completed(0);
    EXPECT_EQ(f.control->async_settled, 3U);
}

TEST(PercounterClusterLoop, FirstReturningMixParticipantSchedulesWhileTwoPeersRemainInKernel) {
    Fixture f({task(7), task(2)}, 1);
    std::array<bool, 3> inside{};
    bool takeover_observed = false;
    execute = [&](int64_t *args) {
        const uint32_t lane = f.running_lane;
        if (args[0] == 0) {
            inside[lane] = true;
            if (lane == 0) {
                f.step(1);
                f.running_lane = lane;
            } else if (lane == 1) {
                f.step(2);
                f.running_lane = lane;
                // V1 returned first and can schedule/exe ordinary V while C/V0
                // still occupy their MIX resources.
                for (uint32_t i = 0; i < 8 && !takeover_observed; ++i)
                    f.step(2);
                f.running_lane = lane;
            }
            inside[lane] = false;
        } else {
            takeover_observed = inside[0] && inside[1] && lane == 2;
            EXPECT_TRUE(takeover_observed);
        }
        f.calls.push_back({args[0], 0, lane});
    };
    f.step(1);  // V0 initializes and publishes the MIX command.
    f.step(0);  // C -> V0 -> V1 nested kernel occupancy, V1 returns first.
    f.drain();
    EXPECT_TRUE(takeover_observed);
    EXPECT_EQ(f.calls.size(), 4U);
    f.expect_completed(0);
    f.expect_completed(1);
}

TEST(PercounterClusterLoop, InvalidDeferredScratchDoesNotPublishCompletion) {
    Fixture f({task(1)}, 1);
    execute = [&](int64_t *) {
        f.cfg[f.running_lane].async_group->count = PERCOUNTER_ASYNC_CAPACITY + 1;
    };
    for (uint32_t round = 0; round < 20 && f.control->lane_error == 0; ++round)
        for (uint32_t lane = 0; lane < f.cfg.size(); ++lane)
            f.raw_step(lane);
    EXPECT_EQ(f.control->lane_error, uint64_t(SchedulerGraphResult::ASYNC_REGISTRATION_FAILED));
    EXPECT_FALSE(f.completed(0));
    EXPECT_EQ(f.control->async_registered, 0U);
}

TEST(PercounterClusterLoop, TimingCapacityDoesNotDelayRealCompletion) {
    Fixture f({task(1, 3)}, 1);
    PercounterProfileBuffer profile{};
    profile.task_capacity = 1;
    std::array<PercounterTaskTiming, 1> records{};
    uint32_t count = 0, dropped = 0;
    f.cfg[0].profile_buffer = &profile;
    f.cfg[0].task_records = records.data();
    f.cfg[0].profile_task_count = &count;
    f.cfg[0].task_dropped = &dropped;
    f.drain();
    f.expect_completed(0);
    EXPECT_EQ(f.calls.size(), 3U);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(dropped, 2U);
    EXPECT_EQ(TaskId::to_uint64(records[0].task_token), TaskId::to_uint64(TaskId::make_global(0)));
    EXPECT_GT(records[0].dispatch, 0U);
    EXPECT_LE(records[0].dispatch, records[0].start);
    EXPECT_LE(records[0].start, records[0].finish);
    EXPECT_EQ(f.cfg[0].work_window.start_cycle, records[0].start);
    EXPECT_GE(f.cfg[0].work_window.end_cycle, records[0].finish);
}

TEST(PercounterClusterLoop, SupervisorExitBeforeEntryDoesNotClaimOrExecute) {
    Fixture f({task(1)}, 1);
    write_reg(RegId::DATA_MAIN_BASE, AICORE_EXIT_SIGNAL);
    percounter_lane_loop<0>(f.cfg[0]);
    write_reg(RegId::DATA_MAIN_BASE, 0);
    EXPECT_TRUE(f.calls.empty());
    EXPECT_EQ(f.control->terminal_claimed, 0U);
    EXPECT_FALSE(f.completed(0));
}

TEST(PercounterClusterLoop, DeferredMixContributionSurvivesScratchAndRecordReuse) {
    std::vector<TaskInput> inputs;
    for (uint32_t i = 0; i < 15; ++i)
        inputs.push_back(task(i % 2 == 0 ? 7 : 5));
    Fixture f(std::move(inputs), 1);
    int32_t signal = 0;
    execute = [&](int64_t *args) {
        if (args[0] == 0 && f.running_lane == 2) f.defer(2, &signal);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            for (uint32_t i = 1; i < f.inputs.size(); ++i)
                if (!f.completed(i)) return false;
            return f.control->async_registered == 1;
        },
        "all later MIX records retired"
    );
    EXPECT_FALSE(f.completed(0));
    EXPECT_EQ(f.control->async_registered, 1U);
    EXPECT_EQ(f.control->async_settled, 0U);
    EXPECT_EQ(f.calls.size(), 38U);
    signal = 1;
    f.drain();
    f.expect_completed(0);
    EXPECT_EQ(f.control->async_settled, 1U);
}

TEST(PercounterClusterLoop, InvalidCallableReportsTaskIdentityWithoutExecuting) {
    Fixture f({task(1)}, 1);
    f.counters[0].function_addresses[0] = 0;
    for (uint32_t round = 0; round < 20 && f.control->lane_error == 0; ++round)
        for (uint32_t lane = 0; lane < f.cfg.size(); ++lane)
            f.raw_step(lane);
    EXPECT_EQ(f.control->lane_error, uint64_t(SchedulerGraphResult::INVALID_CALLABLE));
    EXPECT_EQ(f.control->lane_error_task, 1U);
    EXPECT_TRUE(f.calls.empty());
    EXPECT_FALSE(f.completed(0));
}

TEST(PercounterClusterLoop, GraphTimingKeepsSubtaskIdentityAfterOwnerRetirement) {
    const BodyTaskShape shape{1, false, false, 2, 2};
    const int32_t rows[]{0, 0};
    HostGraphTemplate body;
    ASSERT_EQ(build_graph_template(&shape, 1, rows, nullptr, 0, 3, &body).status, TemplateStatus::OK);
    auto shell = task(1);
    shell.graph_invocation = 0;
    const GraphInvocationInput invocation{0, 0};
    const GraphPlanInput graph{&body, 1, &invocation, 1};
    Fixture f({shell}, 1, graph);
    MaterializedBody materialized(f, body);
    materialized.publish(f);
    PercounterProfileBuffer profile{};
    profile.task_capacity = 2;
    std::array<PercounterTaskTiming, 2> records{};
    uint32_t count = 0, dropped = 0;
    f.cfg[0].profile_buffer = &profile;
    f.cfg[0].task_records = records.data();
    f.cfg[0].profile_task_count = &count;
    f.cfg[0].task_dropped = &dropped;
    f.drain();
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(dropped, 0U);
    for (const auto &record : records) {
        EXPECT_EQ(TaskId::to_uint64(record.task_token), TaskId::to_uint64(TaskId::make_sub_task(0, 0)));
        EXPECT_LE(record.dispatch, record.start);
        EXPECT_LE(record.start, record.finish);
    }
    f.expect_completed(0);
}

TEST(PercounterClusterLoop, GenericVectorLookaheadUsesOtherLaneWhenPreferredLaneRunsSameTask) {
    Fixture f({task(2), task(2, 3)}, 1);
    PercounterClusterSchedulerMemory memory(f.cfg[1].cluster_owner, &f.signals[0]);
    PercounterClusterScheduler<PercounterClusterSchedulerMemory> setup(memory, PercounterClusterOwner::V0);
    ASSERT_TRUE(setup.initialize_quiescent());
    const uint32_t x = setup.reserve(0, 2, 1);
    ASSERT_NE(x, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_TRUE(setup.commit_claim(x, percounter_order_entry(0, 0, 1), f.cfg[0].metadata[0].global_order_id));
    const uint32_t a = setup.reserve(1, 4, 1);
    ASSERT_NE(a, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_TRUE(setup.commit_claim(a, percounter_order_entry(1, 0, 3), f.cfg[0].metadata[1].global_order_id));
    ASSERT_TRUE(setup.mark_ready(x));
    ASSERT_TRUE(setup.mark_ready(a));
    ASSERT_TRUE(setup.publish(x));
    ASSERT_TRUE(setup.publish(a));
    // Establish a valid dispatch prestate through the production controller:
    // X occupies V0 and A block zero occupies V1; neither has lookahead.
    const uint32_t node = f.cfg[0].projection_task_map[1];
    ASSERT_NE(node, PERCOUNTER_CLUSTER_INVALID_REF);
    const uint32_t claim = f.cfg[0].resource_nodes[node].claim_index;
    ASSERT_NE(claim, PERCOUNTER_CLUSTER_INVALID_REF);
    f.cfg[0].task_claims[claim].sectors[0].next = 1;
    ++f.cfg[0].task_claims[claim].closed_sectors;
    f.signals[0].header.resource_head[0] = PERCOUNTER_CLUSTER_INVALID_REF;
    f.signals[0].header.resource_head[1] = node;
    f.signals[0].header.resource_head[2] = node;
    *f.cfg[0].cluster_ready = 1;
    ASSERT_TRUE(setup.release());
    ASSERT_EQ(f.signals[0].resources[1].next, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_EQ(f.signals[0].resources[2].next, PERCOUNTER_CLUSTER_INVALID_REF);
    // Only C serves the real owner loop. The two dispatched workers stay busy.
    f.step(0);
    EXPECT_NE(f.signals[0].resources[1].next, PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_EQ(f.signals[0].resources[2].next, PERCOUNTER_CLUSTER_INVALID_REF);
    const uint32_t next = f.signals[0].resources[1].next;
    if (next != PERCOUNTER_CLUSTER_INVALID_REF)
        EXPECT_EQ(percounter_order_task_id(f.signals[0].execution[next].entry), 1);
}

TEST(PercounterPayload, RejectsTaskCountThatCollidesWithTerminalTaskSentinel) {
    const uint64_t counts[2]{};
    PercounterLayout layout{};
    EXPECT_FALSE(plan_layout(UINT32_MAX, UINT32_MAX, 0, counts, &layout));
    const GraphLayoutInput graph{1, 0, 0};
    EXPECT_FALSE(plan_layout(uint64_t{UINT32_MAX} - 1, UINT32_MAX, 0, counts, &layout, 0, 0, 0, graph));
}

TEST(PercounterPayload, TicketCapacityTracksOrdinaryPiecesRatherThanTaskCount) {
    PercounterLayout layout{};
    const uint64_t ordinary[2]{100, 0};
    ASSERT_TRUE(plan_layout(100, 100, 0, ordinary, &layout));
    EXPECT_EQ(layout.ticket_interval_count, 2U);
    const uint64_t separated[2]{103, 0};
    ASSERT_TRUE(plan_layout(103, 103, 0, separated, &layout, 0, 3));
    EXPECT_EQ(layout.ticket_interval_count, 8U);
    const uint64_t boundaries[2]{3, 0};
    ASSERT_TRUE(plan_layout(3, 3, 0, boundaries, &layout, 0, 3));
    EXPECT_EQ(layout.ticket_interval_count, 0U);
}

TEST(PercounterClusterLoop, AllMixParticipantsDeferAndSettleExactlyOnceAfterRecordReuse) {
    Fixture f(std::vector<TaskInput>(8, task(7)), 1);
    ASSERT_EQ(f.control->async_completion_capacity, 24U);
    std::array<int32_t, 3> signals{};
    execute = [&](int64_t *args) {
        if (args[0] == 0) f.defer(f.running_lane, &signals[f.running_lane]);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            for (uint32_t id = 1; id < f.inputs.size(); ++id)
                if (!f.completed(id)) return false;
            return f.control->async_registered == 3;
        },
        "all three deferred participants published and later MIX records reused"
    );
    ASSERT_EQ(f.calls.size(), 24U);
    EXPECT_FALSE(f.completed(0));
    EXPECT_EQ(f.control->async_settled, 0U);
    for (uint32_t lane : {2U, 1U}) {
        signals[lane] = 1;
        const uint64_t expected = 3 - lane;
        f.drain_until(
            [&] {
                return f.control->async_settled == expected;
            },
            "one more MIX participant settled"
        );
        EXPECT_FALSE(f.completed(0));
        EXPECT_EQ(f.control->async_registered, 3U);
    }
    signals[0] = 1;
    f.drain();
    f.expect_completed(0);
    EXPECT_EQ(f.control->async_registered, 3U);
    EXPECT_EQ(f.control->async_settled, 3U);
    EXPECT_EQ(f.calls.size(), 24U);
    for (uint32_t index = 0; index < 3; ++index)
        EXPECT_EQ(f.cfg[0].async_completions[index].claim_state, uint64_t(PercounterDeferredState::Settled));
}

TEST(PercounterClusterLoop, AllDeferredMixParticipantsHoldGraphShellThroughLaterBodyRecordReuse) {
    std::array<BodyTaskShape, 8> shapes{};
    for (auto &shape : shapes)
        shape = {7, false, false, 1, 3};
    const std::array<int32_t, 9> rows{};
    HostGraphTemplate body;
    ASSERT_EQ(
        build_graph_template(shapes.data(), shapes.size(), rows.data(), nullptr, 0, 3, &body).status, TemplateStatus::OK
    );
    auto shell = task(1);
    shell.graph_invocation = 0;
    const int32_t predecessor = 0;
    auto consumer = task(1);
    consumer.fanin_count = 1;
    consumer.fanin_ids = &predecessor;
    const GraphInvocationInput invocation{0, 0};
    const GraphPlanInput graph{&body, 1, &invocation, 1};
    Fixture f({shell, consumer}, 1, graph);
    ASSERT_EQ(f.control->async_completion_capacity, 25U);
    MaterializedBody materialized(f, body);
    materialized.publish(f);
    std::array<int32_t, 3> signals{};
    execute = [&](int64_t *args) {
        if (args[0] == 2) f.defer(f.running_lane, &signals[f.running_lane]);
        f.calls.push_back({args[0], 0, f.running_lane});
    };
    f.drain_until(
        [&] {
            return f.calls.size() == 24 && f.control->async_registered == 3;
        },
        "all Graph body participants physically returned"
    );
    f.rounds(8);
    EXPECT_FALSE(percounter_counter_published(f.counters[2].value));
    EXPECT_FALSE(f.completed(0));
    EXPECT_FALSE(f.completed(1));
    EXPECT_EQ(f.control->async_settled, 0U);
    for (uint32_t lane : {1U, 0U}) {
        signals[lane] = 1;
        const uint64_t expected = 2 - lane;
        f.drain_until(
            [&] {
                return f.control->async_settled == expected;
            },
            "one Graph MIX participant settled"
        );
        EXPECT_FALSE(percounter_counter_published(f.counters[2].value));
        EXPECT_FALSE(f.completed(0));
        EXPECT_FALSE(f.completed(1));
        EXPECT_EQ(f.calls.size(), 24U);
    }
    signals[2] = 1;
    f.drain();
    EXPECT_TRUE(percounter_counter_published(f.counters[2].value));
    f.expect_completed(0);
    f.expect_completed(1);
    EXPECT_EQ(f.calls.size(), 25U);
    EXPECT_EQ(f.calls.back()[0], 1);
    EXPECT_EQ(f.control->async_registered, 3U);
    EXPECT_EQ(f.control->async_settled, 3U);
    for (uint32_t index = 0; index < 3; ++index)
        EXPECT_EQ(f.cfg[0].async_completions[index].claim_state, uint64_t(PercounterDeferredState::Settled));
}

TEST(PercounterClusterLoop, ExhaustedSequenceReportsRuntimeErrorInsteadOfSpinningOnPendingWork) {
    Fixture f({task(1)}, 1);
    PercounterClusterSchedulerMemory memory(f.cfg[1].cluster_owner, &f.signals[0]);
    PercounterClusterScheduler<PercounterClusterSchedulerMemory> setup(memory, PercounterClusterOwner::V0);
    ASSERT_TRUE(setup.initialize_quiescent());
    for (uint32_t lane = 0; lane < 3; ++lane)
        f.signals[0].header.resource_head[lane] = f.control->resource_heads[lane];
    ASSERT_NE(f.signals[0].header.resource_head[0], PERCOUNTER_CLUSTER_INVALID_REF);
    f.signals[0].header.sequence = UINT32_MAX;
    *f.cfg[0].cluster_ready = 1;
    ASSERT_TRUE(setup.release());
    EXPECT_FALSE(f.raw_step(0));
    EXPECT_EQ(f.control->lane_error, uint64_t(SchedulerGraphResult::INVALID_ARGUMENTS));
    EXPECT_TRUE(f.calls.empty());
    EXPECT_FALSE(f.completed(0));
}
