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
 * Graph Definition packing and upload, split along the preparation/publication
 * seam.
 *
 * A run's distinct Definition objects are packed once, into the prepared call's
 * own block, and shipped once per execution. The two halves are separate because
 * the block has no device address at preparation time — the bank an execution
 * uses is chosen later — and because one prepared call may be published more than
 * once, into a different block each time.
 */

#pragma once

#include <cstddef>
#include <cstdint>

struct GraphHostState;
struct HostApi;
struct ReadyQueuePopulations;

namespace simpler::hbg {
struct PreparedCall;
}

// What the pack produced. The object count is smaller than the run's Graph task
// count, which exceeds it by the replay factor — one Definition serves every task
// with its key. `spilled` is how many objects the recorders could not build in the
// retained staging, and so is 0 for a bind the staging was big enough for.
struct DefinitionPackCounts {
    size_t count{0};
    uint64_t bytes{0};
    size_t spilled{0};
    size_t submissions{0};
};

/**
 * Pack the run's Definition objects into `call`'s own block and name the object
 * every outer Graph task must be bound to.
 *
 * The recorders built most or all of them in place in the retained host staging,
 * each as [GraphDefinitionHeader][Definition image] at the offset it claimed, so
 * this copies that used prefix out, appends whatever did not fit, and writes every
 * object's framing header. Each Definition's reachable ready-queue population is
 * added to `ready_queue_populations` once per submission.
 *
 * Nothing is uploaded and no task's `graph_context` is written: the block has no
 * device address yet, and the bindings recorded on `call` are positions in the
 * block rather than addresses precisely so a later execution can bind them to a
 * different one.
 */
bool pack_graph_definitions(
    const HostApi *api, GraphHostState &graph_state, simpler::hbg::PreparedCall *call, DefinitionPackCounts *counts,
    ReadyQueuePopulations *ready_queue_populations
);

/**
 * Acquire one execution's device block and ship `call`'s packed bytes into it.
 *
 * `*block_base` receives the device address the image's `graph_context` fields
 * must be bound to, or 0 when the run has no Definitions. The acquire is what
 * grows the runner's retained staging as well, so the next bind's recorders find
 * room to build in place.
 */
bool publish_graph_definitions(const HostApi *api, const simpler::hbg::PreparedCall &call, uint64_t *block_base);
