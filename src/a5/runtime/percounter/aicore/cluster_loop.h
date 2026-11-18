/*
 * Copyright (c) PyPTO Contributors.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 * -----------------------------------------------------------------------------------------------------------
 */
#pragma once

#include "scheduler/cluster_scheduler_memory.h"
#include "deferred_groups.h"

namespace simpler::percounter {
namespace {

// Included after lane_loop.h defines the worker's payload, task and profiling helpers.
class PercounterClusterLane {
public:
    __aicore__ explicit PercounterClusterLane(const LaneConfig &cfg) :
        cfg_(cfg),
        memory_(cfg.cluster_owner, cfg.ssbuf),
        scheduler_(memory_, static_cast<PercounterClusterOwner>(cfg.cluster_lane)) {}

    __aicore__ bool done() const { return done_; }
    __aicore__ void stop() {
        if (scheduler_.has_authority()) {
            const uint32_t free = read(&cfg_.ssbuf->header.free_mask);
            for (uint32_t ref = 0; ref < PERCOUNTER_CLUSTER_RECORDS; ++ref) {
                if ((free & (1U << ref)) == 0 && task_id(ref) == cfg_.task_count) {
                    scheduler_.mark_ready(ref);
                    scheduler_.complete_without_kernel(ref);
                }
            }
            scheduler_.release();
        }
        done_ = true;
    }

    __aicore__ bool step(bool *executed) {
        if (executed == nullptr) return false;
        *executed = false;
        if (!initialized_) {
            if (cfg_.cluster_lane == 1) {
                if (!scheduler_.initialize_quiescent()) return fail(SchedulerGraphResult::INVALID_ARGUMENTS);
                for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
                    write(&cfg_.ssbuf->header.resource_head[lane], cfg_.control->resource_heads[lane]);
                memory_.release();
                percounter_gm_store(*cfg_.cluster_ready, uint64_t{1});
            } else {
                if (percounter_gm_read(*cfg_.cluster_ready) != 1) return true;
                memory_.acquire();
            }
            initialized_ = true;
        }
        if (read(&cfg_.ssbuf->header.run_flags) & closed_flag) {
            done_ = true;
            return true;
        }
        PercounterClusterCommand command;
        if (scheduler_.poll_command(command) && !run_worker(command, executed)) return false;
        if (scheduler_.has_authority() || scheduler_.try_acquire()) return serve_owner();
        return true;
    }

private:
    static constexpr uint32_t none = PERCOUNTER_CLUSTER_INVALID_REF;
    static constexpr uint32_t closed_flag = 1;
    static constexpr uint32_t scan_budget = 8;

