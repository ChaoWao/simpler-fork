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

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace simpler::percounter {

enum class PercounterClusterOwner : uint32_t { C = 0, V0 = 1, V1 = 2, NONE = 3 };
inline constexpr uint32_t PERCOUNTER_CLUSTER_INVALID_REF = UINT32_MAX;
inline constexpr uint32_t PERCOUNTER_CLUSTER_LANES = 3;
inline constexpr uint32_t PERCOUNTER_CLUSTER_RECORDS = 6;
inline constexpr uint32_t PERCOUNTER_CLUSTER_RECORD_MASK = (1U << PERCOUNTER_CLUSTER_RECORDS) - 1;
inline constexpr uint32_t PERCOUNTER_CLUSTER_WORDS = 256;

enum PercounterClusterExecutionFlag : uint32_t {
    PERCOUNTER_CLUSTER_CLAIMED = 1U << 0,
    PERCOUNTER_CLUSTER_DEPENDENCIES_READY = 1U << 1,
    PERCOUNTER_CLUSTER_DISPATCHED = 1U << 2,
};

enum PercounterClusterCommandFlag : uint32_t { PERCOUNTER_CLUSTER_RESET_ASYNC = 1U << 0 };

enum class PercounterClusterFinStatus : uint32_t {
    NO_ASYNC = 0,
    HAS_ASYNC = 1,
    SKIPPED = 2,
    ERROR = 3,
    COMPLETION_PUBLISHED = 4,
};
inline constexpr uint32_t PERCOUNTER_CLUSTER_FIN_STATUS_MASK = 0xff;
inline constexpr uint32_t PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED = 1U << 8;

struct alignas(128) PercounterClusterOwnerSector {
    volatile uint64_t owner;
    uint8_t reserved[120];
};

struct PercounterClusterHeader {
    volatile uint32_t sequence;
    volatile uint32_t free_mask;
    volatile uint32_t resource_head[PERCOUNTER_CLUSTER_LANES];
    volatile uint32_t sync_cursor;
    volatile uint32_t prefix_segment[2];
    volatile uint32_t owner_active_hint;
    uint32_t reserved_before_service;
    volatile uint32_t service_cursor;
    volatile uint32_t run_flags;
    uint32_t reserved[4];
};

struct PercounterClusterResource {
    volatile uint32_t running;
    volatile uint32_t next;
    volatile uint32_t consumed_sequence;
    volatile uint32_t flags;
};

struct PercounterClusterCommandSlot {
    volatile uint32_t record;
    volatile uint32_t source_slot;
    volatile uint32_t flags;
    volatile uint32_t sequence;
};

struct PercounterClusterFinSlot {
    volatile uint64_t detail;
    volatile uint32_t status;
    volatile uint32_t sequence;
};

// status packs physical mask, returned mask, execution flags and source catalog
// into consecutive bytes, starting with the least significant byte.
struct PercounterClusterExecution {
    volatile uint64_t entry;
    volatile uint64_t fanin_pending;
    volatile uint64_t fanin_other;
    volatile uint64_t async_owned_head;
    volatile uint64_t async_cursor;
    volatile uint64_t async_scan;
    volatile uint32_t sequence;
    volatile uint32_t rank;
    volatile uint32_t claim_index;
    volatile uint32_t sync_index;
    volatile uint32_t fanin_cursor;
    volatile uint32_t status;
};

struct PercounterClusterFanin {
    volatile uint64_t addresses;
    volatile uint32_t count;
    volatile uint32_t body_base;
};

struct alignas(64) PercounterClusterSchedulerRegion {
    PercounterClusterHeader header;
    PercounterClusterResource resources[PERCOUNTER_CLUSTER_LANES];
    PercounterClusterCommandSlot commands[PERCOUNTER_CLUSTER_LANES];
    PercounterClusterFinSlot fins[PERCOUNTER_CLUSTER_LANES];
    PercounterClusterExecution execution[PERCOUNTER_CLUSTER_RECORDS];
    PercounterClusterFanin fanins[PERCOUNTER_CLUSTER_RECORDS];
    uint8_t reserved_before_defer[224];
    volatile uint64_t observed_defer_head[PERCOUNTER_CLUSTER_RECORDS];
    uint8_t reserved_tail[16];
};

struct PercounterClusterCommand {
    uint64_t entry{0};
    uint32_t sequence{0};
    uint32_t record{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t source_slot{0};
    uint32_t flags{0};
    bool single_resource{false};
};

struct PercounterClusterReturn {
    uint64_t detail{0};
    uint32_t record{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t sequence{0};
    uint32_t lane{0};
    uint32_t status{0};
    bool is_last{false};
};

static_assert(sizeof(PercounterClusterOwnerSector) == 128);
static_assert(sizeof(PercounterClusterHeader) == 64);
static_assert(sizeof(PercounterClusterResource) == 16);
static_assert(sizeof(PercounterClusterCommandSlot) == 16);
static_assert(sizeof(PercounterClusterFinSlot) == 16);
static_assert(sizeof(PercounterClusterExecution) == 72);
static_assert(sizeof(PercounterClusterFanin) == 16);
static_assert(sizeof(PercounterClusterSchedulerRegion) == 1024);
static_assert(offsetof(PercounterClusterSchedulerRegion, execution) == 208);
static_assert(offsetof(PercounterClusterSchedulerRegion, observed_defer_head) == 960);
static_assert(
    std::is_trivially_copyable_v<PercounterClusterOwnerSector> &&
    std::is_standard_layout_v<PercounterClusterOwnerSector>
);
static_assert(
    std::is_trivially_copyable_v<PercounterClusterSchedulerRegion> &&
    std::is_standard_layout_v<PercounterClusterSchedulerRegion>
);
static_assert(
    std::is_trivially_copyable_v<PercounterClusterExecution> && std::is_standard_layout_v<PercounterClusterExecution>
);

}  // namespace simpler::percounter
