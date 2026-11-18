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

#include <cstdint>
#include <type_traits>

namespace simpler::percounter {

inline constexpr uint32_t PERCOUNTER_PROJECTION_NONE = UINT32_MAX;
inline constexpr uint32_t PERCOUNTER_PROJECTION_RESOURCE_COUNT = 3;
inline constexpr uint32_t PERCOUNTER_PROJECTION_CATALOG_COUNT = 2;

// Node references index the projection image; entry retains the original task id.
struct PercounterResourceNode {
    uint64_t entry;
    uint32_t rank;
    uint32_t next[PERCOUNTER_PROJECTION_RESOURCE_COUNT];
    uint32_t claim_index;
    uint32_t piece_index;
    uint32_t piece_ordinal;
    uint32_t sync_index;
    uint8_t active_mask;
    uint8_t catalog;
    uint16_t reserved;
};
static_assert(sizeof(PercounterResourceNode) == 48);
static_assert(
    std::is_trivially_copyable_v<PercounterResourceNode> && std::is_standard_layout_v<PercounterResourceNode>
);

struct alignas(8) PercounterOrdinaryPiece {
    uint32_t catalog;
    uint32_t begin;
    uint32_t length;
    uint32_t ticket_index;
    uint32_t next_piece;
    uint32_t boundary_rank;
    uint32_t reserved[2];
};
static_assert(sizeof(PercounterOrdinaryPiece) == 32);
static_assert(
    std::is_trivially_copyable_v<PercounterOrdinaryPiece> && std::is_standard_layout_v<PercounterOrdinaryPiece>
);

// Prefix counts exclude this sync node. Each earlier node in both catalogs must
// be allocated; checking just the final node does not certify a prefix with holes.
struct alignas(8) PercounterSyncBoundary {
    uint32_t node;
    uint32_t rank;
    uint32_t block_count;
    uint32_t claim_index;
    uint32_t sync_index;
    uint32_t prefix_count[PERCOUNTER_PROJECTION_CATALOG_COUNT];
    uint32_t reserved;
};
static_assert(sizeof(PercounterSyncBoundary) == 32);
static_assert(
    std::is_trivially_copyable_v<PercounterSyncBoundary> && std::is_standard_layout_v<PercounterSyncBoundary>
);

}  // namespace simpler::percounter
