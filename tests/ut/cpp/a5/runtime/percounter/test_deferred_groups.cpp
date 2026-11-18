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
#include "deferred_groups.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

namespace {
using namespace simpler::percounter;

struct TestControl {
    uint64_t async_registered{0};
    uint64_t async_settled{0};
    uint64_t async_run_epoch{7};
};

struct DeferredFixture {
    TestControl control{};
    std::array<PercounterDeferredGroup, 8> pool{};
    pto2::resident::AsyncGroupData scratch{};
    std::array<DeferredCompletionEntry, 4> entries{};
    uint64_t completion{percounter_completion_preset(1)};
    uint64_t shell{percounter_completion_preset(1)};
    uint64_t head{0};
    uint64_t shell_head{0};
    int32_t counter{0};
    int32_t other_counter{0};
    uint64_t event{SIMPLER_CCU_EVENT_MASK};

    DeferredFixture() {
        scratch.capacity = 4;
        scratch.run_epoch = control.async_run_epoch;
        scratch.state = pto2::resident::AsyncPublicationState::OPEN;
        add_counter(counter);
    }

    void add_counter(int32_t &source) {
        auto &entry = entries[scratch.count++];
        entry.addr = reinterpret_cast<uint64_t>(&source);
        entry.expected_value = 1;
        entry.engine = COMPLETION_ENGINE_SDMA;
        entry.completion_type = COMPLETION_TYPE_COUNTER;
        entry.backend_cookie = 0;
        entry._pad = 0;
    }

    void add_event() {
        auto &entry = entries[scratch.count++];
        entry.addr = reinterpret_cast<uint64_t>(&event);
        entry.expected_value = 0;
        entry.engine = COMPLETION_ENGINE_CCU;
        entry.completion_type = COMPLETION_TYPE_CCU;
        entry.backend_cookie = SIMPLER_CCU_EVENT_MASK;
        entry._pad = 0;
    }

    SchedulerGraphResult adopt(uint64_t *ref, bool mix = false, bool sink = false) {
        return percounter_deferred_adopt(
            &control, pool.data(), pool.size(), &scratch, entries.data(), reinterpret_cast<uint64_t>(&completion),
            &head, sink ? reinterpret_cast<uint64_t>(&shell) : 0, sink ? &shell_head : nullptr, mix, ref
        );
    }

    SchedulerGraphResult claim(uint64_t ref, uint64_t previous, bool *claimed) {
        return percounter_deferred_try_claim(&pool[ref - 1], previous, claimed);
    }

    SchedulerGraphResult poll(uint64_t ref, bool *settled) {
        return percounter_deferred_poll_owned(&control, pool.data(), pool.size(), ref, settled);
    }
};

TEST(PercounterDeferredGroups, AdoptionPublishesPersistentDataAndResetsScratch) {
    DeferredFixture f;
    uint64_t ref = 99;
    ASSERT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    EXPECT_EQ(ref, 1);
    EXPECT_EQ(f.control.async_registered, 1);
    EXPECT_EQ(f.scratch.count, 0);
    EXPECT_EQ(f.head, 1);
    EXPECT_EQ(f.shell_head, 0);
    EXPECT_EQ(f.pool[0].claim_state, static_cast<uint64_t>(PercounterDeferredState::Unclaimed));
    EXPECT_EQ(f.pool[0].condition_count, 1);
    EXPECT_EQ(f.pool[0].pending_mask, 1);
    EXPECT_EQ(f.pool[0].entries[0].addr, reinterpret_cast<uint64_t>(&f.counter));
    EXPECT_EQ(f.pool[0].completion_address, reinterpret_cast<uint64_t>(&f.completion));
    EXPECT_EQ(f.pool[0].shell_completion_address, 0);
    EXPECT_TRUE(percounter_completion_has_defer(f.completion));
    EXPECT_EQ(percounter_completion_logical(f.completion), 0);
    f.entries[0].addr = 0;
    EXPECT_EQ(f.pool[0].entries[0].addr, reinterpret_cast<uint64_t>(&f.counter));
}

TEST(PercounterDeferredGroups, ComponentAndShellIndexesShareOneClaim) {
    DeferredFixture f;
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref, false, true), SchedulerGraphResult::OK);
    ASSERT_EQ(f.head, f.shell_head);
    bool claimed = false;
    ASSERT_EQ(f.claim(f.head, 0, &claimed), SchedulerGraphResult::OK);
    EXPECT_TRUE(claimed);
    claimed = true;
    ASSERT_EQ(f.claim(f.shell_head, 7, &claimed), SchedulerGraphResult::OK);
    EXPECT_FALSE(claimed);
    EXPECT_EQ(f.pool[0].next_owned, 0);
    EXPECT_TRUE(percounter_completion_has_defer(f.completion));
    EXPECT_TRUE(percounter_completion_has_defer(f.shell));
}

