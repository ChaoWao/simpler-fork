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

#include "async_completion_types.h"
#include "scheduler/async_completion.h"
#include "scheduler/completion_state.h"
#include "scheduler/deferred_completion_types.h"
#include "scheduler/gm.h"
#include "scheduler/scheduler_graph.h"

namespace simpler::percounter {

inline __aicore__ void percounter_deferred_barrier() {
    percounter_scalar_memory_issue(ProfilePhase::DsbAll, 1, []() __aicore__ {
        dsb((mem_dsb_t)0);
    });
}

inline __aicore__ void percounter_deferred_writeback(__gm__ void *address, uint64_t bytes) {
    (void)address;
    for (uint64_t offset = 0; offset < bytes; offset += 64) {
        percounter_scalar_memory_issue(ProfilePhase::DcciOut, 1, [=]() __aicore__ {
            dcci(reinterpret_cast<__gm__ uint8_t *>(address) + offset, SINGLE_CACHE_LINE, CACHELINE_OUT);
        });
    }
    percounter_deferred_barrier();
}

inline __aicore__ void percounter_deferred_observe(__gm__ void *address, uint64_t bytes) {
    (void)address;
    for (uint64_t offset = 0; offset < bytes; offset += 64) {
        percounter_scalar_memory_issue(ProfilePhase::DcciSingle, 1, [=]() __aicore__ {
            dcci(reinterpret_cast<__gm__ uint8_t *>(address) + offset, SINGLE_CACHE_LINE);
        });
    }
    percounter_deferred_barrier();
}

inline __aicore__ bool percounter_deferred_word_address(uint64_t address) {
    return address != 0 && address % alignof(uint64_t) == 0;
}

inline __aicore__ SchedulerGraphResult percounter_deferred_registration_error(int32_t error) {
    if (error == SIMPLER_ERROR_ASYNC_WAIT_OVERFLOW) return SchedulerGraphResult::ASYNC_WAIT_OVERFLOW;
    if (error == SIMPLER_ERROR_ASYNC_COMPLETION_INVALID) return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
    return SchedulerGraphResult::ASYNC_REGISTRATION_FAILED;
}

/** Only the initializing publisher may mutate a node while linking indexes. */
inline __aicore__ void
percounter_deferred_push_index(__gm__ PercounterDeferredGroup *group, __gm__ uint64_t *head, uint64_t ref, bool shell) {
    uint64_t expected = percounter_gm_read(*head);
    for (;;) {
        if (shell) group->next_shell = expected;
        else group->next = expected;
        percounter_deferred_writeback(group, offsetof(PercounterDeferredGroup, entries));
        const uint64_t observed = percounter_gm_compare_exchange(*head, expected, ref);
        if (observed == expected) return;
        expected = observed;
    }
}

/** The tag is sticky; only actual defer publication reaches this CAS loop. */
inline __aicore__ void percounter_deferred_mark(__gm__ uint64_t *completion) {
    uint64_t expected = percounter_gm_read(*completion);
    while (!percounter_completion_has_defer(expected)) {
        const uint64_t observed =
            percounter_gm_compare_exchange(*completion, expected, percounter_completion_mark_defer(expected));
        if (observed == expected) return;
        expected = observed;
    }
}

/**
 * Transfer an actual returned invocation into persistent storage. The caller
 * holds cluster owner throughout adoption and commits the resulting ref before
 * any handoff. No-defer FIN handling must skip this remote-scratch path.
 * Both index links are stable before Unclaimed is released; the publisher
 * performs no cached write to this node after that release.
 */
template <typename Control>
inline __aicore__ SchedulerGraphResult percounter_deferred_adopt(
    __gm__ Control *control, __gm__ PercounterDeferredGroup *pool, uint64_t capacity,
    __gm__ pto2::resident::AsyncGroupData *scratch, __gm__ DeferredCompletionEntry *entries,
    uint64_t completion_address, __gm__ uint64_t *component_head, uint64_t shell_completion_address,
    __gm__ uint64_t *shell_head, bool withdraw_mix_debt, uint64_t *out_ref
) {
    if (out_ref == nullptr) return SchedulerGraphResult::INVALID_ARGUMENTS;
    *out_ref = 0;
    if (control == nullptr || pool == nullptr || scratch == nullptr || entries == nullptr ||
        component_head == nullptr || capacity == 0 || capacity == UINT64_MAX ||
        !percounter_deferred_word_address(completion_address) ||
        !percounter_deferred_word_address(reinterpret_cast<uint64_t>(component_head)) ||
        ((shell_completion_address == 0) != (shell_head == nullptr)) ||
        (shell_completion_address != 0 &&
         (!percounter_deferred_word_address(shell_completion_address) ||
          !percounter_deferred_word_address(reinterpret_cast<uint64_t>(shell_head)) ||
          shell_completion_address == completion_address || shell_head == component_head)))
        return SchedulerGraphResult::INVALID_ARGUMENTS;

    percounter_deferred_observe(scratch, sizeof(*scratch));
    const uint32_t count = scratch->count;
    const int32_t error = scratch->error_code;
    if (error != SIMPLER_ERROR_NONE) return percounter_deferred_registration_error(error);
    if (scratch->reservation_limit != 0 || count > PERCOUNTER_DEFERRED_CONDITION_CAPACITY ||
        count > scratch->capacity || scratch->run_epoch != percounter_gm_read(control->async_run_epoch) ||
        scratch->state == pto2::resident::AsyncPublicationState::FAILED)
        return SchedulerGraphResult::ASYNC_REGISTRATION_FAILED;
    if (count == 0) return SchedulerGraphResult::OK;

    percounter_deferred_observe(entries, count * sizeof(*entries));
    for (uint32_t i = 0; i < count; ++i) {
        const auto &entry = entries[i];
        const CompletionToken token{
            entry.addr, entry.expected_value, entry.engine, entry.completion_type, entry.backend_cookie
        };
        if (entry._pad != 0 || !percounter_completion_token_valid(token))
            return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
        if (entry.completion_type == COMPLETION_TYPE_CCU) {
            for (uint32_t previous = 0; previous < i; ++previous) {
                if (entries[previous].completion_type == COMPLETION_TYPE_CCU && entries[previous].addr == entry.addr)
                    return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
            }
        }
    }

    const uint64_t index = percounter_gm_fetch_add(control->async_registered, uint64_t{1});
    if (index >= capacity) return SchedulerGraphResult::ASYNC_REGISTRATION_FAILED;
    const uint64_t ref = index + 1;
    auto *group = &pool[index];
    group->next = 0;
    group->next_shell = 0;
    group->next_owned = 0;
    group->pending_mask = (uint64_t{1} << count) - 1;
    group->completion_address = completion_address;
    group->shell_completion_address = shell_completion_address;
    group->claim_state = static_cast<uint64_t>(PercounterDeferredState::Initializing);
    group->condition_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        auto *target = &group->entries[i];
        const auto *source = &entries[i];
        target->addr = source->addr;
        target->backend_cookie = source->backend_cookie;
        target->expected_value = source->expected_value;
        target->engine = source->engine;
        target->completion_type = source->completion_type;
        target->_pad = 0;
    }
    percounter_deferred_writeback(group, offsetof(PercounterDeferredGroup, entries) + count * sizeof(*entries));

