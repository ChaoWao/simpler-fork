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

#include "completion_types.h"

namespace simpler::percounter {

inline constexpr uint32_t PERCOUNTER_DEFERRED_CONDITION_CAPACITY = 4;
static_assert(PERCOUNTER_DEFERRED_CONDITION_CAPACITY <= MAX_COMPLETIONS_PER_TASK);

enum class PercounterDeferredState : uint64_t {
    Unclaimed = 0,
    Claimed = 1,
    Settled = 2,
    Initializing = UINT64_MAX,
};

/**
 * Every link is a pool index plus one; zero terminates a list. Component and
 * optional shell indexes refer to this same group and share its claim word.
 * Initializing protects both index links until the publisher makes them
 * durable. A reader must not follow either link while that state is visible.
 * Only the first claiming successor's cluster writes pending_mask/next_owned.
 * Claim state remains Claimed across pending polls and cluster-owner handoff.
 * Completion addresses are resolved GM words; a zero shell address means
 * that this group owes no direct Graph-shell contribution.
 */
struct alignas(64) PercounterDeferredGroup {
    uint64_t next;
    uint64_t next_shell;
    uint64_t next_owned;
    volatile uint64_t pending_mask;
    uint64_t completion_address;
    uint64_t shell_completion_address;
    volatile uint64_t claim_state;
    uint64_t condition_count;
    DeferredCompletionEntry entries[PERCOUNTER_DEFERRED_CONDITION_CAPACITY];
};

static_assert(sizeof(PercounterDeferredGroup) == 192);
static_assert(alignof(PercounterDeferredGroup) == 64);
static_assert(offsetof(PercounterDeferredGroup, claim_state) == 48);
static_assert(offsetof(PercounterDeferredGroup, entries) == 64);
static_assert(
    std::is_trivially_copyable_v<PercounterDeferredGroup> && std::is_standard_layout_v<PercounterDeferredGroup>
);

}  // namespace simpler::percounter
