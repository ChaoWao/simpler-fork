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

#include "host_build_graph/kernel_graph_slot_wire.h"
#include "task_interface/kernel_invocation_validation.h"

namespace hbg {

struct GraphPacketReadOps {
    void *context{nullptr};
    // Covers the complete task-owned packet after trusted size/overlap checks,
    // before its first byte is parsed. Null selects device cache invalidation.
    bool (*invalidate)(void *, const void *, size_t){nullptr};
};

// Validated resource binding; graph image semantics are checked during preparation.
// The caller retains the immutable packet and the context execution lease while
// consuming this borrowed view. Validation does not acquire resources or dispatch tasks.
struct ValidatedGraphExecutionBinding {
    SimplerKernelInvocationHeader invocation{};
    GraphSlotRegistration slot{};
    GraphPacketHeader graph{};
    const std::byte *payload{nullptr};
    GraphImageRegion regions[4]{};  // Indexed by GraphImageKind; absent images have zero bytes.
};

// Uses the independently latched registry, never an address supplied by packet.
// Expected device/binary identity comes from trusted AICPU initialization, not packet fields.
// trusted_callable is borrowed from the live callable registration owner, never
// synthesized from packet fields. Its lease spans admission and consumption.
// On failure: no slot write, generation publication, or output modification.
GraphSlotStatus validate_graph_execution_binding(
    const void *packet, size_t bytes, int device_id, uint64_t runtime_binary_id,
    const simpler::kernel::PreparedInvocationView &trusted_callable, ValidatedGraphExecutionBinding &out,
    const GraphPacketReadOps &ops = {}, GraphPacketAddress address_mode = GraphPacketAddress::DeviceCopy
) noexcept;

}  // namespace hbg