    auto *completion = reinterpret_cast<__gm__ uint64_t *>(completion_address);
    auto *shell_completion = reinterpret_cast<__gm__ uint64_t *>(shell_completion_address);
    if (withdraw_mix_debt) {
        percounter_gm_fetch_add_issue(*completion, ~uint64_t{0});
        if (shell_completion != nullptr) percounter_gm_fetch_add_issue(*shell_completion, ~uint64_t{0});
        percounter_deferred_barrier();
    }
    percounter_deferred_push_index(group, component_head, ref, false);
    if (shell_head != nullptr) percounter_deferred_push_index(group, shell_head, ref, true);
    percounter_deferred_barrier();
    percounter_gm_store(group->claim_state, static_cast<uint64_t>(PercounterDeferredState::Unclaimed));
    percounter_deferred_barrier();
    percounter_deferred_mark(completion);
    if (shell_completion != nullptr) percounter_deferred_mark(shell_completion);
    percounter_deferred_barrier();

    scratch->count = 0;
    percounter_deferred_writeback(scratch, 64);
    *out_ref = ref;
    return SchedulerGraphResult::OK;
}

/** Caller commits its successor's new owned-head in SSBUF before handoff. */
inline __aicore__ SchedulerGraphResult
percounter_deferred_try_claim(__gm__ PercounterDeferredGroup *group, uint64_t owned_head, bool *out_claimed) {
    if (out_claimed == nullptr) return SchedulerGraphResult::INVALID_ARGUMENTS;
    *out_claimed = false;
    if (group == nullptr) return SchedulerGraphResult::INVALID_ARGUMENTS;
    const uint64_t observed = percounter_gm_compare_exchange(
        group->claim_state, static_cast<uint64_t>(PercounterDeferredState::Unclaimed),
        static_cast<uint64_t>(PercounterDeferredState::Claimed)
    );
    if (observed == static_cast<uint64_t>(PercounterDeferredState::Unclaimed)) {
        percounter_deferred_observe(group, offsetof(PercounterDeferredGroup, entries));
        group->next_owned = owned_head;
        percounter_deferred_writeback(group, offsetof(PercounterDeferredGroup, entries));
        *out_claimed = true;
        return SchedulerGraphResult::OK;
    }
    if (observed == static_cast<uint64_t>(PercounterDeferredState::Initializing) ||
        observed == static_cast<uint64_t>(PercounterDeferredState::Claimed) ||
        observed == static_cast<uint64_t>(PercounterDeferredState::Settled))
        return SchedulerGraphResult::OK;
    return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
}