TEST(PercounterDeferredGroups, IndexChainsRetainEarlierPublishedGroups) {
    DeferredFixture f;
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref, false, true), SchedulerGraphResult::OK);
    f.add_counter(f.other_counter);
    ASSERT_EQ(f.adopt(&ref, false, true), SchedulerGraphResult::OK);
    EXPECT_EQ(ref, 2);
    EXPECT_EQ(f.head, 2);
    EXPECT_EQ(f.shell_head, 2);
    EXPECT_EQ(f.pool[1].next, 1);
    EXPECT_EQ(f.pool[1].next_shell, 1);
    EXPECT_EQ(f.pool[0].next, 0);
    EXPECT_EQ(f.pool[0].next_shell, 0);
}

TEST(PercounterDeferredGroups, ZeroRegistrationDoesNotAllocateOrPublish) {
    DeferredFixture f;
    f.scratch.count = 0;
    uint64_t ref = 99;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    EXPECT_EQ(ref, 0);
    EXPECT_EQ(f.control.async_registered, 0);
    EXPECT_EQ(f.head, 0);
    EXPECT_FALSE(percounter_completion_has_defer(f.completion));
}

TEST(PercounterDeferredGroups, RegistrationFailuresAreReturnedBeforeAllocation) {
    DeferredFixture f;
    uint64_t ref = 99;
    f.scratch.error_code = SIMPLER_ERROR_ASYNC_WAIT_OVERFLOW;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_WAIT_OVERFLOW);
    EXPECT_EQ(f.control.async_registered, 0);
    f.scratch.error_code = SIMPLER_ERROR_NONE;
    f.scratch.reservation_limit = 1;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_REGISTRATION_FAILED);
    f.scratch.reservation_limit = 0;
    f.scratch.run_epoch = 6;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_REGISTRATION_FAILED);
    EXPECT_EQ(f.control.async_registered, 0);
    EXPECT_EQ(f.scratch.count, 1);
    EXPECT_EQ(f.head, 0);
}

TEST(PercounterDeferredGroups, InvalidConditionsAreNotPublished) {
    DeferredFixture f;
    f.entries[0]._pad = 1;
    uint64_t ref = 99;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_COMPLETION_INVALID);
    EXPECT_EQ(ref, 0);
    EXPECT_EQ(f.control.async_registered, 0);
    EXPECT_EQ(f.head, 0);
    EXPECT_EQ(f.scratch.count, 1);
}

TEST(PercounterDeferredGroups, ADestructiveEventCannotBeRegisteredTwiceInOneGroup) {
    DeferredFixture f;
    f.scratch.count = 0;
    f.add_event();
    f.add_event();
    uint64_t ref = 0;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_COMPLETION_INVALID);
    EXPECT_EQ(f.control.async_registered, 0);
    EXPECT_EQ(f.event, SIMPLER_CCU_EVENT_MASK);
}

TEST(PercounterDeferredGroups, PoolExhaustionIsReturnedWithoutPublishingAnInvalidReference) {
    DeferredFixture f;
    f.control.async_registered = f.pool.size();
    uint64_t ref = 99;
    EXPECT_EQ(f.adopt(&ref), SchedulerGraphResult::ASYNC_REGISTRATION_FAILED);
    EXPECT_EQ(ref, 0);
    EXPECT_EQ(f.head, 0);
    EXPECT_EQ(f.scratch.count, 1);
}

TEST(PercounterDeferredGroups, InitializingNodesCannotBeClaimedOrChanged) {
    DeferredFixture f;
    auto &group = f.pool[0];
    group.claim_state = static_cast<uint64_t>(PercounterDeferredState::Initializing);
    group.next = 3;
    group.next_shell = 5;
    group.next_owned = 7;
    bool claimed = true;
    ASSERT_EQ(f.claim(1, 0, &claimed), SchedulerGraphResult::OK);
    EXPECT_FALSE(claimed);
    EXPECT_EQ(group.next, 3);
    EXPECT_EQ(group.next_shell, 5);
    EXPECT_EQ(group.next_owned, 7);
    group.claim_state = static_cast<uint64_t>(PercounterDeferredState::Unclaimed);
    ASSERT_EQ(f.claim(1, 2, &claimed), SchedulerGraphResult::OK);
    EXPECT_TRUE(claimed);
    EXPECT_EQ(group.next_owned, 2);
}

