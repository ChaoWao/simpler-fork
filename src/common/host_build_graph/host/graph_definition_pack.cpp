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

/**
 * Graph Definition packing and upload. See
 * host_build_graph/graph_definition_pack.h for the contract.
 */

#include "host_build_graph/graph_definition_pack.h"

#include <cinttypes>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

#include "assert_compat.h"
#include "common/host_api.h"
#include "common/unified_log.h"
#include "host_build_graph/graph_execution.h"
#include "host_build_graph/graph_host_state.h"
#include "host_build_graph/prepared_call.h"
#include "host_build_graph/ready_queue_sizing.h"
#include "host_build_graph/runtime_types.h"

namespace {

size_t align_object(size_t value) {
    return (value + GRAPH_DEFINITION_OBJECT_ALIGN - 1) & ~(GRAPH_DEFINITION_OBJECT_ALIGN - 1);
}

struct PackedDefinition {
    size_t object_offset;   // of the object's header, from the block base
    size_t image_bytes;     // the Definition image alone
    const std::byte *copy;  // the image to copy in, or nullptr when built in place
    ReadyQueuePopulations ready_queue_populations;
    bool populations_ready{false};
};

}  // namespace

bool pack_graph_definitions(
    const HostApi *api, GraphHostState &graph_state, simpler::hbg::PreparedCall *call, DefinitionPackCounts *counts,
    ReadyQueuePopulations *ready_queue_populations
) {
    *counts = DefinitionPackCounts{};
    call->definition_block.clear();
    call->definition_bindings.clear();
    call->definition_count = 0;
    call->definition_spills = 0;

    const size_t submissions = graph_host_upload_count(graph_state);
    counts->submissions = submissions;
    GraphHostDefinitionList definitions = graph_host_definitions(graph_state);
    std::unordered_map<uint64_t, PackedDefinition> packed;
    // Objects the recorders built already occupy the staging's used prefix at the
    // offsets they claimed, so the block starts out that long and the rest are
    // appended past them.
    const size_t built_in_place_bytes = graph_host_arena_used(graph_state);
    size_t block_bytes = built_in_place_bytes;
    for (const GraphHostDefinition &entry : definitions.entries) {
        if (entry.bytes < sizeof(GraphDefinition)) continue;
        if (entry.spill == nullptr) {
            packed.emplace(entry.full_key, PackedDefinition{entry.object_offset, entry.bytes, nullptr, {}, false});
            continue;
        }
        const size_t object_offset = block_bytes;
        block_bytes += align_object(sizeof(GraphDefinitionHeader) + entry.bytes);
        packed.emplace(entry.full_key, PackedDefinition{object_offset, entry.bytes, entry.spill, {}, false});
        call->definition_spills++;
    }

    if (block_bytes == 0) {
        // No Definitions, so no block and no bindings. A run with no Graph task
        // takes this path on every bind; a Graph task with nothing to bind to is
        // a broken recording.
        if (submissions != 0) {
            LOG_ERROR("host-orch: %zu Graph task(s) with no Definition object to bind", submissions);
            return false;
        }
        return true;
    }

    const std::byte *staging = nullptr;
    {
        void *staging_addr = nullptr;
        size_t staging_bytes = 0;
        api->get_graph_definition_staging(&staging_addr, &staging_bytes);
        if (built_in_place_bytes != 0 && (staging_addr == nullptr || staging_bytes < built_in_place_bytes)) {
            LOG_ERROR(
                "host-orch: Definition staging holds %zu bytes but the recorders claimed %zu", staging_bytes,
                built_in_place_bytes
            );
            return false;
        }
        staging = static_cast<const std::byte *>(staging_addr);
    }

    // Value-initialized, so every byte of the block — the inter-object alignment
    // padding included — is defined by this pass rather than by whatever the
    // staging held before it.
    call->definition_block.assign(block_bytes, std::byte{0});
    std::byte *block = call->definition_block.data();
    if (built_in_place_bytes != 0) {
        std::memcpy(block, staging, built_in_place_bytes);
    }
    for (const auto &[key, object] : packed) {
        std::byte *base = block + object.object_offset;
        std::byte *image = base + sizeof(GraphDefinitionHeader);
        if (object.copy != nullptr) std::memcpy(image, object.copy, object.image_bytes);
        const auto *definition = reinterpret_cast<const GraphDefinition *>(image);
        GraphDefinitionHeader framing{};
        framing.magic = GRAPH_DEFINITION_OBJECT_MAGIC;
        framing.full_key = definition->full_key;
        framing.definition_bytes = definition->total_bytes;
        std::memcpy(base, &framing, sizeof(framing));
        const size_t object_bytes = sizeof(GraphDefinitionHeader) + object.image_bytes;
        std::memset(base + object_bytes, 0, align_object(object_bytes) - object_bytes);
    }
    call->definition_count = packed.size();
    counts->count = packed.size();
    counts->bytes = block_bytes;
    counts->spilled = call->definition_spills;

    for (size_t index = 0; index < submissions; ++index) {
        std::optional<GraphHostUpload> upload = graph_host_upload(graph_state, index);
        if (!upload.has_value() || upload->outer_slot == nullptr || upload->outer_slot->task_kind != TaskKind::GRAPH) {
            LOG_ERROR("host-orch: invalid pending Graph task");
            return false;
        }
        auto object_it = packed.find(upload->full_key);
        if (object_it == packed.end()) {
            LOG_ERROR("host-orch: Graph task has no matching packed Definition object");
            return false;
        }
        // The object as it was packed, so what this validates is the bytes the
        // device will read rather than a second host copy of them.
        const auto *definition = reinterpret_cast<const GraphDefinition *>(
            block + object_it->second.object_offset + sizeof(GraphDefinitionHeader)
        );
        if (definition->total_bytes != object_it->second.image_bytes) {
            LOG_ERROR("host-orch: Graph task has no matching packed Definition object");
            return false;
        }
        GraphExecutionStorageLayout storage_layout{};
        if (definition->task_count <= 0 || definition->task_count > MAX_IN_GRAPH_TASKS ||
            definition->full_key != upload->full_key ||
            !graph_execution_storage_layout(
                definition->task_count, definition->tensor_arg_count, definition->scalar_arg_count, &storage_layout
            ) ||
            storage_layout.total_bytes != definition->execution_storage_bytes ||
            upload->outer_slot->to_payload().tensor_count != definition->boundary_tensor_count ||
            upload->outer_slot->to_payload().scalar_count != definition->boundary_scalar_count) {
            LOG_ERROR("host-orch: invalid Graph Definition for task");
            return false;
        }
        // Checked against the virtual heap window the orchestrator allocated out
        // of, which is congruent with every committed heap base, so the verdict
        // carries to whichever bank an execution binds to.
        const uintptr_t outer_base =
            reinterpret_cast<uintptr_t>(upload->outer_slot->to_descriptor().packed_buffer_base);
        const uintptr_t outer_end = reinterpret_cast<uintptr_t>(upload->outer_slot->to_descriptor().packed_buffer_end);
        if (outer_end < outer_base || definition->required_heap > UINTPTR_MAX - outer_base ||
            storage_layout.total_bytes > outer_end - outer_base ||
            definition->required_heap > outer_end - outer_base - storage_layout.total_bytes) {
            LOG_ERROR("host-orch: Graph runtime storage does not fit its outer task heap");
            return false;
        }
        const uintptr_t storage_addr = outer_base + definition->required_heap;
        if (storage_addr % alignof(ChipTaskStorage) != 0) {
            LOG_ERROR("host-orch: Graph runtime storage address is misaligned");
            return false;
        }
        PackedDefinition &packed_definition = object_it->second;
        if (!packed_definition.populations_ready) {
            const InGraphTaskDefinition *tasks = graph_definition_array<InGraphTaskDefinition>(
                *definition, definition->off_in_graph_tasks, definition->task_count
            );
            if (tasks == nullptr) {
                LOG_ERROR("host-orch: invalid Graph Definition in-graph task array");
                return false;
            }
            for (int32_t i = 0; i < definition->task_count; ++i) {
                // Sizing takes the kind materialize will give this task. add_task
                // singles out GRAPH and routes everything else by shape, and a Graph
                // body member is never the shell, so the shape decides. Derived here
                // the same way the device derives it, so the two cannot drift.
                const ActiveMask mask(tasks[i].active_mask);
                packed_definition.ready_queue_populations.add_task(
                    mask, TaskAttrs(tasks[i].task_attrs), mask.is_dummy() ? TaskKind::DUMMY : TaskKind::KERNEL
                );
            }
            packed_definition.populations_ready = true;
        }
        ready_queue_populations->add(packed_definition.ready_queue_populations);
        // Named by position, not by address: a prepared call may be published into
        // a different block, and a task id indexes the image's storage directly.
        const TaskId outer_id = upload->outer_slot->to_descriptor().task_id;
        if (!outer_id.is_valid() || !outer_id.is_global() || outer_id.local_id() < 0) {
            LOG_ERROR("host-orch: outer Graph task carries no task-table id");
            return false;
        }
        call->definition_bindings.push_back(
            simpler::hbg::DefinitionBinding{static_cast<uint64_t>(outer_id.local_id()), object_it->second.object_offset}
        );
    }
    return true;
}

bool publish_graph_definitions(const HostApi *api, const simpler::hbg::PreparedCall &call, uint64_t *block_base) {
    *block_base = 0;
    if (call.definition_block.empty()) {
        return call.definition_bindings.empty();
    }
    void *block = nullptr;
    void *staging = nullptr;
    // Acquiring at the packed size is also what grows the runner's retained
    // staging, so the next bind's recorders build in place rather than spilling.
    if (api->acquire_graph_definition_block(
            call.definition_block.size(), GRAPH_DEFINITION_OBJECT_ALIGN, &block, &staging
        ) != 0 ||
        block == nullptr) {
        LOG_ERROR(
            "host-orch: failed to retain %zu bytes for %zu Graph Definition object(s)", call.definition_block.size(),
            call.definition_count
        );
        return false;
    }
    if (api->copy_to_device(block, call.definition_block.data(), call.definition_block.size()) != 0) {
        LOG_ERROR("host-orch: failed to upload the Graph Definition block");
        return false;
    }
    *block_base = reinterpret_cast<uint64_t>(block);
    return true;
}
