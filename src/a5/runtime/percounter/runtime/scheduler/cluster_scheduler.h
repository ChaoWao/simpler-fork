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

#include "cluster_scheduler_types.h"

#if defined(__CCE_AICORE__)
#define PERCOUNTER_CLUSTER_INLINE inline __aicore__
#else
#define PERCOUNTER_CLUSTER_INLINE inline
#endif

namespace simpler::percounter {

// Memory provides owner_load/cas/store, load/store(word), acquire/release.
// All SSBUF updates have one writer and use no read-modify-write primitive.
template <typename Memory>
class PercounterClusterScheduler {
public:
    PERCOUNTER_CLUSTER_INLINE PercounterClusterScheduler(Memory &memory, PercounterClusterOwner lane) :
        memory_(memory),
        lane_(lane) {}
    PercounterClusterScheduler(const PercounterClusterScheduler &) = delete;
    PercounterClusterScheduler &operator=(const PercounterClusterScheduler &) = delete;

    // Workers and old-run accesses are quiescent until initialization publishes.
    PERCOUNTER_CLUSTER_INLINE bool initialize_quiescent() {
        if (lane_ != PercounterClusterOwner::V0 || initialized_ || holding_) return false;
        for (uint32_t word = 0; word < PERCOUNTER_CLUSTER_WORDS; ++word)
            memory_.store(word, 0);
        set_header(offsetof(PercounterClusterHeader, free_mask), PERCOUNTER_CLUSTER_RECORD_MASK);
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane) {
            set_header(offsetof(PercounterClusterHeader, resource_head) + lane * sizeof(uint32_t), invalid());
            set_resource(lane, offsetof(PercounterClusterResource, running), invalid());
            set_resource(lane, offsetof(PercounterClusterResource, next), invalid());
            store(command_offset(lane, offsetof(PercounterClusterCommandSlot, record)), invalid());
        }
        memory_.release();
        memory_.owner_store(static_cast<uint32_t>(lane_));
        initialized_ = true;
        holding_ = true;
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool try_acquire() {
        if (!valid_lane() || holding_) return false;
        const auto none = static_cast<uint32_t>(PercounterClusterOwner::NONE);
        if (memory_.owner_load() != none) return false;
        if (memory_.owner_cas(none, static_cast<uint32_t>(lane_)) != none) return false;
        memory_.acquire();
        holding_ = true;
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool release() {
        if (!holding_ || transaction_open()) return false;
        memory_.release();
        holding_ = false;
        memory_.owner_store(static_cast<uint32_t>(PercounterClusterOwner::NONE));
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool has_authority() const { return holding_; }
    PERCOUNTER_CLUSTER_INLINE bool worker_active() const { return accepted_; }

    PERCOUNTER_CLUSTER_INLINE uint32_t running_ref(uint32_t lane) {
        return lane < PERCOUNTER_CLUSTER_LANES ? resource(lane, offsetof(PercounterClusterResource, running)) :
                                                 invalid();
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t next_ref(uint32_t lane) {
        return lane < PERCOUNTER_CLUSTER_LANES ? resource(lane, offsetof(PercounterClusterResource, next)) : invalid();
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t execution_mask(uint32_t ref) { return valid_ref(ref) ? status(ref) & 0xffU : 0; }
    PERCOUNTER_CLUSTER_INLINE uint32_t returned_mask(uint32_t ref) {
        return valid_ref(ref) ? (status(ref) >> 8) & 0xffU : 0;
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t execution_flags(uint32_t ref) {
        return valid_ref(ref) ? (status(ref) >> 16) & 0xffU : 0;
    }
    PERCOUNTER_CLUSTER_INLINE uint64_t entry(uint32_t ref) {
        return valid_ref(ref) ? load64(record_offset(ref, offsetof(PercounterClusterExecution, entry))) : 0;
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t sequence(uint32_t ref) {
        return valid_ref(ref) ? record(ref, offsetof(PercounterClusterExecution, sequence)) : 0;
    }

    // The reservation is committed or rolled back before any owner handoff.
    PERCOUNTER_CLUSTER_INLINE uint32_t reserve(uint32_t task, uint32_t mask, uint32_t catalog) {
        if (!holding_ || transaction_open() || mask == 0 || (mask & ~7U) != 0 || catalog >= 2 || task == invalid() ||
            header(offsetof(PercounterClusterHeader, sequence)) == UINT32_MAX)
            return invalid();
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane) {
            if ((mask & (1U << lane)) == 0) continue;
            if (next_ref(lane) != invalid() || resource_holds_task(lane, task, invalid())) return invalid();
        }
        const uint32_t free = header(offsetof(PercounterClusterHeader, free_mask)) & PERCOUNTER_CLUSTER_RECORD_MASK;
        if (free == 0) return invalid();
        const uint32_t ref = static_cast<uint32_t>(__builtin_ctz(free));
        set_header(offsetof(PercounterClusterHeader, free_mask), free & ~(1U << ref));
        for (uint32_t byte = 0; byte < sizeof(PercounterClusterExecution); byte += sizeof(uint32_t))
            set_record(ref, byte, 0);
        store64(record_offset(ref, offsetof(PercounterClusterExecution, entry)), task);
        set_record(ref, offsetof(PercounterClusterExecution, claim_index), invalid());
        set_record(ref, offsetof(PercounterClusterExecution, sync_index), invalid());
        set_record(ref, offsetof(PercounterClusterExecution, status), mask | (catalog << 24));
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (mask & (1U << lane)) set_resource(lane, offsetof(PercounterClusterResource, next), ref);
        uncommitted_ref_ = ref;
        return ref;
    }

    PERCOUNTER_CLUSTER_INLINE bool commit_claim(
        uint32_t ref, uint64_t claimed_entry, uint32_t rank, uint32_t claim_index = UINT32_MAX,
        uint32_t sync_index = UINT32_MAX
    ) {
        if (!holding_ || uncommitted_ref_ != ref || !valid_ref(ref)) return false;
        const uint32_t mask = execution_mask(ref);
        const uint32_t task = static_cast<uint32_t>(claimed_entry);
        const uint32_t previous = header(offsetof(PercounterClusterHeader, sequence));
        if (previous == UINT32_MAX || task == invalid()) return false;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if ((mask & (1U << lane)) && (next_ref(lane) != ref || resource_holds_task(lane, task, ref))) return false;
        store64(record_offset(ref, offsetof(PercounterClusterExecution, entry)), claimed_entry);
        set_record(ref, offsetof(PercounterClusterExecution, rank), rank);
        set_record(ref, offsetof(PercounterClusterExecution, claim_index), claim_index);
        set_record(ref, offsetof(PercounterClusterExecution, sync_index), sync_index);
        set_record(ref, offsetof(PercounterClusterExecution, status), status(ref) | (PERCOUNTER_CLUSTER_CLAIMED << 16));
        set_record(ref, offsetof(PercounterClusterExecution, sequence), previous + 1);
        set_header(offsetof(PercounterClusterHeader, sequence), previous + 1);
        uncommitted_ref_ = invalid();
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool rollback(uint32_t ref) {
        if (!holding_ || uncommitted_ref_ != ref || !valid_ref(ref)) return false;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (next_ref(lane) == ref) set_resource(lane, offsetof(PercounterClusterResource, next), invalid());
        reclaim(ref);
        uncommitted_ref_ = invalid();
        return true;
    }

    // The caller has verified metadata, dependencies and any global claim gate.
    PERCOUNTER_CLUSTER_INLINE bool mark_ready(uint32_t ref) {
        if (!holding_ || transaction_open() || !live(ref) || (execution_flags(ref) & PERCOUNTER_CLUSTER_CLAIMED) == 0 ||
            (execution_flags(ref) & PERCOUNTER_CLUSTER_DISPATCHED) != 0)
            return false;
        set_record(
            ref, offsetof(PercounterClusterExecution, status),
            status(ref) | (PERCOUNTER_CLUSTER_DEPENDENCIES_READY << 16)
        );
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool can_dispatch(uint32_t ref) {
        if (!holding_ || transaction_open() || !live(ref)) return false;
        const uint32_t flags = execution_flags(ref);
        if ((flags & (PERCOUNTER_CLUSTER_CLAIMED | PERCOUNTER_CLUSTER_DEPENDENCIES_READY)) !=
                (PERCOUNTER_CLUSTER_CLAIMED | PERCOUNTER_CLUSTER_DEPENDENCIES_READY) ||
            (flags & PERCOUNTER_CLUSTER_DISPATCHED) != 0)
            return false;
        const uint32_t mask = execution_mask(ref);
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if ((mask & (1U << lane)) && (next_ref(lane) != ref || running_ref(lane) != invalid())) return false;
        return mask != 0;
    }

    PERCOUNTER_CLUSTER_INLINE bool publish(uint32_t ref) {
        if (!can_dispatch(ref)) return false;
        const uint32_t mask = execution_mask(ref);
        const uint32_t command_sequence = sequence(ref);
        const bool ordinary_vector = mask == 2 || mask == 4;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane) {
            if ((mask & (1U << lane)) == 0) continue;
            set_resource(lane, offsetof(PercounterClusterResource, running), ref);
            set_resource(lane, offsetof(PercounterClusterResource, next), invalid());
            store(command_offset(lane, offsetof(PercounterClusterCommandSlot, record)), ref);
            store(
                command_offset(lane, offsetof(PercounterClusterCommandSlot, source_slot)), ordinary_vector ? 1 : lane
            );
            store(
                command_offset(lane, offsetof(PercounterClusterCommandSlot, flags)),
                resource(lane, offsetof(PercounterClusterResource, flags))
            );
        }
        set_record(
            ref, offsetof(PercounterClusterExecution, status), status(ref) | (PERCOUNTER_CLUSTER_DISPATCHED << 16)
        );
        memory_.release();
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (mask & (1U << lane))
                store(command_offset(lane, offsetof(PercounterClusterCommandSlot, sequence)), command_sequence);
        return true;
    }

    // The caller has submitted the dependency-only or skipped task's GM
    // contribution. Its admission reference is released without a worker FIN.
    PERCOUNTER_CLUSTER_INLINE bool complete_without_kernel(uint32_t ref) {
        if (!can_dispatch(ref)) return false;
        memory_.release();
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (next_ref(lane) == ref) set_resource(lane, offsetof(PercounterClusterResource, next), invalid());
        reclaim(ref);
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool poll_command(PercounterClusterCommand &command) {
        if (!valid_lane() || accepted_) return false;
        const uint32_t lane = static_cast<uint32_t>(lane_);
        const uint32_t command_sequence = load(command_offset(lane, offsetof(PercounterClusterCommandSlot, sequence)));
        if (command_sequence == 0 || command_sequence == accepted_sequence_) return false;
        memory_.acquire();
        const uint32_t ref = load(command_offset(lane, offsetof(PercounterClusterCommandSlot, record)));
        if (!valid_ref(ref) || sequence(ref) != command_sequence || (execution_mask(ref) & (1U << lane)) == 0)
            return false;
        command.entry = entry(ref);
        command.record = ref;
        command.sequence = command_sequence;
        command.source_slot = load(command_offset(lane, offsetof(PercounterClusterCommandSlot, source_slot)));
        command.flags = load(command_offset(lane, offsetof(PercounterClusterCommandSlot, flags)));
        accepted_sequence_ = command_sequence;
        accepted_record_ = ref;
        accepted_ = true;
        entered_ = false;
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool before_kernel() {
        if (!accepted_ || entered_ || sequence(accepted_record_) != accepted_sequence_) return false;
        if (holding_ && !release()) return false;
        entered_ = true;
        return true;
    }

    // Output ordering and any scratch writeback precede FIN publication.
    PERCOUNTER_CLUSTER_INLINE bool publish_fin(uint32_t result, uint64_t detail = 0) {
        if (!accepted_ ||
            (result & ~(PERCOUNTER_CLUSTER_FIN_STATUS_MASK | PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED)) != 0 ||
            (result & PERCOUNTER_CLUSTER_FIN_STATUS_MASK) > static_cast<uint32_t>(PercounterClusterFinStatus::ERROR))
            return false;
        const uint32_t lane = static_cast<uint32_t>(lane_);
        if (load(command_offset(lane, offsetof(PercounterClusterCommandSlot, sequence))) != accepted_sequence_ ||
            load(fin_offset(lane, offsetof(PercounterClusterFinSlot, sequence))) == accepted_sequence_)
            return false;
        store64(fin_offset(lane, offsetof(PercounterClusterFinSlot, detail)), detail);
        store(fin_offset(lane, offsetof(PercounterClusterFinSlot, status)), result);
        memory_.release();
        store(fin_offset(lane, offsetof(PercounterClusterFinSlot, sequence)), accepted_sequence_);
        accepted_ = false;
        entered_ = false;
        accepted_record_ = invalid();
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool publish_fin(PercounterClusterFinStatus result, uint64_t detail = 0) {
        return publish_fin(static_cast<uint32_t>(result), detail);
    }

    // The matching scratch must be adopted before commit_fin. No handoff occurs
    // during this interval. A last FIN retains its reference until retire.
    PERCOUNTER_CLUSTER_INLINE bool collect_fin(uint32_t lane, PercounterClusterReturn &returned) {
        if (!holding_ || transaction_open() || lane >= PERCOUNTER_CLUSTER_LANES) return false;
        const uint32_t ref = running_ref(lane);
        if (!valid_ref(ref)) return false;
        const uint32_t command_sequence = sequence(ref);
        if (command_sequence == 0 ||
            resource(lane, offsetof(PercounterClusterResource, consumed_sequence)) == command_sequence ||
            load(fin_offset(lane, offsetof(PercounterClusterFinSlot, sequence))) != command_sequence)
            return false;
        memory_.acquire();
        returned.detail = load64(fin_offset(lane, offsetof(PercounterClusterFinSlot, detail)));
        returned.record = ref;
        returned.sequence = command_sequence;
        returned.lane = lane;
        returned.status = load(fin_offset(lane, offsetof(PercounterClusterFinSlot, status)));
        returned.is_last = (returned_mask(ref) | (1U << lane)) == execution_mask(ref);
        consuming_lane_ = lane;
        consuming_ref_ = ref;
        consuming_sequence_ = command_sequence;
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool commit_fin(uint32_t lane, uint32_t command_sequence) {
        if (!holding_ || consuming_lane_ != lane || consuming_sequence_ != command_sequence ||
            running_ref(lane) != consuming_ref_ || sequence(consuming_ref_) != command_sequence)
            return false;
        const uint32_t ref = consuming_ref_;
        const uint32_t returned = returned_mask(ref) | (1U << lane);
        const bool last = returned == execution_mask(ref);
        memory_.release();
        set_record(ref, offsetof(PercounterClusterExecution, status), (status(ref) & ~0xff00U) | (returned << 8));
        set_resource(lane, offsetof(PercounterClusterResource, consumed_sequence), command_sequence);
        if (last) retiring_ref_ = ref;
        else set_resource(lane, offsetof(PercounterClusterResource, running), invalid());
        consuming_ref_ = invalid();
        consuming_lane_ = invalid();
        consuming_sequence_ = 0;
        return true;
    }

    // All required GM completion contributions and defer publication precede
    // retirement. The last resource reference pins the record until this call.
    PERCOUNTER_CLUSTER_INLINE bool retire(uint32_t ref) {
        if (!holding_ || retiring_ref_ != ref || !live(ref) || returned_mask(ref) != execution_mask(ref)) return false;
        memory_.release();
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (running_ref(lane) == ref) set_resource(lane, offsetof(PercounterClusterResource, running), invalid());
        reclaim(ref);
        retiring_ref_ = invalid();
        return true;
    }

    PERCOUNTER_CLUSTER_INLINE bool resources_quiescent() {
        if (!holding_ || transaction_open()) return false;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (running_ref(lane) != invalid() || next_ref(lane) != invalid()) return false;
        return header(offsetof(PercounterClusterHeader, free_mask)) == PERCOUNTER_CLUSTER_RECORD_MASK;
    }

private:
    PERCOUNTER_CLUSTER_INLINE static constexpr uint32_t invalid() { return PERCOUNTER_CLUSTER_INVALID_REF; }
    PERCOUNTER_CLUSTER_INLINE bool valid_lane() const {
        return static_cast<uint32_t>(lane_) < PERCOUNTER_CLUSTER_LANES;
    }
    PERCOUNTER_CLUSTER_INLINE static constexpr bool valid_ref(uint32_t ref) { return ref < PERCOUNTER_CLUSTER_RECORDS; }
    PERCOUNTER_CLUSTER_INLINE bool transaction_open() const {
        return uncommitted_ref_ != invalid() || consuming_ref_ != invalid() || retiring_ref_ != invalid();
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t load(size_t byte) { return memory_.load(static_cast<uint32_t>(byte / 4)); }
    PERCOUNTER_CLUSTER_INLINE void store(size_t byte, uint32_t value) {
        memory_.store(static_cast<uint32_t>(byte / 4), value);
    }
    PERCOUNTER_CLUSTER_INLINE uint64_t load64(size_t byte) {
        const uint64_t low = load(byte);
        return low | (static_cast<uint64_t>(load(byte + 4)) << 32);
    }
    PERCOUNTER_CLUSTER_INLINE void store64(size_t byte, uint64_t value) {
        store(byte, static_cast<uint32_t>(value));
        store(byte + 4, static_cast<uint32_t>(value >> 32));
    }
    PERCOUNTER_CLUSTER_INLINE static constexpr size_t resource_offset(uint32_t lane, size_t field) {
        return offsetof(PercounterClusterSchedulerRegion, resources) + lane * sizeof(PercounterClusterResource) + field;
    }
    PERCOUNTER_CLUSTER_INLINE static constexpr size_t command_offset(uint32_t lane, size_t field) {
        return offsetof(PercounterClusterSchedulerRegion, commands) + lane * sizeof(PercounterClusterCommandSlot) +
               field;
    }
    PERCOUNTER_CLUSTER_INLINE static constexpr size_t fin_offset(uint32_t lane, size_t field) {
        return offsetof(PercounterClusterSchedulerRegion, fins) + lane * sizeof(PercounterClusterFinSlot) + field;
    }
    PERCOUNTER_CLUSTER_INLINE static constexpr size_t record_offset(uint32_t ref, size_t field) {
        return offsetof(PercounterClusterSchedulerRegion, execution) + ref * sizeof(PercounterClusterExecution) + field;
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t header(size_t field) {
        return load(offsetof(PercounterClusterSchedulerRegion, header) + field);
    }
    PERCOUNTER_CLUSTER_INLINE void set_header(size_t field, uint32_t value) {
        store(offsetof(PercounterClusterSchedulerRegion, header) + field, value);
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t resource(uint32_t lane, size_t field) {
        return load(resource_offset(lane, field));
    }
    PERCOUNTER_CLUSTER_INLINE void set_resource(uint32_t lane, size_t field, uint32_t value) {
        store(resource_offset(lane, field), value);
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t record(uint32_t ref, size_t field) { return load(record_offset(ref, field)); }
    PERCOUNTER_CLUSTER_INLINE void set_record(uint32_t ref, size_t field, uint32_t value) {
        store(record_offset(ref, field), value);
    }
    PERCOUNTER_CLUSTER_INLINE uint32_t status(uint32_t ref) {
        return record(ref, offsetof(PercounterClusterExecution, status));
    }
    PERCOUNTER_CLUSTER_INLINE bool live(uint32_t ref) {
        return valid_ref(ref) && (header(offsetof(PercounterClusterHeader, free_mask)) & (1U << ref)) == 0;
    }
    PERCOUNTER_CLUSTER_INLINE bool resource_holds_task(uint32_t lane, uint32_t task, uint32_t exclude) {
        const uint32_t refs[2]{running_ref(lane), next_ref(lane)};
        for (uint32_t ref : refs)
            if (ref != exclude && valid_ref(ref) && static_cast<uint32_t>(entry(ref)) == task) return true;
        return false;
    }
    PERCOUNTER_CLUSTER_INLINE void reclaim(uint32_t ref) {
        set_record(ref, offsetof(PercounterClusterExecution, status), 0);
        set_record(ref, offsetof(PercounterClusterExecution, sequence), 0);
        set_header(
            offsetof(PercounterClusterHeader, free_mask),
            header(offsetof(PercounterClusterHeader, free_mask)) | (1U << ref)
        );
    }

    Memory &memory_;
    PercounterClusterOwner lane_;
    bool holding_{false};
    bool initialized_{false};
    bool accepted_{false};
    bool entered_{false};
    uint32_t accepted_sequence_{0};
    uint32_t accepted_record_{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t uncommitted_ref_{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t consuming_lane_{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t consuming_ref_{PERCOUNTER_CLUSTER_INVALID_REF};
    uint32_t consuming_sequence_{0};
    uint32_t retiring_ref_{PERCOUNTER_CLUSTER_INVALID_REF};
};

}  // namespace simpler::percounter

#undef PERCOUNTER_CLUSTER_INLINE
