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

#include "scheduler/completion_state.h"
#include "scheduler/deferred_completion_types.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace {
using namespace simpler::percounter;

TEST(PercounterCompletionState, InitialValuesRetainSignedCompletionMeaning) {
    EXPECT_EQ(percounter_completion_preset(0), UINT64_C(0x4000000000000001));
    EXPECT_EQ(percounter_completion_preset(1), UINT64_C(0x4000000000000000));
    EXPECT_EQ(percounter_completion_preset(3), UINT64_C(0x3ffffffffffffffe));
    EXPECT_EQ(percounter_completion_logical(percounter_completion_preset(0)), 1);
    EXPECT_EQ(percounter_completion_logical(percounter_completion_preset(1)), 0);
    EXPECT_EQ(percounter_completion_logical(percounter_completion_preset(3)), -2);
    EXPECT_TRUE(percounter_completion_published(percounter_completion_preset(0)));
    EXPECT_FALSE(percounter_completion_published(percounter_completion_preset(1)));
    EXPECT_FALSE(percounter_completion_published(percounter_completion_preset(3)));
}

TEST(PercounterCompletionState, OrdinaryCompletionIsOneNativeAdd) {
    std::atomic<uint64_t> completion{percounter_completion_preset(1)};
    completion.fetch_add(1);
    EXPECT_EQ(percounter_completion_logical(completion.load()), 1);
    EXPECT_TRUE(percounter_completion_published(completion.load()));
    EXPECT_FALSE(percounter_completion_has_defer(completion.load()));
}

TEST(PercounterCompletionState, DeferredSingleCoreWithholdsItsPhysicalContribution) {
    uint64_t completion = percounter_completion_mark_defer(percounter_completion_preset(1));
    EXPECT_EQ(percounter_completion_logical(completion), 0);
    EXPECT_FALSE(percounter_completion_published(completion));
    completion += 1;
    EXPECT_EQ(percounter_completion_logical(completion), 1);
    EXPECT_TRUE(percounter_completion_published(completion));
    EXPECT_TRUE(percounter_completion_has_defer(completion));
}

TEST(PercounterCompletionState, MixDebtHoldsCompletionAfterAllPhysicalReturns) {
    uint64_t completion = percounter_completion_preset(3);
    completion -= 1;
    completion = percounter_completion_mark_defer(completion);
    completion += 3;
    EXPECT_EQ(percounter_completion_logical(completion), 0);
    EXPECT_FALSE(percounter_completion_published(completion));
    completion += 1;
    EXPECT_EQ(percounter_completion_logical(completion), 1);
    EXPECT_TRUE(percounter_completion_published(completion));
    EXPECT_TRUE(percounter_completion_has_defer(completion));
}

TEST(PercounterCompletionState, MixSignalMaySettleBeforeItsPeersReturn) {
    uint64_t completion = percounter_completion_mark_defer(percounter_completion_preset(3) - 1);
    completion += 1;
    EXPECT_EQ(percounter_completion_logical(completion), -2);
    EXPECT_FALSE(percounter_completion_published(completion));
    completion += 3;
    EXPECT_EQ(percounter_completion_logical(completion), 1);
    EXPECT_TRUE(percounter_completion_published(completion));
}

TEST(PercounterCompletionState, EachDeferredMixParticipantOwesOneShare) {
    uint64_t completion = percounter_completion_mark_defer(percounter_completion_preset(3) - 3);
    EXPECT_EQ(percounter_completion_logical(completion), -5);
    completion += 3;
    EXPECT_EQ(percounter_completion_logical(completion), -2);
    completion += 1;
    EXPECT_EQ(percounter_completion_logical(completion), -1);
    EXPECT_FALSE(percounter_completion_published(completion));
    completion += 1;
    EXPECT_EQ(percounter_completion_logical(completion), 0);
    EXPECT_FALSE(percounter_completion_published(completion));
    completion += 1;
    EXPECT_TRUE(percounter_completion_published(completion));
    EXPECT_TRUE(percounter_completion_has_defer(completion));
}

