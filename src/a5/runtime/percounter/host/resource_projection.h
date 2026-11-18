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
#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "scheduler/resource_projection_types.h"

namespace simpler::percounter {

struct ResourceProjectionInput {
    uint64_t entry;
    uint32_t rank;
    uint32_t catalog_position;
    uint32_t claim_index{PERCOUNTER_PROJECTION_NONE};
    uint32_t sync_index{PERCOUNTER_PROJECTION_NONE};
    uint8_t active_mask;
    uint8_t catalog;
};

struct ResourceProjectionPlan {
    std::vector<PercounterResourceNode> nodes;
    std::vector<PercounterOrdinaryPiece> pieces;
    std::vector<PercounterSyncBoundary> sync_boundaries;
    std::vector<uint32_t> catalog_nodes[PERCOUNTER_PROJECTION_CATALOG_COUNT];
    std::vector<uint32_t> task_to_node;
    std::array<uint32_t, PERCOUNTER_PROJECTION_RESOURCE_COUNT> heads{
        PERCOUNTER_PROJECTION_NONE, PERCOUNTER_PROJECTION_NONE, PERCOUNTER_PROJECTION_NONE
    };
};

// A generic vector task may use either vector lane. A no-kernel control entry
// takes one vector admission token, while MIX uses its entire physical mask.
inline uint8_t resource_projection_eligibility(uint8_t mask) { return mask == 0 || mask == 2 ? uint8_t{6} : mask; }

// Each MIX/SPMD boundary cuts both ordinary catalogs. Tickets cannot allocate
// across a cut, even when the value returned by FAA exceeds the piece length.
inline bool build_resource_projection(
    std::vector<ResourceProjectionInput> input, ResourceProjectionPlan *output, uint32_t task_count = 0
) {
    if (output == nullptr || input.size() >= PERCOUNTER_PROJECTION_NONE) return false;
    std::sort(input.begin(), input.end(), [](const auto &a, const auto &b) {
        return a.rank < b.rank;
    });
    ResourceProjectionPlan plan;
    plan.nodes.reserve(input.size());
    std::array<uint32_t, PERCOUNTER_PROJECTION_RESOURCE_COUNT> tails = plan.heads;
    std::array<uint32_t, PERCOUNTER_PROJECTION_CATALOG_COUNT> active_piece{
        PERCOUNTER_PROJECTION_NONE, PERCOUNTER_PROJECTION_NONE
    };
    std::array<uint32_t, PERCOUNTER_PROJECTION_CATALOG_COUNT> piece_tail = active_piece;
    std::unordered_set<uint32_t> tasks;
    std::unordered_set<uint32_t> claims;
    uint32_t previous_rank = 0;
    for (const auto &source : input) {
        const uint32_t id = static_cast<uint32_t>(source.entry);
        const uint32_t blocks = static_cast<uint32_t>(source.entry >> 48);
        const uint32_t block = static_cast<uint32_t>((source.entry >> 32) & 0xffffU);
        const bool mix = source.active_mask == 3 || source.active_mask >= 5;
        const bool spmd = block == 0xffffU;
        const bool boundary = mix || spmd;
        if (id == PERCOUNTER_PROJECTION_NONE || source.rank == PERCOUNTER_PROJECTION_NONE ||
            (task_count != 0 && id >= task_count) || source.catalog >= PERCOUNTER_PROJECTION_CATALOG_COUNT ||
            source.active_mask > 7 || source.active_mask == 4 || blocks == 0 ||
            (!spmd && (blocks != 1 || block != 0)) || (spmd && blocks < 2) ||
            (source.active_mask == 0 && blocks != 1) || source.catalog != (source.active_mask == 1 ? 0U : 1U) ||
            source.catalog_position != plan.catalog_nodes[source.catalog].size() ||
            (!plan.nodes.empty() && source.rank == previous_rank) || !tasks.insert(id).second ||
            (boundary != (source.claim_index != PERCOUNTER_PROJECTION_NONE)) ||
            (boundary && !claims.insert(source.claim_index).second))
            return false;
        if (source.sync_index != PERCOUNTER_PROJECTION_NONE) {
            if (!spmd || blocks >= 0xffffU || source.sync_index != plan.sync_boundaries.size()) return false;
        }
        previous_rank = source.rank;
        if (boundary) {
            for (uint32_t catalog = 0; catalog < PERCOUNTER_PROJECTION_CATALOG_COUNT; ++catalog) {
                if (active_piece[catalog] != PERCOUNTER_PROJECTION_NONE)
                    plan.pieces[active_piece[catalog]].boundary_rank = source.rank;
                active_piece[catalog] = PERCOUNTER_PROJECTION_NONE;
            }
        }
        const uint32_t node_index = static_cast<uint32_t>(plan.nodes.size());
        PercounterResourceNode node{};
        node.entry = source.entry;
        node.rank = source.rank;
        for (auto &next : node.next)
            next = PERCOUNTER_PROJECTION_NONE;
        node.claim_index = source.claim_index;
        node.piece_index = PERCOUNTER_PROJECTION_NONE;
        node.piece_ordinal = 0;
        node.sync_index = source.sync_index;
        node.active_mask = source.active_mask;
        node.catalog = source.catalog;
        if (!boundary) {
            const uint32_t catalog = source.catalog;
            if (active_piece[catalog] == PERCOUNTER_PROJECTION_NONE) {
                const uint32_t index = static_cast<uint32_t>(plan.pieces.size());
                plan.pieces.push_back(
                    {catalog,
                     source.catalog_position,
                     0,
                     index,
                     PERCOUNTER_PROJECTION_NONE,
                     PERCOUNTER_PROJECTION_NONE,
                     {0, 0}}
                );
                if (piece_tail[catalog] != PERCOUNTER_PROJECTION_NONE)
                    plan.pieces[piece_tail[catalog]].next_piece = index;
                active_piece[catalog] = index;
                piece_tail[catalog] = index;
            }
            node.piece_index = active_piece[catalog];
            node.piece_ordinal = plan.pieces[node.piece_index].length++;
        }
        if (source.sync_index != PERCOUNTER_PROJECTION_NONE) {
            plan.sync_boundaries.push_back(
                {node_index,
                 source.rank,
                 blocks,
                 source.claim_index,
                 source.sync_index,
                 {static_cast<uint32_t>(plan.catalog_nodes[0].size()),
                  static_cast<uint32_t>(plan.catalog_nodes[1].size())},
                 0}
            );
        }
        const uint8_t eligible = resource_projection_eligibility(source.active_mask);
        for (uint32_t resource = 0; resource < PERCOUNTER_PROJECTION_RESOURCE_COUNT; ++resource) {
            if ((eligible & (1U << resource)) == 0) continue;
            if (tails[resource] == PERCOUNTER_PROJECTION_NONE) plan.heads[resource] = node_index;
            else plan.nodes[tails[resource]].next[resource] = node_index;
            tails[resource] = node_index;
        }
        plan.nodes.push_back(node);
        plan.catalog_nodes[source.catalog].push_back(node_index);
    }
    if (task_count != 0) {
        plan.task_to_node.assign(task_count, PERCOUNTER_PROJECTION_NONE);
        for (uint32_t n = 0; n < plan.nodes.size(); ++n)
            plan.task_to_node[static_cast<uint32_t>(plan.nodes[n].entry)] = n;
    }
    *output = std::move(plan);
    return true;
}

}  // namespace simpler::percounter
