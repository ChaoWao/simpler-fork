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

#include "resource_projection.h"

#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace {
using namespace simpler::percounter;
constexpr uint32_t none = PERCOUNTER_PROJECTION_NONE;

ResourceProjectionInput task(
    uint32_t id, uint32_t rank, uint8_t mask, uint32_t position, uint32_t blocks = 1, uint32_t claim = none,
    uint32_t sync = none
) {
    const uint32_t block = blocks > 1 ? 0xffffU : 0;
    return {
        uint64_t{id} | (uint64_t{block} << 32) | (uint64_t{blocks} << 48),
        rank,
        position,
        claim,
        sync,
        mask,
        static_cast<uint8_t>(mask == 1 ? 0 : 1)
    };
}

TEST(PercounterResourceProjection, OrdinaryVectorLanesShareOnePieceAndTicket) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(7, 0, 2, 0), task(9, 1, 2, 1)}, &plan, 12));
    ASSERT_EQ(plan.pieces.size(), 1U);
    EXPECT_EQ(plan.heads[0], none);
    EXPECT_EQ(plan.heads[1], 0U);
    EXPECT_EQ(plan.heads[2], 0U);
    EXPECT_EQ(plan.nodes[0].next[1], 1U);
    EXPECT_EQ(plan.nodes[0].next[2], 1U);
    EXPECT_EQ(plan.nodes[0].piece_index, plan.nodes[1].piece_index);
    EXPECT_EQ(plan.nodes[1].piece_ordinal, 1U);
    EXPECT_EQ(plan.pieces[0].length, 2U);
    EXPECT_EQ(plan.pieces[0].catalog, 1U);
    EXPECT_EQ(plan.task_to_node[7], 0U);
    EXPECT_EQ(plan.task_to_node[9], 1U);
    EXPECT_EQ(plan.task_to_node[8], none);
}

TEST(PercounterResourceProjection, UnallocatedCBlocksKeepDependentMixBehindThem) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(0, 0, 1, 0, 100, 4), task(1, 1, 3, 0, 1, 5), task(2, 2, 2, 1)}, &plan));
    EXPECT_EQ(plan.heads[0], 0U);
    EXPECT_EQ(plan.nodes[0].claim_index, 4U);
    EXPECT_EQ(plan.nodes[0].next[0], 1U);
    EXPECT_EQ(plan.heads[1], 1U);
    EXPECT_EQ(plan.heads[2], 2U);
    EXPECT_EQ(plan.nodes[1].active_mask, 3U);
    EXPECT_EQ(plan.nodes[1].catalog, 1U);
    EXPECT_EQ(plan.nodes[1].next[2], none);
}

TEST(PercounterResourceProjection, SyncCutsBothCatalogsAndPreservesAllEarlierNodes) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection(
        {task(0, 0, 1, 0), task(1, 1, 2, 0), task(2, 2, 1, 1, 2, 3, 0), task(3, 3, 1, 2), task(4, 4, 2, 1)}, &plan
    ));
    ASSERT_EQ(plan.sync_boundaries.size(), 1U);
    const auto &cut = plan.sync_boundaries[0];
    EXPECT_EQ(cut.node, 2U);
    EXPECT_EQ(cut.prefix_count[0], 1U);
    EXPECT_EQ(cut.prefix_count[1], 1U);
    EXPECT_EQ(cut.block_count, 2U);
    EXPECT_EQ(cut.claim_index, 3U);
    EXPECT_EQ(plan.catalog_nodes[0], (std::vector<uint32_t>{0, 2, 3}));
    EXPECT_EQ(plan.catalog_nodes[1], (std::vector<uint32_t>{1, 4}));
    ASSERT_EQ(plan.pieces.size(), 4U);
    EXPECT_EQ(plan.pieces[0].boundary_rank, 2U);
    EXPECT_EQ(plan.pieces[1].boundary_rank, 2U);
    EXPECT_NE(plan.nodes[0].piece_index, plan.nodes[3].piece_index);
    EXPECT_NE(plan.nodes[1].piece_index, plan.nodes[4].piece_index);
    EXPECT_EQ(plan.pieces[0].next_piece, plan.nodes[3].piece_index);
}

