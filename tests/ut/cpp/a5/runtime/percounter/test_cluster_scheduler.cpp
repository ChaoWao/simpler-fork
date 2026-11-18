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

#include "scheduler/cluster_scheduler.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <functional>
#include <thread>

namespace {
using namespace simpler::percounter;

struct SharedMemory {
    std::atomic<uint64_t> owner{static_cast<uint64_t>(PercounterClusterOwner::NONE)};
    std::array<std::atomic<uint32_t>, 256> words{};
};

struct Memory {
    SharedMemory &shared;
    std::function<void()> before_cas{};
    uint64_t owner_load() { return shared.owner.load(std::memory_order_acquire); }
    uint64_t owner_cas(uint32_t expected, uint32_t desired) {
        if (before_cas) before_cas();
        uint64_t value = expected;
        shared.owner.compare_exchange_strong(value, desired, std::memory_order_acq_rel);
        return value;
    }
    void owner_store(uint32_t value) { shared.owner.store(value, std::memory_order_release); }
    uint32_t load(uint32_t index) { return shared.words.at(index).load(std::memory_order_acquire); }
    void store(uint32_t index, uint32_t value) { shared.words.at(index).store(value, std::memory_order_release); }
    void acquire() { std::atomic_thread_fence(std::memory_order_acquire); }
    void release() { std::atomic_thread_fence(std::memory_order_release); }
};

TEST(PercounterClusterScheduler, ReservationRetainsTheWholeTargetSetWhileTargetsAreBusy) {
    SharedMemory shared;
    Memory memory{shared};
    PercounterClusterScheduler control(memory, PercounterClusterOwner::V0);
    ASSERT_TRUE(control.initialize_quiescent());
    const uint32_t current = control.reserve(11, 1, 0);
    ASSERT_NE(current, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_TRUE(control.commit_claim(current, 11, 0));
    ASSERT_TRUE(control.mark_ready(current));
    ASSERT_TRUE(control.publish(current));
    const uint32_t next = control.reserve(12, 3, 1);
    ASSERT_NE(next, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_TRUE(control.commit_claim(next, 12, 1));
    EXPECT_EQ(control.next_ref(0), next);
    EXPECT_EQ(control.next_ref(1), next);
    EXPECT_FALSE(control.can_dispatch(next));
    EXPECT_EQ(control.reserve(13, 2, 1), PERCOUNTER_CLUSTER_INVALID_REF);
}

struct Harness {
    SharedMemory shared;
    Memory cm{shared}, v0m{shared}, v1m{shared};
    PercounterClusterScheduler<Memory> c{cm, PercounterClusterOwner::C};
    PercounterClusterScheduler<Memory> v0{v0m, PercounterClusterOwner::V0};
    PercounterClusterScheduler<Memory> v1{v1m, PercounterClusterOwner::V1};

    Harness() { EXPECT_TRUE(v0.initialize_quiescent()); }
    uint32_t claim(uint32_t task, uint32_t mask, uint32_t catalog = 1, uint32_t block = 0) {
        const uint32_t ref = v0.reserve(task, mask, catalog);
        EXPECT_NE(ref, PERCOUNTER_CLUSTER_INVALID_REF);
        EXPECT_TRUE(v0.commit_claim(ref, uint64_t{task} | (uint64_t{block} << 32) | (uint64_t{8} << 48), task));
        EXPECT_TRUE(v0.mark_ready(ref));
        return ref;
    }
};

TEST(PercounterClusterScheduler, SameTaskCannotOccupyRunningAndNextOnTheSamePhysicalResource) {
    Harness h;
    const uint32_t first = h.claim(41, 1, 0);
    ASSERT_TRUE(h.v0.publish(first));
    EXPECT_EQ(h.v0.reserve(41, 1, 0), PERCOUNTER_CLUSTER_INVALID_REF);
    const uint32_t second = h.claim(42, 1, 0);
    EXPECT_EQ(h.v0.next_ref(0), second);
    EXPECT_EQ(h.v0.running_ref(0), first);
    EXPECT_EQ(h.v0.reserve(41, 3, 1), PERCOUNTER_CLUSTER_INVALID_REF);
}

TEST(PercounterClusterScheduler, OrdinaryVectorBlocksCanUseBothPhysicalVectorsWithTheSameSourceSlot) {
    Harness h;
    const uint32_t left = h.claim(51, 2, 1, 0);
    ASSERT_TRUE(h.v0.publish(left));
    const uint32_t right = h.claim(51, 4, 1, 1);
    ASSERT_TRUE(h.v0.publish(right));
    PercounterClusterCommand lcmd, rcmd, unused;
    EXPECT_TRUE(h.v0.poll_command(lcmd));
    EXPECT_TRUE(h.v1.poll_command(rcmd));
    EXPECT_FALSE(h.c.poll_command(unused));
    EXPECT_EQ(lcmd.source_slot, 1U);
    EXPECT_EQ(rcmd.source_slot, 1U);
    EXPECT_NE(lcmd.record, rcmd.record);
    EXPECT_EQ(h.v0.reserve(51, 2, 1), PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_EQ(h.v0.reserve(51, 4, 1), PERCOUNTER_CLUSTER_INVALID_REF);
}

TEST(PercounterClusterScheduler, RightVectorMixUsesTheRightCallableAndNoUnrelatedFin) {
    Harness h;
    const uint32_t ref = h.claim(61, 5);
    ASSERT_TRUE(h.v0.publish(ref));
    PercounterClusterCommand ccmd, rcmd, unused;
    ASSERT_TRUE(h.c.poll_command(ccmd));
    ASSERT_TRUE(h.v1.poll_command(rcmd));
    EXPECT_FALSE(h.v0.poll_command(unused));
    EXPECT_EQ(ccmd.source_slot, 0U);
    EXPECT_EQ(rcmd.source_slot, 2U);
    ASSERT_TRUE(h.c.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    ASSERT_TRUE(h.v1.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    PercounterClusterReturn fin;
    ASSERT_TRUE(h.v0.collect_fin(0, fin));
    EXPECT_FALSE(fin.is_last);
    ASSERT_TRUE(h.v0.commit_fin(0, fin.sequence));
    EXPECT_FALSE(h.v0.collect_fin(1, fin));
    ASSERT_TRUE(h.v0.collect_fin(2, fin));
    EXPECT_TRUE(fin.is_last);
    ASSERT_TRUE(h.v0.commit_fin(2, fin.sequence));
    ASSERT_TRUE(h.v0.retire(ref));
    EXPECT_TRUE(h.v0.resources_quiescent());
}

TEST(PercounterClusterScheduler, PartialMixReturnReusesOnlyThatResourceAndPinsTheOriginalRecord) {
    Harness h;
    const uint32_t mix = h.claim(71, 3);
    ASSERT_TRUE(h.v0.publish(mix));
    PercounterClusterCommand cmd;
    ASSERT_TRUE(h.c.poll_command(cmd));
    ASSERT_TRUE(h.v0.poll_command(cmd));
    ASSERT_TRUE(h.c.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    PercounterClusterReturn fin;
    ASSERT_TRUE(h.v0.collect_fin(0, fin));
    EXPECT_FALSE(h.v0.release());
    EXPECT_FALSE(h.v0.retire(mix));
    ASSERT_TRUE(h.v0.commit_fin(0, fin.sequence));
    EXPECT_EQ(h.v0.running_ref(0), PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_EQ(h.v0.running_ref(1), mix);
    const uint32_t newer = h.claim(72, 1, 0);
    ASSERT_TRUE(h.v0.publish(newer));
    ASSERT_TRUE(h.c.poll_command(cmd));
    EXPECT_EQ(cmd.record, newer);
    ASSERT_TRUE(h.v0.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    ASSERT_TRUE(h.v0.collect_fin(1, fin));
    EXPECT_TRUE(fin.is_last);
    ASSERT_TRUE(h.v0.commit_fin(1, fin.sequence));
    EXPECT_FALSE(h.v0.release());
    EXPECT_EQ(h.v0.running_ref(1), mix);
    ASSERT_TRUE(h.v0.retire(mix));
    EXPECT_EQ(h.v0.running_ref(0), newer);
    EXPECT_EQ(h.v0.running_ref(1), PERCOUNTER_CLUSTER_INVALID_REF);
}

TEST(PercounterClusterScheduler, OneCoreReturnCanRetireAndIssueItsSuccessorWithoutPeerFins) {
    Harness h;
    const uint32_t first = h.claim(81, 1, 0);
    ASSERT_TRUE(h.v0.publish(first));
    const uint32_t next = h.claim(82, 1, 0);
    EXPECT_FALSE(h.v0.publish(next));
    ASSERT_TRUE(h.v0.release());
    PercounterClusterCommand command;
    ASSERT_TRUE(h.c.poll_command(command));
    ASSERT_TRUE(h.c.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    EXPECT_FALSE(h.c.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    ASSERT_TRUE(h.c.try_acquire());
    PercounterClusterReturn fin;
    ASSERT_TRUE(h.c.collect_fin(0, fin));
    EXPECT_TRUE(fin.is_last);
    EXPECT_FALSE(h.c.release());
    ASSERT_TRUE(h.c.commit_fin(0, fin.sequence));
    EXPECT_FALSE(h.c.release());
    ASSERT_TRUE(h.c.retire(first));
    ASSERT_TRUE(h.c.publish(next));
    ASSERT_TRUE(h.c.poll_command(command));
    EXPECT_EQ(command.record, next);
    EXPECT_FALSE(h.c.collect_fin(0, fin));
}

TEST(PercounterClusterScheduler, SixRecordsBoundThreeCurrentAndThreeFutureInvocations) {
    Harness h;
    std::array<uint32_t, 6> refs{};
    for (uint32_t lane = 0; lane < 3; ++lane) {
        refs[lane] = h.claim(100 + lane, 1U << lane, lane == 0 ? 0 : 1);
        ASSERT_TRUE(h.v0.publish(refs[lane]));
    }
    for (uint32_t lane = 0; lane < 3; ++lane)
        refs[3 + lane] = h.claim(103 + lane, 1U << lane, lane == 0 ? 0 : 1);
    for (size_t i = 0; i < refs.size(); ++i)
        for (size_t j = i + 1; j < refs.size(); ++j)
            EXPECT_NE(refs[i], refs[j]);
    EXPECT_EQ(h.v0.reserve(107, 7, 1), PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_FALSE(h.v0.resources_quiescent());
}

TEST(PercounterClusterScheduler, FailedClaimRollsBackOnlyTheCompleteLocalReservation) {
    Harness h;
    const uint32_t ref = h.v0.reserve(111, 7, 1);
    ASSERT_NE(ref, PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_EQ(h.v0.next_ref(0), ref);
    EXPECT_EQ(h.v0.next_ref(1), ref);
    EXPECT_EQ(h.v0.next_ref(2), ref);
    EXPECT_FALSE(h.v0.release());
    ASSERT_TRUE(h.v0.rollback(ref));
    EXPECT_FALSE(h.v0.rollback(ref));
    EXPECT_TRUE(h.v0.resources_quiescent());
    EXPECT_EQ(h.v0.reserve(112, 3, 1), ref);
}

TEST(PercounterClusterScheduler, AuthorityDoesNotComeFromAnOwnerSampleNamingSelf) {
    Harness h;
    h.shared.owner.store(static_cast<uint64_t>(PercounterClusterOwner::C));
    EXPECT_FALSE(h.c.try_acquire());
    EXPECT_FALSE(h.c.has_authority());
    EXPECT_FALSE(h.c.release());
    EXPECT_EQ(h.c.reserve(121, 1, 0), PERCOUNTER_CLUSTER_INVALID_REF);
}

TEST(PercounterClusterScheduler, DelayedCandidateAcquiresTheCurrentVacancyAndReadsNewCommands) {
    Harness h;
    ASSERT_TRUE(h.v0.release());
    h.cm.before_cas = [&] {
        ASSERT_TRUE(h.v1.try_acquire());
        const uint32_t ref = h.v1.reserve(131, 1, 0);
        ASSERT_NE(ref, PERCOUNTER_CLUSTER_INVALID_REF);
        ASSERT_TRUE(h.v1.commit_claim(ref, 131, 4));
        ASSERT_TRUE(h.v1.mark_ready(ref));
        ASSERT_TRUE(h.v1.publish(ref));
        ASSERT_TRUE(h.v1.release());
    };
    ASSERT_TRUE(h.c.try_acquire());
    PercounterClusterCommand cmd;
    ASSERT_TRUE(h.c.poll_command(cmd));
    EXPECT_EQ(cmd.entry, 131U);
    EXPECT_EQ(h.c.running_ref(0), cmd.record);
    EXPECT_TRUE(h.c.has_authority());
    EXPECT_TRUE(h.c.before_kernel());
    EXPECT_FALSE(h.c.has_authority());
    EXPECT_EQ(h.shared.owner.load(), static_cast<uint64_t>(PercounterClusterOwner::NONE));
    EXPECT_FALSE(h.c.before_kernel());
}

TEST(PercounterClusterScheduler, FullWidthCasFailureCannotBeMistakenForNone) {
    Harness h;
    ASSERT_TRUE(h.v0.release());
    h.cm.before_cas = [&] {
        h.shared.owner.store((uint64_t{1} << 32) | 3U);
    };
    EXPECT_FALSE(h.c.try_acquire());
    EXPECT_FALSE(h.c.has_authority());
    EXPECT_EQ(h.shared.owner.load(), (uint64_t{1} << 32) | 3U);
}

TEST(PercounterClusterScheduler, SuccessfulSkipAndErrorHaveDistinctFinStatusesAndPreserveDetail) {
    Harness h;
    const uint32_t skipped = h.claim(141, 2);
    ASSERT_TRUE(h.v0.publish(skipped));
    PercounterClusterCommand cmd;
    ASSERT_TRUE(h.v0.poll_command(cmd));
    EXPECT_FALSE(h.v0.publish_fin(4));
    ASSERT_TRUE(h.v0.publish_fin(
        static_cast<uint32_t>(PercounterClusterFinStatus::SKIPPED) | PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED
    ));
    PercounterClusterReturn fin;
    ASSERT_TRUE(h.v0.collect_fin(1, fin));
    EXPECT_EQ(
        fin.status & PERCOUNTER_CLUSTER_FIN_STATUS_MASK, static_cast<uint32_t>(PercounterClusterFinStatus::SKIPPED)
    );
    EXPECT_NE(fin.status & PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED, 0U);
    ASSERT_TRUE(h.v0.commit_fin(1, fin.sequence));
    ASSERT_TRUE(h.v0.retire(skipped));
    const uint32_t error = h.claim(142, 4);
    ASSERT_TRUE(h.v0.publish(error));
    ASSERT_TRUE(h.v1.poll_command(cmd));
    const uint64_t detail = UINT64_C(0xabcdeffe01234567);
    ASSERT_TRUE(h.v1.publish_fin(PercounterClusterFinStatus::ERROR, detail));
    ASSERT_TRUE(h.v0.collect_fin(2, fin));
    EXPECT_EQ(fin.status, static_cast<uint32_t>(PercounterClusterFinStatus::ERROR));
    EXPECT_EQ(fin.detail, detail);
}

TEST(PercounterClusterScheduler, SequenceExhaustionRefusesAReservationBeforeItsClaim) {
    Harness h;
    constexpr uint32_t sequence_word =
        (offsetof(PercounterClusterSchedulerRegion, header) + offsetof(PercounterClusterHeader, sequence)) / 4;
    h.shared.words[sequence_word].store(UINT32_MAX);
    EXPECT_EQ(h.v0.reserve(151, 7, 1), PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_TRUE(h.v0.resources_quiescent());
}

TEST(PercounterClusterScheduler, DependencyOnlyNodeRetiresWithoutACommandOrFakeFin) {
    Harness h;
    const uint32_t ref = h.v0.reserve(161, 2, 1);
    ASSERT_NE(ref, PERCOUNTER_CLUSTER_INVALID_REF);
    ASSERT_TRUE(h.v0.commit_claim(ref, 161, 0));
    EXPECT_FALSE(h.v0.complete_without_kernel(ref));
    ASSERT_TRUE(h.v0.mark_ready(ref));
    ASSERT_TRUE(h.v0.complete_without_kernel(ref));
    EXPECT_FALSE(h.v0.complete_without_kernel(ref));
    EXPECT_TRUE(h.v0.resources_quiescent());
    PercounterClusterCommand cmd;
    PercounterClusterReturn fin;
    EXPECT_FALSE(h.v0.poll_command(cmd));
    EXPECT_FALSE(h.v0.collect_fin(1, fin));
    const uint32_t next = h.claim(162, 2);
    ASSERT_TRUE(h.v0.publish(next));
    ASSERT_TRUE(h.v0.poll_command(cmd));
    EXPECT_EQ(cmd.entry & UINT32_MAX, 162U);
    EXPECT_EQ(cmd.sequence, 2U);
}

TEST(PercounterClusterScheduler, SimultaneousCandidatesCannotBothAcquireAuthority) {
    Harness h;
    ASSERT_TRUE(h.v0.release());
    std::atomic<uint32_t> arrived{0};
    std::atomic<bool> go{false};
    bool c_won = false, v1_won = false;
    auto before_cas = [&] {
        arrived.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {}
    };
    h.cm.before_cas = before_cas;
    h.v1m.before_cas = before_cas;
    std::thread ct([&] {
        c_won = h.c.try_acquire();
    });
    std::thread vt([&] {
        v1_won = h.v1.try_acquire();
    });
    while (arrived.load(std::memory_order_acquire) != 2) {}
    go.store(true, std::memory_order_release);
    ct.join();
    vt.join();
    EXPECT_NE(c_won, v1_won);
    EXPECT_EQ(h.c.has_authority(), c_won);
    EXPECT_EQ(h.v1.has_authority(), v1_won);
    EXPECT_EQ(
        h.shared.owner.load(), static_cast<uint64_t>(c_won ? PercounterClusterOwner::C : PercounterClusterOwner::V1)
    );
}

TEST(PercounterClusterScheduler, FinPublicationMakesWorkerOutputVisibleToTheConsumingOwner) {
    Harness h;
    const uint32_t ref = h.claim(171, 1, 0);
    ASSERT_TRUE(h.v0.publish(ref));
    uint64_t output = 0;
    std::thread worker([&] {
        PercounterClusterCommand command;
        EXPECT_TRUE(h.c.poll_command(command));
        EXPECT_TRUE(h.c.before_kernel());
        output = UINT64_C(0x123456789abcdef0);
        EXPECT_TRUE(h.c.publish_fin(PercounterClusterFinStatus::NO_ASYNC));
    });
    PercounterClusterReturn fin;
    while (!h.v0.collect_fin(0, fin)) {}
    EXPECT_EQ(output, UINT64_C(0x123456789abcdef0));
    EXPECT_TRUE(fin.is_last);
    EXPECT_TRUE(h.v0.commit_fin(0, fin.sequence));
    EXPECT_TRUE(h.v0.retire(ref));
    worker.join();
}

TEST(PercounterClusterScheduler, KernelEntryCannotAbandonAnUncommittedClaim) {
    Harness h;
    const uint32_t ref = h.claim(181, 2);
    ASSERT_TRUE(h.v0.publish(ref));
    PercounterClusterCommand command;
    ASSERT_TRUE(h.v0.poll_command(command));
    const uint32_t future = h.v0.reserve(182, 1, 0);
    ASSERT_NE(future, PERCOUNTER_CLUSTER_INVALID_REF);
    EXPECT_FALSE(h.v0.before_kernel());
    EXPECT_TRUE(h.v0.has_authority());
    ASSERT_TRUE(h.v0.rollback(future));
    EXPECT_TRUE(h.v0.before_kernel());
    EXPECT_FALSE(h.v0.has_authority());
}

}  // namespace