TEST(PercounterCompletionState, SpmdNeedsEveryCompletionComponent) {
    std::array<uint64_t, 4> components{};
    components.fill(percounter_completion_preset(2));
    components[1] = percounter_completion_mark_defer(components[1]);
    components[0] += 2;
    components[1] += 1;
    components[2] += 2;
    components[3] += 2;
    EXPECT_EQ(percounter_completion_logical(components[0]), 1);
    EXPECT_EQ(percounter_completion_logical(components[1]), 0);
    EXPECT_EQ(percounter_completion_logical(components[2]), 1);
    EXPECT_EQ(percounter_completion_logical(components[3]), 1);
    EXPECT_FALSE(percounter_completion_published(components[1]));
    components[1] += 1;
    for (uint64_t component : components)
        EXPECT_TRUE(percounter_completion_published(component));
}

TEST(PercounterCompletionState, GraphShellRetainsDebtBeyondSixteenBitSinkTotals) {
    uint64_t sink = percounter_completion_mark_defer(percounter_completion_preset(3) - 1);
    uint64_t shell = percounter_completion_mark_defer(percounter_completion_preset(70000) - 1);
    sink += 3;
    shell += 3;
    EXPECT_EQ(percounter_completion_logical(sink), 0);
    EXPECT_EQ(percounter_completion_logical(shell), -69997);
    shell += 69997;
    EXPECT_EQ(percounter_completion_logical(shell), 0);
    EXPECT_FALSE(percounter_completion_published(shell));
    sink += 1;
    shell += 1;
    EXPECT_TRUE(percounter_completion_published(sink));
    EXPECT_TRUE(percounter_completion_published(shell));
}

TEST(PercounterCompletionState, MarkerIsIdempotentAndDoesNotPublishTheTask) {
    const uint64_t original = percounter_completion_preset(2);
    const uint64_t marked = percounter_completion_mark_defer(original);
    EXPECT_EQ(percounter_completion_mark_defer(marked), marked);
    EXPECT_EQ(percounter_completion_logical(marked), -1);
    EXPECT_FALSE(percounter_completion_published(marked));
    EXPECT_EQ(percounter_completion_logical(marked + 2), 1);
    EXPECT_TRUE(percounter_completion_published(marked + 2));
    EXPECT_TRUE(percounter_completion_has_defer(marked + 2));
}

TEST(PercounterCompletionState, LogicalSignBoundaryAndOvershootDoNotChangeTheMarker) {
    for (bool marked : {false, true}) {
        uint64_t completion = percounter_completion_preset(2);
        if (marked) completion = percounter_completion_mark_defer(completion);
        EXPECT_EQ(percounter_completion_logical(completion), -1);
        completion += 1;
        EXPECT_EQ(percounter_completion_logical(completion), 0);
        EXPECT_FALSE(percounter_completion_published(completion));
        EXPECT_EQ(percounter_completion_has_defer(completion), marked);
        completion += 1;
        EXPECT_EQ(percounter_completion_logical(completion), 1);
        EXPECT_TRUE(percounter_completion_published(completion));
        EXPECT_EQ(percounter_completion_has_defer(completion), marked);
        completion += 1;
        EXPECT_EQ(percounter_completion_logical(completion), 2);
        EXPECT_TRUE(percounter_completion_published(completion));
        EXPECT_EQ(percounter_completion_has_defer(completion), marked);
    }
}

TEST(PercounterCompletionState, MarkerCasRetriesWithTheActualConcurrentProgress) {
    std::atomic<uint64_t> completion{percounter_completion_preset(3)};
    uint64_t expected = completion.load();
    completion.fetch_add(1);
    EXPECT_FALSE(completion.compare_exchange_strong(expected, percounter_completion_mark_defer(expected)));
    EXPECT_EQ(percounter_completion_logical(expected), -1);
    EXPECT_TRUE(completion.compare_exchange_strong(expected, percounter_completion_mark_defer(expected)));
    completion.fetch_add(2);
    EXPECT_EQ(percounter_completion_logical(completion.load()), 1);
    EXPECT_TRUE(percounter_completion_has_defer(completion.load()));
}