TEST(PercounterDeferredGroups, PendingPollsKeepTheFirstSuccessorsClaim) {
    DeferredFixture f;
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    ASSERT_TRUE(claimed);
    bool settled = true;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_FALSE(settled);
    EXPECT_EQ(f.pool[0].claim_state, static_cast<uint64_t>(PercounterDeferredState::Claimed));
    ASSERT_EQ(f.claim(ref, 4, &claimed), SchedulerGraphResult::OK);
    EXPECT_FALSE(claimed);
    EXPECT_EQ(f.pool[0].next_owned, 0);
    EXPECT_EQ(f.control.async_settled, 0);
    EXPECT_FALSE(percounter_completion_published(f.completion));
}

TEST(PercounterDeferredGroups, PartialConsumptionPersistsAcrossOwnerHandoff) {
    DeferredFixture f;
    f.scratch.count = 0;
    f.add_event();
    f.add_counter(f.counter);
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    ASSERT_TRUE(claimed);
    bool settled = false;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_FALSE(settled);
    EXPECT_EQ(f.event, 0);
    EXPECT_EQ(f.pool[0].pending_mask, 2);
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_FALSE(settled);
    f.counter = 1;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_TRUE(settled);
    EXPECT_EQ(f.event, 0);
    EXPECT_TRUE(percounter_completion_published(f.completion));
}

TEST(PercounterDeferredGroups, SettlementPublishesComponentAndShellExactlyOnce) {
    DeferredFixture f;
    f.counter = 1;
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref, false, true), SchedulerGraphResult::OK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    ASSERT_TRUE(claimed);
    bool settled = false;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    ASSERT_TRUE(settled);
    EXPECT_EQ(percounter_completion_logical(f.completion), 1);
    EXPECT_EQ(percounter_completion_logical(f.shell), 1);
    EXPECT_EQ(f.control.async_settled, 1);
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_TRUE(settled);
    EXPECT_EQ(percounter_completion_logical(f.completion), 1);
    EXPECT_EQ(percounter_completion_logical(f.shell), 1);
    EXPECT_EQ(f.control.async_settled, 1);
}

TEST(PercounterDeferredGroups, OneGroupOfManyConditionsWithdrawsOnlyOneMixShare) {
    DeferredFixture f;
    f.completion = percounter_completion_preset(3);
    f.shell = percounter_completion_preset(3);
    f.add_counter(f.other_counter);
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref, true, true), SchedulerGraphResult::OK);
    EXPECT_EQ(percounter_completion_logical(f.completion), -3);
    EXPECT_EQ(percounter_completion_logical(f.shell), -3);
    percounter_gm_fetch_add_issue(f.completion, 3);
    percounter_gm_fetch_add_issue(f.shell, 3);
    EXPECT_EQ(percounter_completion_logical(f.completion), 0);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    bool settled = false;
    f.counter = 1;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_FALSE(settled);
    EXPECT_FALSE(percounter_completion_published(f.completion));
    f.other_counter = 1;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_TRUE(settled);
    EXPECT_EQ(percounter_completion_logical(f.completion), 1);
    EXPECT_EQ(percounter_completion_logical(f.shell), 1);
}

TEST(PercounterDeferredGroups, AllFourConditionsAreAndedBeforeOneSettlement) {
    DeferredFixture f;
    f.scratch.count = 0;
    std::array<int32_t, 4> counters{1, 1, 1, 0};
    for (auto &counter : counters)
        f.add_counter(counter);
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    ASSERT_TRUE(claimed);
    bool settled = false;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_FALSE(settled);
    EXPECT_EQ(f.pool[0].pending_mask, 8);
    EXPECT_EQ(f.control.async_settled, 0);
    counters[3] = 1;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_TRUE(settled);
    EXPECT_EQ(f.control.async_settled, 1);
    EXPECT_EQ(percounter_completion_logical(f.completion), 1);
}