/**
 * Only the winning successor's cluster may call this for a Claimed node.
 * A Claimed word alone is not ownership proof; that proof is the caller's
 * committed SSBUF responsibility ref. A pending group stays Claimed.
 */
template <typename Control>
inline __aicore__ SchedulerGraphResult percounter_deferred_poll_owned(
    __gm__ Control *control, __gm__ PercounterDeferredGroup *pool, uint64_t capacity, uint64_t ref, bool *out_settled
) {
    if (out_settled == nullptr) return SchedulerGraphResult::INVALID_ARGUMENTS;
    *out_settled = false;
    if (control == nullptr || pool == nullptr || ref == 0 || ref > capacity)
        return SchedulerGraphResult::INVALID_ARGUMENTS;
    auto *group = &pool[ref - 1];
    const uint64_t state = percounter_gm_read(group->claim_state);
    if (state == static_cast<uint64_t>(PercounterDeferredState::Settled)) {
        *out_settled = true;
        return SchedulerGraphResult::OK;
    }
    percounter_deferred_observe(group, sizeof(*group));
    // A bypass sample may lag. The fresh header, not that earlier sample,
    // decides whether this responsibility still needs its contribution.
    const uint64_t current_state = group->claim_state;
    if (current_state == static_cast<uint64_t>(PercounterDeferredState::Settled)) {
        *out_settled = true;
        return SchedulerGraphResult::OK;
    }
    if (current_state != static_cast<uint64_t>(PercounterDeferredState::Claimed))
        return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
    const uint64_t count = group->condition_count;
    const uint64_t pending = group->pending_mask;
    if (count == 0 || count > PERCOUNTER_DEFERRED_CONDITION_CAPACITY ||
        (pending & ~((uint64_t{1} << count) - 1)) != 0 ||
        !percounter_deferred_word_address(group->completion_address) ||
        (group->shell_completion_address != 0 && (!percounter_deferred_word_address(group->shell_completion_address) ||
                                                  group->shell_completion_address == group->completion_address)))
        return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;

    uint64_t remaining = pending;
    while (remaining != 0) {
        const uint32_t i = static_cast<uint32_t>(__builtin_ctzll(remaining));
        const auto &entry = group->entries[i];
        const CompletionToken token{
            entry.addr, entry.expected_value, entry.engine, entry.completion_type, entry.backend_cookie
        };
        if (entry._pad != 0 || !percounter_completion_token_valid(token))
            return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
        const CompletionPollResult sample = percounter_poll_valid_completion(token);
        if (sample.state == CompletionPollState::FAILED) return SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
        if (sample.state != CompletionPollState::READY) break;
        remaining &= remaining - 1;
    }
    if (remaining != pending) {
        group->pending_mask = remaining;
        percounter_deferred_writeback(group, offsetof(PercounterDeferredGroup, entries));
    }
    if (remaining != 0) return SchedulerGraphResult::OK;

    percounter_deferred_barrier();
    percounter_gm_fetch_add_issue(*reinterpret_cast<__gm__ uint64_t *>(group->completion_address), uint64_t{1});
    if (group->shell_completion_address != 0) {
        percounter_gm_fetch_add_issue(
            *reinterpret_cast<__gm__ uint64_t *>(group->shell_completion_address), uint64_t{1}
        );
    }
    percounter_deferred_barrier();
    percounter_gm_fetch_add_issue(control->async_settled, uint64_t{1});
    percounter_deferred_barrier();
    percounter_gm_store(group->claim_state, static_cast<uint64_t>(PercounterDeferredState::Settled));
    percounter_deferred_barrier();
    *out_settled = true;
    return SchedulerGraphResult::OK;
}

}  // namespace simpler::percounter