TEST(PercounterResourceProjection, OvershootCannotConsumeNextPieceTicket) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(0, 0, 2, 0), task(1, 1, 1, 0, 2, 0, 0), task(2, 2, 2, 1)}, &plan));
    const auto &before = plan.pieces[plan.nodes[0].piece_index];
    const auto &after = plan.pieces[plan.nodes[2].piece_index];
    EXPECT_NE(before.ticket_index, after.ticket_index);
    EXPECT_EQ(before.length, 1U);
    EXPECT_EQ(before.begin, 0U);
    EXPECT_EQ(after.begin, 1U);
    std::vector<uint64_t> tickets(plan.pieces.size(), 0);
    tickets[before.ticket_index] += 8;
    EXPECT_EQ(tickets[after.ticket_index], 0U);
}

TEST(PercounterResourceProjection, EveryResourceChainRetainsItsGlobalOrder) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection(
        {task(0, 0, 1, 0), task(1, 1, 2, 0), task(2, 2, 3, 1, 1, 0), task(3, 3, 5, 2, 1, 1), task(4, 4, 6, 3, 1, 2),
         task(5, 5, 7, 4, 1, 3)},
        &plan
    ));
    const std::vector<uint32_t> expected[3]{{0, 2, 3, 5}, {1, 2, 4, 5}, {1, 3, 4, 5}};
    for (uint32_t lane = 0; lane < 3; ++lane) {
        std::vector<uint32_t> chain;
        for (uint32_t node = plan.heads[lane]; node != none; node = plan.nodes[node].next[lane])
            chain.push_back(node);
        EXPECT_EQ(chain, expected[lane]);
    }
}

TEST(PercounterResourceProjection, RanksSortBeforeCheckingCatalogPositions) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(8, 12, 2, 1), task(3, 4, 1, 0), task(6, 8, 2, 0)}, &plan));
    EXPECT_EQ(static_cast<uint32_t>(plan.nodes[0].entry), 3U);
    EXPECT_EQ(plan.catalog_nodes[1], (std::vector<uint32_t>{1, 2}));
}

TEST(PercounterResourceProjection, NoKernelControlStillHasAnAdmissionResource) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(1, 0, 0, 0)}, &plan));
    EXPECT_EQ(plan.heads[0], none);
    EXPECT_EQ(plan.heads[1], 0U);
    EXPECT_EQ(plan.heads[2], 0U);
    EXPECT_EQ(plan.nodes[0].active_mask, 0U);
}

TEST(PercounterResourceProjection, InvalidInputDoesNotReplaceOutput) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({task(0, 0, 1, 0)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0), task(2, 0, 2, 0)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 1)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0), task(1, 1, 1, 1)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(4, 0, 1, 0)}, &plan, 4));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 7, 0)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0, 2)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0, 1, 0)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0, 1, none, 0)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 3, 0, 1, 2), task(2, 1, 5, 1, 1, 2)}, &plan));
    EXPECT_FALSE(build_resource_projection({task(1, 0, 1, 0, 2, 0, 1)}, &plan));
    EXPECT_EQ(plan.nodes.size(), 1U);
    EXPECT_EQ(plan.nodes[0].entry, uint64_t{1} << 48);
}

TEST(PercounterResourceProjection, EmptyInputHasNoAdmissionHeads) {
    ResourceProjectionPlan plan;
    ASSERT_TRUE(build_resource_projection({}, &plan));
    EXPECT_TRUE(plan.nodes.empty());
    EXPECT_EQ(plan.heads, (std::array<uint32_t, 3>{none, none, none}));
    EXPECT_FALSE(build_resource_projection({}, nullptr));
}
}  // namespace