TEST(PercounterDeferredGroups, ConcurrentPublishersRetainBothIndexesAndAllMixDebt) {
    DeferredFixture f;
    f.completion = percounter_completion_preset(2);
    f.shell = percounter_completion_preset(2);
    std::array<pto2::resident::AsyncGroupData, 2> scratch{};
    std::array<std::array<DeferredCompletionEntry, 4>, 2> entries{};
    std::array<int32_t, 2> signals{0, 0};
    std::array<uint64_t, 2> refs{};
    std::array<SchedulerGraphResult, 2> results{};
    std::atomic<uint32_t> arrived{0};
    std::atomic<bool> start{false};
    std::array<std::thread, 2> publishers;
    for (uint32_t i = 0; i < publishers.size(); ++i) {
        scratch[i].capacity = 4;
        scratch[i].run_epoch = 7;
        scratch[i].count = 1;
        entries[i][0] = {
            reinterpret_cast<uint64_t>(&signals[i]), 0, 1, COMPLETION_ENGINE_SDMA, COMPLETION_TYPE_COUNTER, 0
        };
        publishers[i] = std::thread([&, i] {
            arrived.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            results[i] = percounter_deferred_adopt(
                &f.control, f.pool.data(), f.pool.size(), &scratch[i], entries[i].data(),
                reinterpret_cast<uint64_t>(&f.completion), &f.head, reinterpret_cast<uint64_t>(&f.shell), &f.shell_head,
                true, &refs[i]
            );
        });
    }
    while (arrived.load(std::memory_order_acquire) != 2) {}
    start.store(true, std::memory_order_release);
    for (auto &publisher : publishers)
        publisher.join();
    ASSERT_EQ(results[0], SchedulerGraphResult::OK);
    ASSERT_EQ(results[1], SchedulerGraphResult::OK);
    ASSERT_NE(refs[0], refs[1]);
    EXPECT_EQ(f.control.async_registered, 2);
    EXPECT_EQ(percounter_completion_logical(f.completion), -3);
    EXPECT_EQ(percounter_completion_logical(f.shell), -3);
    EXPECT_TRUE(percounter_completion_has_defer(f.completion));
    EXPECT_TRUE(percounter_completion_has_defer(f.shell));
    for (bool shell : {false, true}) {
        uint64_t current = shell ? f.shell_head : f.head;
        uint32_t observed = 0;
        for (uint32_t visited = 0; current != 0 && visited < 3; ++visited) {
            ASSERT_GE(current, 1);
            ASSERT_LE(current, 2);
            observed |= 1U << (current - 1);
            current = shell ? f.pool[current - 1].next_shell : f.pool[current - 1].next;
        }
        EXPECT_EQ(current, 0);
        EXPECT_EQ(observed, 3);
    }
    percounter_gm_fetch_add_issue(f.completion, 2);
    percounter_gm_fetch_add_issue(f.shell, 2);
    signals = {1, 1};
    for (uint64_t ref : refs) {
        bool claimed = false;
        ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
        ASSERT_TRUE(claimed);
        bool settled = false;
        ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
        ASSERT_TRUE(settled);
    }
    EXPECT_EQ(percounter_completion_logical(f.completion), 1);
    EXPECT_EQ(percounter_completion_logical(f.shell), 1);
    EXPECT_EQ(f.control.async_settled, 2);
}

TEST(PercounterDeferredGroups, ReadySignalCannotPublishMixBeforeItsPhysicalAggregation) {
    DeferredFixture f;
    f.completion = percounter_completion_preset(3);
    f.counter = 1;
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref, true), SchedulerGraphResult::OK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    bool settled = false;
    ASSERT_EQ(f.poll(ref, &settled), SchedulerGraphResult::OK);
    EXPECT_TRUE(settled);
    EXPECT_EQ(percounter_completion_logical(f.completion), -2);
    EXPECT_FALSE(percounter_completion_published(f.completion));
    percounter_gm_fetch_add_issue(f.completion, 3);
    EXPECT_TRUE(percounter_completion_published(f.completion));
}

TEST(PercounterDeferredGroups, PollingUnclaimedOrCorruptedGroupsCannotConsumeSignals) {
    DeferredFixture f;
    f.scratch.count = 0;
    f.add_event();
    uint64_t ref = 0;
    ASSERT_EQ(f.adopt(&ref), SchedulerGraphResult::OK);
    bool settled = true;
    EXPECT_EQ(f.poll(ref, &settled), SchedulerGraphResult::ASYNC_COMPLETION_INVALID);
    EXPECT_FALSE(settled);
    EXPECT_EQ(f.event, SIMPLER_CCU_EVENT_MASK);
    bool claimed = false;
    ASSERT_EQ(f.claim(ref, 0, &claimed), SchedulerGraphResult::OK);
    f.pool[0].pending_mask = 2;
    EXPECT_EQ(f.poll(ref, &settled), SchedulerGraphResult::ASYNC_COMPLETION_INVALID);
    EXPECT_EQ(f.event, SIMPLER_CCU_EVENT_MASK);
    EXPECT_EQ(f.control.async_settled, 0);
}

TEST(PercounterDeferredGroups, InvalidReferencesAndOutputsReturnErrors) {
    DeferredFixture f;
    bool settled = true;
    EXPECT_EQ(f.poll(0, &settled), SchedulerGraphResult::INVALID_ARGUMENTS);
    EXPECT_FALSE(settled);
    EXPECT_EQ(f.poll(9, &settled), SchedulerGraphResult::INVALID_ARGUMENTS);
    EXPECT_EQ(f.poll(1, nullptr), SchedulerGraphResult::INVALID_ARGUMENTS);
    bool claimed = true;
    EXPECT_EQ(percounter_deferred_try_claim(nullptr, 0, &claimed), SchedulerGraphResult::INVALID_ARGUMENTS);
    EXPECT_FALSE(claimed);
}

}  // namespace
