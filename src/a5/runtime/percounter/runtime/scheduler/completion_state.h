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

#if defined(__CCE_AICORE__)
#define PERCOUNTER_COMPLETION_CONSTEXPR inline __aicore__ constexpr
#else
#define PERCOUNTER_COMPLETION_CONSTEXPR inline constexpr
#endif

namespace simpler::percounter {

inline constexpr uint64_t PERCOUNTER_COMPLETION_DEFER_BIT = uint64_t{1} << 63;
inline constexpr uint64_t PERCOUNTER_COMPLETION_COUNT_MASK = PERCOUNTER_COMPLETION_DEFER_BIT - 1;
inline constexpr uint64_t PERCOUNTER_COMPLETION_BIAS = uint64_t{1} << 62;
inline constexpr uint64_t PERCOUNTER_COMPLETION_MAX_TOTAL = (uint64_t{1} << 61) - 1;
inline constexpr uint64_t PERCOUNTER_COMPLETION_INVALID = 0;

/**
 * The low field stores BIAS+c, where c starts at 1-T and publishes at c>0.
 * At most T physical invocations can each create one deferred group, so the
 * lowest valid count is 1-T-D >= 1-2*T. For T<=MAX_TOTAL the low field stays
 * in [3, BIAS+1], even if all debt precedes all completion contributions.
 * Native FAA(+n/-n) therefore cannot carry into or borrow from the tag bit.
 * Each deferred debt and its settlement must still be contributed once.
 */
PERCOUNTER_COMPLETION_CONSTEXPR bool percounter_completion_total_valid(uint64_t total) {
    return total <= PERCOUNTER_COMPLETION_MAX_TOTAL;
}

/** Invalid geometry has no valid encoded seed; callers must reject it. */
PERCOUNTER_COMPLETION_CONSTEXPR uint64_t percounter_completion_preset(uint64_t total) {
    return percounter_completion_total_valid(total) ? PERCOUNTER_COMPLETION_BIAS + 1 - total :
                                                      PERCOUNTER_COMPLETION_INVALID;
}

PERCOUNTER_COMPLETION_CONSTEXPR bool percounter_completion_try_preset(uint64_t total, uint64_t *seed) {
    if (seed == nullptr || !percounter_completion_total_valid(total)) return false;
    *seed = percounter_completion_preset(total);
    return true;
}

PERCOUNTER_COMPLETION_CONSTEXPR int64_t percounter_completion_logical(uint64_t word) {
    return static_cast<int64_t>(word & PERCOUNTER_COMPLETION_COUNT_MASK) -
           static_cast<int64_t>(PERCOUNTER_COMPLETION_BIAS);
}

PERCOUNTER_COMPLETION_CONSTEXPR bool percounter_completion_published(uint64_t word) {
    return (word & PERCOUNTER_COMPLETION_COUNT_MASK) > PERCOUNTER_COMPLETION_BIAS;
}

PERCOUNTER_COMPLETION_CONSTEXPR bool percounter_completion_has_defer(uint64_t word) {
    return (word & PERCOUNTER_COMPLETION_DEFER_BIT) != 0;
}

/** The desired value for a CAS; publication/cache ordering belongs to its caller. */
PERCOUNTER_COMPLETION_CONSTEXPR uint64_t percounter_completion_mark_defer(uint64_t word) {
    return word | PERCOUNTER_COMPLETION_DEFER_BIT;
}

/** Recover T only from an unmodified, untagged initialization seed. */
PERCOUNTER_COMPLETION_CONSTEXPR bool percounter_completion_total_from_preset(uint64_t seed, uint64_t *total) {
    if (total == nullptr || percounter_completion_has_defer(seed) ||
        seed < PERCOUNTER_COMPLETION_BIAS + 1 - PERCOUNTER_COMPLETION_MAX_TOTAL ||
        seed > PERCOUNTER_COMPLETION_BIAS + 1)
        return false;
    *total = PERCOUNTER_COMPLETION_BIAS + 1 - seed;
    return true;
}

static_assert(PERCOUNTER_COMPLETION_BIAS + 1 - 2 * PERCOUNTER_COMPLETION_MAX_TOTAL == 3);
static_assert(percounter_completion_preset(0) == PERCOUNTER_COMPLETION_BIAS + 1);
static_assert(percounter_completion_logical(percounter_completion_preset(3)) == -2);
static_assert(percounter_completion_logical(percounter_completion_mark_defer(percounter_completion_preset(3))) == -2);

}  // namespace simpler::percounter

#undef PERCOUNTER_COMPLETION_CONSTEXPR