    template <typename T>
    __aicore__ T read(const PERCOUNTER_SSBUF volatile T *field) const {
        return percounter_ssbuf_load_field(field);
    }
    template <typename T>
    __aicore__ void write(PERCOUNTER_SSBUF volatile T *field, T value) {
        percounter_ssbuf_store_field(field, value);
    }
    __aicore__ bool fail(SchedulerGraphResult error, int64_t task = -1, uint64_t detail = 0) {
        percounter_fail(cfg_, error, task, detail);
        return false;
    }
    __aicore__ uint32_t task_id(uint32_t ref) { return static_cast<uint32_t>(scheduler_.entry(ref)); }
    __aicore__ uint32_t worker_id(uint32_t lane) const {
        return lane == 0 ?
                   static_cast<uint32_t>(cfg_.cluster_index) :
                   static_cast<uint32_t>(
                       cfg_.control->cluster_count + cfg_.cluster_index * cfg_.control->aiv_per_cluster + lane - 1
                   );
    }
    __aicore__ bool targets_idle(uint32_t ref) {
        const uint32_t mask = scheduler_.execution_mask(ref);
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if ((mask & (1U << lane)) && (scheduler_.running_ref(lane) != none || scheduler_.next_ref(lane) != ref))
                return false;
        return true;
    }
    __aicore__ bool run_worker(const PercounterClusterCommand &command, bool *executed) {
        SchedulerGraphResult error = SchedulerGraphResult::OK;
        cfg_.local_error = &error;
        PercounterTask task{};
        bool skipped = false;
        bool ok = percounter_resolve(cfg_, percounter_order_task_id(command.entry), &task);
        if (ok)
            ok = percounter_prepare_subtask(
                cfg_, task.id, command.source_slot, percounter_order_block_idx(command.entry),
                percounter_order_block_num(command.entry), cfg_.payloads
            );
        if (ok)
            ok = percounter_run_prepared(
                cfg_, task, cfg_.payloads,
                [&]() __aicore__ {
                    return scheduler_.before_kernel();
                },
                &skipped
            );
        uint32_t status =
            static_cast<uint32_t>(skipped ? PercounterClusterFinStatus::SKIPPED : PercounterClusterFinStatus::NO_ASYNC);
        if (ok) {
            auto *scratch = cfg_.async_group;
            const uint32_t count = scratch->count;
            if (scratch->error_code != SIMPLER_ERROR_NONE || count > PERCOUNTER_ASYNC_CAPACITY ||
                scratch->reservation_limit != 0) {
                error = scratch->error_code == SIMPLER_ERROR_ASYNC_WAIT_OVERFLOW ?
                            SchedulerGraphResult::ASYNC_WAIT_OVERFLOW :
                        scratch->error_code == SIMPLER_ERROR_ASYNC_COMPLETION_INVALID ?
                            SchedulerGraphResult::ASYNC_COMPLETION_INVALID :
                            SchedulerGraphResult::ASYNC_REGISTRATION_FAILED;
                ok = false;
            } else if (count != 0) {
                if (skipped) {
                    error = SchedulerGraphResult::ASYNC_REGISTRATION_FAILED;
                    ok = false;
                } else {
                    for (uint32_t i = 0; i < count; ++i) {
                        const auto &entry = cfg_.async_entries[i];
                        const CompletionToken token{
                            entry.addr, entry.expected_value, entry.engine, entry.completion_type, entry.backend_cookie
                        };
                        if (entry._pad != 0 || !percounter_completion_token_valid(token)) {
                            error = SchedulerGraphResult::ASYNC_COMPLETION_INVALID;
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        dcci(scratch, SINGLE_CACHE_LINE, CACHELINE_OUT);
                        dcci(reinterpret_cast<__gm__ uint8_t *>(scratch) + 64, SINGLE_CACHE_LINE, CACHELINE_OUT);
                        const uint64_t bytes = uint64_t{count} * sizeof(DeferredCompletionEntry);
                        for (uint64_t offset = 0; offset < bytes; offset += 64)
                            dcci(
                                reinterpret_cast<__gm__ uint8_t *>(cfg_.async_entries) + offset, SINGLE_CACHE_LINE,
                                CACHELINE_OUT
                            );
                        dsb((mem_dsb_t)0);
                        status = static_cast<uint32_t>(PercounterClusterFinStatus::HAS_ASYNC);
                    }
                }
            }
        }
        cfg_.local_error = nullptr;
        if (!ok) {
            status = static_cast<uint32_t>(PercounterClusterFinStatus::ERROR);
        } else if (command.single_resource && status != static_cast<uint32_t>(PercounterClusterFinStatus::HAS_ASYNC)) {
            // The output barrier and registration checks precede the worker's contribution.
            percounter_publish_completion(
                cfg_, task,
                percounter_completion_address(
                    cfg_, task.id, percounter_order_block_idx(command.entry), percounter_order_block_num(command.entry)
                ),
                uint64_t{1}
            );
            status = static_cast<uint32_t>(PercounterClusterFinStatus::COMPLETION_PUBLISHED);
        }
        // Payload preparation acquires the data cache, including the owner-reset registration scratch.
        if (command.flags & PERCOUNTER_CLUSTER_RESET_ASYNC) status |= PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED;
        if (!scheduler_.publish_fin(status, static_cast<uint64_t>(error)))
            return fail(SchedulerGraphResult::INVALID_ARGUMENTS, task.id);
        *executed = ok && !skipped;
        percounter_commit_task_timing(cfg_);
        return true;
    }

    __aicore__ __gm__ uint64_t *index_head(uint64_t address) {
        const uint64_t primary = reinterpret_cast<uint64_t>(cfg_.counters);
        if (address >= primary && address - primary < cfg_.task_count * sizeof(PercounterCounter) &&
            (address - primary) % sizeof(PercounterCounter) == 0)
            return &cfg_.counters[(address - primary) / sizeof(PercounterCounter)].async_head;
        const uint64_t sectors = reinterpret_cast<uint64_t>(cfg_.completion_sectors);
        const uint64_t count = cfg_.task_count * PERCOUNTER_COMPLETION_SECTOR_COUNT;
        if (address >= sectors && address - sectors < count * sizeof(PercounterCompletionSector) &&
            (address - sectors) % sizeof(PercounterCompletionSector) == 0)
            return &cfg_.completion_sectors[(address - sectors) / sizeof(PercounterCompletionSector)].async_head;
        return nullptr;
    }
    __aicore__ bool consume_fin(const PercounterClusterReturn &returned) {
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::Aggregate);
        const uint32_t ref = returned.record;
        const uint64_t entry = scheduler_.entry(ref);
        const uint32_t status = returned.status & PERCOUNTER_CLUSTER_FIN_STATUS_MASK;
        const int64_t id = percounter_order_task_id(entry);
        if (status == static_cast<uint32_t>(PercounterClusterFinStatus::ERROR))
            return fail(static_cast<SchedulerGraphResult>(returned.detail), id);
        uint32_t flags = read(&cfg_.ssbuf->resources[returned.lane].flags);
        if (returned.status & PERCOUNTER_CLUSTER_FIN_RESET_OBSERVED) flags &= ~PERCOUNTER_CLUSTER_RESET_ASYNC;
        const bool deferred = status == static_cast<uint32_t>(PercounterClusterFinStatus::HAS_ASYNC);
        const bool published = status == static_cast<uint32_t>(PercounterClusterFinStatus::COMPLETION_PUBLISHED);
        const uint32_t participants = __builtin_popcount(scheduler_.execution_mask(ref));
        const bool single_resource = participants == 1;
        if ((published && (!single_resource || !returned.is_last)) || (single_resource && !deferred && !published))
            return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
        PercounterTask task{};
        uint64_t completion = 0;
        if (deferred || (returned.is_last && !single_resource)) {
            if (!percounter_resolve(cfg_, id, &task)) return false;
            completion = percounter_completion_address(
                cfg_, id, percounter_order_block_idx(entry), percounter_order_block_num(entry)
            );
        }
        if (deferred) {
            ProfileScope seal_scope(cfg_.scalar, ProfilePhase::AsyncSeal);
            const uint32_t worker = worker_id(returned.lane);
            auto *scratch =
                scheduler_state_at<pto2::resident::AsyncGroupData>(cfg_.control, cfg_.control->async_groups_offset) +
                worker;
            auto *entries =
                scheduler_state_at<DeferredCompletionEntry>(cfg_.control, cfg_.control->async_entries_offset) +
                uint64_t{worker} * PERCOUNTER_ASYNC_CAPACITY;
            const uint64_t shell = task.sink ? reinterpret_cast<uint64_t>(&cfg_.counters[task.shell].value) : 0;
            uint64_t group = 0;
            const auto error = percounter_deferred_adopt(
                cfg_.control, cfg_.async_completions, cfg_.async_completion_capacity, scratch, entries, completion,
                index_head(completion), shell, shell == 0 ? nullptr : index_head(shell), !single_resource, &group
            );
            if (error != SchedulerGraphResult::OK) return fail(error, id);
            if (group == 0) return fail(SchedulerGraphResult::ASYNC_REGISTRATION_FAILED, id);
            flags |= PERCOUNTER_CLUSTER_RESET_ASYNC;
        }
        write(&cfg_.ssbuf->resources[returned.lane].flags, flags);
        if (!scheduler_.commit_fin(returned.lane, returned.sequence))
            return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
        if (returned.is_last) {
            if (!single_resource) percounter_publish_completion(cfg_, task, completion, uint64_t{participants});
            if (!scheduler_.retire(ref)) return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
        }
        return true;
    }
    __aicore__ bool service_owned(uint32_t ref) {
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::AsyncPoll);
        auto &record = cfg_.ssbuf->execution[ref];
        const uint64_t head = read(&record.async_owned_head);
        if (head == 0) return true;
        uint64_t cursor = read(&record.async_cursor);
        if (cursor == 0) cursor = head;
        bool settled = false;
        const auto error = percounter_deferred_poll_owned(
            cfg_.control, cfg_.async_completions, cfg_.async_completion_capacity, cursor, &settled
        );
        if (error != SchedulerGraphResult::OK) return fail(error, task_id(ref));
        auto *group = &cfg_.async_completions[cursor - 1];
        percounter_observe_cache_line(group);
        const uint64_t next = group->next_owned;
        uint64_t new_head = head;
        if (settled && cursor == head) {
            new_head = next;
            write(&record.async_owned_head, new_head);
        }
        write(&record.async_cursor, next == 0 ? new_head : next);
        return true;
    }
    __aicore__ bool probe_word(uint64_t address, uint32_t ref, bool *ready) {
        *ready = false;
        const uint64_t sample = percounter_gm_read(*reinterpret_cast<__gm__ uint64_t *>(address));
        if (percounter_counter_published(sample)) {
            *ready = true;
            return true;
        }
        if (!percounter_completion_has_defer(sample)) return true;
        if (!service_owned(ref)) return false;
        auto *head = index_head(address);
        if (head == nullptr) return fail(SchedulerGraphResult::INVALID_TASK_ID, task_id(ref));
        auto &record = cfg_.ssbuf->execution[ref];
        uint64_t cursor = read(&record.async_scan);
        if (cursor == 0) {
            const uint64_t published = percounter_gm_read(*head);
            if (published == read(&cfg_.ssbuf->observed_defer_head[ref])) {
                *ready =
                    percounter_counter_published(percounter_gm_read(*reinterpret_cast<__gm__ uint64_t *>(address)));
                return true;
            }
            write(&cfg_.ssbuf->observed_defer_head[ref], published);
            cursor = published;
        }
        if (cursor != 0) {
            if (cursor > cfg_.async_completion_capacity)
                return fail(SchedulerGraphResult::ASYNC_COMPLETION_INVALID, task_id(ref));
            auto *group = &cfg_.async_completions[cursor - 1];
            if (percounter_gm_read(group->claim_state) ==
                static_cast<uint64_t>(PercounterDeferredState::Initializing)) {
                write(&record.async_scan, cursor);
                return true;
            }
            percounter_observe_cache_line(group);
            const uint64_t current_state = group->claim_state;
            if (current_state == static_cast<uint64_t>(PercounterDeferredState::Initializing)) {
                write(&record.async_scan, cursor);
                return true;
            }
            const uint64_t next = group->shell_completion_address == address ? group->next_shell : group->next;
            bool claimed = false;
            if (current_state == static_cast<uint64_t>(PercounterDeferredState::Unclaimed)) {
                const auto error = percounter_deferred_try_claim(group, read(&record.async_owned_head), &claimed);
                if (error != SchedulerGraphResult::OK) return fail(error, task_id(ref));
            } else if (current_state != static_cast<uint64_t>(PercounterDeferredState::Claimed) &&
                       current_state != static_cast<uint64_t>(PercounterDeferredState::Settled)) {
                return fail(SchedulerGraphResult::ASYNC_COMPLETION_INVALID, task_id(ref));
            }
            if (claimed) {
                write(&record.async_owned_head, cursor);
                if (read(&record.async_cursor) == 0) write(&record.async_cursor, cursor);
            }
            write(&record.async_scan, next);
            if (claimed && !service_owned(ref)) return false;
        }
        *ready = percounter_counter_published(percounter_gm_read(*reinterpret_cast<__gm__ uint64_t *>(address)));
        return true;
    }
    __aicore__ bool probe_reference(uint64_t reference, uint32_t ref, bool *ready) {
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::Fanin);
        auto &record = cfg_.ssbuf->execution[ref];
        uint64_t pending = read(&record.fanin_pending);
        uint64_t other = read(&record.fanin_other);
        if (pending == 0) {
            pending = reference & ~PERCOUNTER_COMPLETION_PARTITIONED;
            other = 0;
            if (reference & PERCOUNTER_COMPLETION_PARTITIONED) {
                const uint64_t task = (pending - reinterpret_cast<uint64_t>(cfg_.counters)) / sizeof(PercounterCounter);
                const uint32_t first =
                    (cfg_.cluster_index + cfg_.cluster_lane) & (PERCOUNTER_COMPLETION_SECTOR_COUNT - 1);
                auto *sectors = cfg_.completion_sectors + task * PERCOUNTER_COMPLETION_SECTOR_COUNT;
                pending = reinterpret_cast<uint64_t>(&sectors[first].value);
                other = reinterpret_cast<uint64_t>(sectors) | PERCOUNTER_COMPLETION_SECTOR_COUNT;
            }
        }
        for (uint32_t component = 0; component < PERCOUNTER_COMPLETION_SECTOR_COUNT; ++component) {
            bool published = false;
            if (!probe_word(pending, ref, &published)) return false;
            if (!published) {
                write(&record.fanin_pending, pending);
                write(&record.fanin_other, other);
                *ready = false;
                return true;
            }
            write(&record.async_scan, uint64_t{0});
            write(&cfg_.ssbuf->observed_defer_head[ref], uint64_t{0});
            const uint64_t remaining = other & (sizeof(PercounterCompletionSector) - 1);
            if (remaining <= 1) {
                write(&record.fanin_pending, uint64_t{0});
                write(&record.fanin_other, uint64_t{0});
                *ready = true;
                return true;
            }
            const uint64_t base = other & ~uint64_t{sizeof(PercounterCompletionSector) - 1};
            const uint64_t next =
                ((pending - base) / sizeof(PercounterCompletionSector) + 1) & (PERCOUNTER_COMPLETION_SECTOR_COUNT - 1);
            pending = base + next * sizeof(PercounterCompletionSector);
            other = base | (remaining - 1);
        }
        return fail(SchedulerGraphResult::INVALID_ARGUMENTS, task_id(ref));
    }