TEST(PercounterCompletionState, MaximumDebtCannotBorrowFromTheMarker) {
    std::atomic<uint64_t> completion{percounter_completion_preset(PERCOUNTER_COMPLETION_MAX_TOTAL)};
    completion.fetch_sub(PERCOUNTER_COMPLETION_MAX_TOTAL);
    EXPECT_EQ(completion.load(), UINT64_C(3));
    EXPECT_FALSE(percounter_completion_has_defer(completion.load()));
    completion.store(percounter_completion_mark_defer(completion.load()));
    completion.fetch_add(PERCOUNTER_COMPLETION_MAX_TOTAL);
    EXPECT_FALSE(percounter_completion_published(completion.load()));
    EXPECT_TRUE(percounter_completion_has_defer(completion.load()));
    completion.fetch_add(PERCOUNTER_COMPLETION_MAX_TOTAL);
    EXPECT_EQ(completion.load(), UINT64_C(0xc000000000000001));
    EXPECT_TRUE(percounter_completion_published(completion.load()));
}

TEST(PercounterCompletionState, InvalidGeometryCannotProduceAValidInitialization) {
    uint64_t output = 77;
    EXPECT_FALSE(percounter_completion_try_preset(PERCOUNTER_COMPLETION_MAX_TOTAL + 1, &output));
    EXPECT_EQ(output, 77);
    EXPECT_EQ(percounter_completion_preset(UINT64_MAX), PERCOUNTER_COMPLETION_INVALID);
    EXPECT_FALSE(percounter_completion_try_preset(1, nullptr));
    EXPECT_TRUE(percounter_completion_try_preset(PERCOUNTER_COMPLETION_MAX_TOTAL, &output));
    EXPECT_EQ(output, UINT64_C(0x2000000000000002));
}

TEST(PercounterCompletionState, TotalRecoveryIsRestrictedToUnmodifiedSeeds) {
    uint64_t total = 99;
    EXPECT_TRUE(percounter_completion_total_from_preset(percounter_completion_preset(70000), &total));
    EXPECT_EQ(total, 70000);
    EXPECT_TRUE(percounter_completion_total_from_preset(percounter_completion_preset(0), &total));
    EXPECT_EQ(total, 0);
    EXPECT_FALSE(percounter_completion_total_from_preset(PERCOUNTER_COMPLETION_INVALID, &total));
    EXPECT_FALSE(percounter_completion_total_from_preset(PERCOUNTER_COMPLETION_BIAS + 2, &total));
    EXPECT_FALSE(percounter_completion_total_from_preset(
        percounter_completion_preset(PERCOUNTER_COMPLETION_MAX_TOTAL) - 1, &total
    ));
    EXPECT_FALSE(percounter_completion_total_from_preset(
        percounter_completion_mark_defer(percounter_completion_preset(1)), &total
    ));
    EXPECT_EQ(total, 0);
    EXPECT_FALSE(percounter_completion_total_from_preset(percounter_completion_preset(1), nullptr));
}

void check_mix_orders(
    uint32_t participants, std::array<uint8_t, 3> stages, uint32_t deferred, bool aggregated, uint64_t completion,
    uint64_t *schedules
) {
    uint32_t settled = 0;
    bool marked = false;
    bool debts_published = true;
    for (uint32_t i = 0; i < deferred; ++i) {
        settled += stages[i] == 3;
        marked |= stages[i] >= 2;
        debts_published &= stages[i] >= 1;
    }
    ASSERT_EQ(percounter_completion_published(completion), aggregated && settled == deferred);
    ASSERT_EQ(percounter_completion_has_defer(completion), marked);
    if (aggregated && settled == deferred) {
        ++*schedules;
        return;
    }
    if (!aggregated && debts_published)
        check_mix_orders(participants, stages, deferred, true, completion + participants, schedules);
    for (uint32_t i = 0; i < deferred; ++i) {
        if (stages[i] == 3) continue;
        auto next = stages;
        ++next[i];
        uint64_t value = completion;
        if (stages[i] == 0) --value;
        else if (stages[i] == 1) value = percounter_completion_mark_defer(value);
        else ++value;
        check_mix_orders(participants, next, deferred, aggregated, value, schedules);
    }
}

TEST(PercounterCompletionState, AllSmallMixInterleavingsRequirePhysicalAndDeferredCompletion) {
    uint64_t schedules = 0;
    for (uint32_t participants = 1; participants <= 3; ++participants) {
        for (uint32_t deferred = 0; deferred <= participants; ++deferred) {
            check_mix_orders(participants, {}, deferred, false, percounter_completion_preset(participants), &schedules);
        }
    }
    EXPECT_GT(schedules, 1000);
}

}  // namespace