    struct TicketSample {
        uint32_t index{none};
        uint64_t next{0};
    };

    __aicore__ bool node_assigned(const __gm__ PercounterResourceNode &node, TicketSample &sample) {
        if (node.claim_index != none)
            return percounter_gm_read(cfg_.task_claims[node.claim_index].closed_sectors) >=
                   PERCOUNTER_CLAIM_SECTOR_COUNT;
        const auto &piece = cfg_.ordinary_pieces[node.piece_index];
        if (sample.index != piece.ticket_index) {
            sample.index = piece.ticket_index;
            sample.next = percounter_gm_read(cfg_.tickets[sample.index].next);
        }
        return sample.next > node.piece_ordinal;
    }
    __aicore__ void normalize_heads(uint32_t available) {
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::SharedHead);
        TicketSample samples[PERCOUNTER_LIST_COUNT]{};
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane) {
            if ((available & (1U << lane)) == 0) continue;
            uint32_t head = read(&cfg_.ssbuf->header.resource_head[lane]);
            const uint32_t original = head;
            for (uint32_t step = 0; head != none && step < scan_budget; ++step) {
                const auto &node = cfg_.resource_nodes[head];
                if (!node_assigned(node, samples[node.catalog])) break;
                head = node.next[lane];
            }
            if (head != original) write(&cfg_.ssbuf->header.resource_head[lane], head);
        }
        uint32_t cursor = read(&cfg_.ssbuf->header.sync_cursor);
        const uint32_t original = cursor;
        for (uint32_t step = 0; cursor < cfg_.control->sync_cohort_count && step < scan_budget; ++step) {
            const auto &sync = cfg_.sync_boundaries[cursor];
            if (percounter_gm_read(cfg_.task_claims[sync.claim_index].closed_sectors) < PERCOUNTER_CLAIM_SECTOR_COUNT)
                break;
            ++cursor;
        }
        if (cursor != original) write(&cfg_.ssbuf->header.sync_cursor, cursor);
    }
    __aicore__ bool prefix_ready(const __gm__ PercounterSyncBoundary &sync) {
        bool ready = true;
        for (uint32_t catalog = 0; catalog < PERCOUNTER_LIST_COUNT; ++catalog) {
            uint32_t cursor = read(&cfg_.ssbuf->header.prefix_segment[catalog]);
            const uint32_t original = cursor;
            TicketSample sample;
            for (uint32_t step = 0; cursor < sync.prefix_count[catalog] && step < scan_budget; ++step) {
                const uint32_t node = cfg_.projection_catalog[catalog][cursor];
                if (!node_assigned(cfg_.resource_nodes[node], sample)) break;
                ++cursor;
            }
            if (cursor != original) write(&cfg_.ssbuf->header.prefix_segment[catalog], cursor);
            ready = ready && cursor >= sync.prefix_count[catalog];
        }
        return ready;
    }
    __aicore__ uint32_t physical_mask(const __gm__ PercounterResourceNode &node, uint32_t index) {
        if (node.active_mask != 0 && node.active_mask != 2) {
            for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
                if ((node.active_mask & (1U << lane)) && read(&cfg_.ssbuf->header.resource_head[lane]) != index)
                    return 0;
            return node.active_mask;
        }
        for (uint32_t pass = 0; pass < 2; ++pass)
            for (uint32_t choice = 0; choice < 2; ++choice) {
                const uint32_t lane = 2 - choice;
                const uint32_t running = scheduler_.running_ref(lane);
                if (read(&cfg_.ssbuf->header.resource_head[lane]) == index && scheduler_.next_ref(lane) == none &&
                    (running == none || (pass != 0 && task_id(running) != percounter_order_task_id(node.entry))))
                    return 1U << lane;
            }
        return 0;
    }
    __aicore__ uint32_t claim_block(uint32_t index, uint32_t blocks) {
        auto *claim = &cfg_.task_claims[index];
        const uint32_t first = cfg_.cluster_index & (PERCOUNTER_CLAIM_SECTOR_COUNT - 1);
        for (uint32_t offset = 0; offset < PERCOUNTER_CLAIM_SECTOR_COUNT; ++offset) {
            const uint32_t part = (first + offset) & (PERCOUNTER_CLAIM_SECTOR_COUNT - 1);
            const uint32_t count = (blocks + PERCOUNTER_CLAIM_SECTOR_COUNT - 1 - part) / PERCOUNTER_CLAIM_SECTOR_COUNT;
            if (count == 0 || percounter_gm_read(claim->sectors[part].next) >= count) continue;
            const uint64_t ticket = percounter_gm_fetch_add(claim->sectors[part].next, uint64_t{1});
            if (ticket >= count) continue;
            if (ticket + 1 == count) percounter_gm_fetch_add_issue(claim->closed_sectors, uint64_t{1});
            return part + static_cast<uint32_t>(ticket) * PERCOUNTER_CLAIM_SECTOR_COUNT;
        }
        return blocks;
    }
    __aicore__ bool claim_one(uint32_t &claimed) {
        claimed = none;
        uint32_t available = 0;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (scheduler_.next_ref(lane) == none) available |= 1U << lane;
        if (available == 0) return true;
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::Claim);
        normalize_heads(available);
        uint32_t candidates[PERCOUNTER_CLUSTER_LANES];
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            candidates[lane] = (available & (1U << lane)) != 0 ? read(&cfg_.ssbuf->header.resource_head[lane]) : none;
        for (uint32_t a = 0; a < PERCOUNTER_CLUSTER_LANES; ++a)
            for (uint32_t b = a + 1; b < PERCOUNTER_CLUSTER_LANES; ++b)
                if (candidates[b] != none && (candidates[a] == none || cfg_.resource_nodes[candidates[b]].rank <
                                                                           cfg_.resource_nodes[candidates[a]].rank)) {
                    const uint32_t t = candidates[a];
                    candidates[a] = candidates[b];
                    candidates[b] = t;
                }
        for (uint32_t which = 0; which < PERCOUNTER_CLUSTER_LANES; ++which) {
            const uint32_t index = candidates[which];
            if (index == none || (which != 0 && index == candidates[which - 1])) continue;
            auto *node = &cfg_.resource_nodes[index];
            const uint32_t sync_cursor = read(&cfg_.ssbuf->header.sync_cursor);
            if (sync_cursor < cfg_.control->sync_cohort_count) {
                const auto &sync = cfg_.sync_boundaries[sync_cursor];
                if (node->rank > sync.rank || (node->rank == sync.rank && !prefix_ready(sync))) continue;
            }
            const uint32_t mask = physical_mask(*node, index);
            if (mask == 0 || (mask & available) != mask) continue;
            const uint32_t ref = scheduler_.reserve(static_cast<uint32_t>(node->entry), mask, node->catalog);
            if (ref == none) {
                if (read(&cfg_.ssbuf->header.sequence) == UINT32_MAX)
                    return fail(SchedulerGraphResult::INVALID_ARGUMENTS, percounter_order_task_id(node->entry));
                continue;
            }
            uint64_t entry = node->entry;
            if (node->claim_index != none) {
                const uint32_t blocks = percounter_order_block_num(entry);
                const uint32_t block = claim_block(node->claim_index, blocks);
                if (block >= blocks) {
                    scheduler_.rollback(ref);
                    continue;
                }
                entry = percounter_order_entry(percounter_order_task_id(entry), block, blocks);
            } else {
                const auto &piece = cfg_.ordinary_pieces[node->piece_index];
                const uint64_t ticket = percounter_gm_fetch_add(cfg_.tickets[piece.ticket_index].next, uint64_t{1});
                if (ticket >= piece.length) {
                    scheduler_.rollback(ref);
                    continue;
                }
                const uint32_t actual = cfg_.projection_catalog[piece.catalog][piece.begin + ticket];
                node = &cfg_.resource_nodes[actual];
                entry = node->entry;
            }
            if (!scheduler_.commit_claim(ref, entry, node->rank, node->claim_index, node->sync_index)) {
                scheduler_.rollback(ref);
                return fail(SchedulerGraphResult::INVALID_ARGUMENTS, percounter_order_task_id(entry));
            }
            write(&cfg_.ssbuf->observed_defer_head[ref], uint64_t{0});
            write(&cfg_.ssbuf->fanins[ref].addresses, uint64_t{0});
            claimed = ref;
            return true;
        }
        return true;
    }
    __aicore__ bool serve_terminal(uint32_t ref) {
        auto &record = cfg_.ssbuf->execution[ref];
        uint32_t cursor = read(&record.fanin_cursor);
        auto *sinks = scheduler_state_at<uint64_t>(cfg_.control, cfg_.control->sinks_offset);
        for (uint32_t step = 0; cursor < cfg_.control->sink_count && step < scan_budget; ++step) {
            const uint64_t sink = sinks[cursor];
            const uint64_t reference = reinterpret_cast<uint64_t>(&cfg_.counters[sink >> 1].value) |
                                       (sink & PERCOUNTER_COMPLETION_PARTITIONED);
            bool ready = false;
            if (!probe_reference(reference, ref, &ready)) return false;
            if (!ready) return true;
            ++cursor;
            write(&record.fanin_cursor, cursor);
        }
        if (cursor != cfg_.control->sink_count) return true;
        if (!scheduler_.mark_ready(ref) || !scheduler_.complete_without_kernel(ref))
            return fail(SchedulerGraphResult::INVALID_ARGUMENTS);
        close();
        return true;
    }
    __aicore__ bool service_record(uint32_t ref) {
        if (scheduler_.execution_flags(ref) & PERCOUNTER_CLUSTER_DISPATCHED) return true;
        if (!targets_idle(ref)) return true;
        const uint32_t id = task_id(ref);
        if (id == cfg_.task_count) return serve_terminal(ref);
        auto &record = cfg_.ssbuf->execution[ref];
        const uint32_t sync = read(&record.sync_index);
        if (sync != PERCOUNTER_NO_SYNC &&
            percounter_gm_read(cfg_.task_claims[read(&record.claim_index)].closed_sectors) <
                PERCOUNTER_CLAIM_SECTOR_COUNT)
            return true;
        auto &fanin = cfg_.ssbuf->fanins[ref];
        PercounterTask task{};
        const uint64_t addresses = read(&fanin.addresses);
        if (addresses == 0) {
            if (!percounter_metadata_ready(cfg_, id)) return true;
            if (!percounter_resolve(cfg_, id, &task)) return false;
            write(&fanin.count, static_cast<uint32_t>(task.fanin_count));
            write(&fanin.body_base, task.body_fanin ? static_cast<uint32_t>(task.body_base) : none);
            write(&fanin.addresses, reinterpret_cast<uint64_t>(task.fanin));
        } else {
            task.fanin = reinterpret_cast<__gm__ uint64_t *>(addresses);
            task.fanin_count = static_cast<int32_t>(read(&fanin.count));
            task.body_base = read(&fanin.body_base);
            task.body_fanin = task.body_base != none;
        }
        uint32_t cursor = read(&record.fanin_cursor);
        for (uint32_t step = 0; cursor < static_cast<uint32_t>(task.fanin_count) && step < scan_budget; ++step) {
            bool ready = false;
            if (!probe_reference(percounter_producer(cfg_, task, cursor), ref, &ready)) return false;
            if (!ready) return true;
            ++cursor;
            write(&record.fanin_cursor, cursor);
        }
        if (cursor != static_cast<uint32_t>(task.fanin_count)) return true;
        write(&record.async_owned_head, uint64_t{0});
        write(&record.async_cursor, uint64_t{0});
        write(&record.async_scan, uint64_t{0});
        const uint32_t node_index = cfg_.projection_task_map[id];
        if (node_index == none) return fail(SchedulerGraphResult::INVALID_TASK_ID, id);
        if (!scheduler_.mark_ready(ref)) return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
        if (cfg_.resource_nodes[node_index].active_mask == 0) {
            // Dependency-only tasks also need their Graph sink identity.
            if (addresses != 0 && !percounter_resolve(cfg_, id, &task)) return false;
            const uint64_t entry = scheduler_.entry(ref);
            percounter_publish_completion(
                cfg_, task,
                percounter_completion_address(
                    cfg_, id, percounter_order_block_idx(entry), percounter_order_block_num(entry)
                ),
                uint64_t{1}
            );
            if (!scheduler_.complete_without_kernel(ref)) return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
            return true;
        }
        if (!scheduler_.publish(ref)) return fail(SchedulerGraphResult::INVALID_ARGUMENTS, id);
        return true;
    }
    __aicore__ void close() {
        write(&cfg_.ssbuf->header.run_flags, read(&cfg_.ssbuf->header.run_flags) | closed_flag);
        memory_.release();
        scheduler_.release();
        done_ = true;
    }
    __aicore__ bool terminal_or_close() {
        if (!scheduler_.resources_quiescent()) return true;
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane)
            if (read(&cfg_.ssbuf->header.resource_head[lane]) != none) return true;
        if (percounter_gm_read(cfg_.control->terminal_claimed) != 0) {
            close();
            return true;
        }
        const uint32_t ref = scheduler_.reserve(static_cast<uint32_t>(cfg_.task_count), 4, 1);
        if (ref == none) return fail(SchedulerGraphResult::INVALID_ARGUMENTS);
        if (percounter_gm_compare_exchange(cfg_.control->terminal_claimed, uint64_t{0}, uint64_t{1}) != 0) {
            scheduler_.rollback(ref);
            close();
            return true;
        }
        if (!scheduler_.commit_claim(ref, percounter_order_entry(cfg_.task_count, 0, 1), UINT32_MAX))
            return fail(SchedulerGraphResult::INVALID_ARGUMENTS);
        write(&cfg_.ssbuf->observed_defer_head[ref], uint64_t{0});
        return serve_terminal(ref);
    }
    __aicore__ bool serve_owner() {
        ProfileScope profile_scope(cfg_.scalar, ProfilePhase::SharedService);
        for (uint32_t lane = 0; lane < PERCOUNTER_CLUSTER_LANES; ++lane) {
            PercounterClusterReturn returned;
            if (scheduler_.collect_fin(lane, returned) && !consume_fin(returned)) return false;
        }
        const uint32_t cursor = read(&cfg_.ssbuf->header.service_cursor) % PERCOUNTER_CLUSTER_RECORDS;
        const uint32_t free = read(&cfg_.ssbuf->header.free_mask);
        for (uint32_t offset = 0; offset < PERCOUNTER_CLUSTER_RECORDS; ++offset) {
            const uint32_t ref = (cursor + offset) % PERCOUNTER_CLUSTER_RECORDS;
            if ((free & (1U << ref)) == 0 && !service_record(ref)) return false;
            if (done_) return true;
        }
        write(&cfg_.ssbuf->header.service_cursor, (cursor + 1) % PERCOUNTER_CLUSTER_RECORDS);
        for (uint32_t claim = 0; claim < PERCOUNTER_CLUSTER_LANES; ++claim) {
            uint32_t ref;
            if (!claim_one(ref)) return false;
            if (ref == none) break;
            if (!service_record(ref)) return false;
        }
        return terminal_or_close();
    }

    const LaneConfig &cfg_;
    PercounterClusterSchedulerMemory memory_;
    PercounterClusterScheduler<PercounterClusterSchedulerMemory> scheduler_;
    bool initialized_{false};
    bool done_{false};
};

}  // namespace
}  // namespace simpler::percounter
