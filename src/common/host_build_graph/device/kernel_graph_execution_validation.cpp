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
#include "host_build_graph/kernel_graph_execution_validation.h"

#include "aicpu/cache_maintenance.h"
#include "common/unified_log.h"
#include "host_build_graph/kernel_graph_slot_registry.h"

namespace hbg {

GraphSlotStatus validate_graph_execution_binding(
    const void *packet, size_t bytes, int device_id, uint64_t runtime_binary_id,
    const simpler::kernel::PreparedInvocationView &trusted_callable, ValidatedGraphExecutionBinding &out,
    const GraphPacketReadOps &ops, GraphPacketAddress address_mode
) noexcept {
    const GraphSlotRegistry *registry = current_graph_slot_registry();
    if (registry == nullptr) return GraphSlotStatus::NotReady;
    GraphSlotRegistration registration{};
    const auto status = acquire_graph_execution_slot(registry, device_id, runtime_binary_id, registration);
    if (status != GraphSlotStatus::Ok) return status;
    if (packet == nullptr || bytes > registration.max_packet_bytes) return GraphSlotStatus::InvalidPacket;
    const GraphDestination source{reinterpret_cast<uintptr_t>(packet), bytes};
    if (!graph_span_fits(source.address, source.capacity, UINT64_MAX)) return GraphSlotStatus::InvalidPacket;
    if (graph_windows_overlap(source, registration.registry)) return GraphSlotStatus::SourceOverlap;
    for (const auto &destination : registration.destinations)
        if (graph_windows_overlap(source, destination)) return GraphSlotStatus::SourceOverlap;
    if (bytes < sizeof(SimplerKernelInvocationHeader) + sizeof(GraphPacketHeader))
        return GraphSlotStatus::InvalidPacket;
    if (ops.invalidate) {
        if (!ops.invalidate(ops.context, packet, bytes)) return GraphSlotStatus::SourceUnavailable;
    } else {
        cache_invalidate_range(packet, bytes);
    }
    SimplerKernelInvocationHeader invocation{};
    const auto admission = simpler::kernel::validate_invocation_header(
        {static_cast<const uint8_t *>(packet), bytes}, trusted_callable, &invocation
    );
    if (admission == simpler::kernel::InvocationStatus::StaleCallable) return GraphSlotStatus::CallableMismatch;
    if (admission != simpler::kernel::InvocationStatus::Ok) return GraphSlotStatus::InvalidPacket;
    const auto packet_status = validate_graph_packet(packet, bytes, address_mode);
    if (packet_status != GraphPacketStatus::Ok) {
        LOG_ERROR("HBG packet validation failed: GraphPacketStatus=%u", static_cast<unsigned>(packet_status));
        return GraphSlotStatus::InvalidPacket;
    }
    GraphPacketHeader header{};
    std::memcpy(
        &header, static_cast<const std::byte *>(packet) + sizeof(SimplerKernelInvocationHeader), sizeof(header)
    );
    if (header.device_id != registration.device_id) return GraphSlotStatus::DeviceMismatch;
    if (header.slot_generation != registration.slot_generation) return GraphSlotStatus::GenerationMismatch;
    if (header.runtime_binary_id != registration.runtime_binary_id) return GraphSlotStatus::BinaryMismatch;
    for (size_t i = 0; i < 4; ++i)
        if (header.destinations[i].address != registration.destinations[i].address ||
            header.destinations[i].capacity != registration.destinations[i].capacity)
            return GraphSlotStatus::BindingMismatch;
    out = {
        invocation, registration, header,
        static_cast<const std::byte *>(packet) + sizeof(SimplerKernelInvocationHeader) + header.payload_offset
    };
    for (uint32_t i = 0; i < header.region_count; ++i) {
        GraphImageRegion region{};
        std::memcpy(
            &region,
            static_cast<const std::byte *>(packet) + sizeof(SimplerKernelInvocationHeader) + sizeof(header) +
                i * sizeof(region),
            sizeof(region)
        );
        out.regions[static_cast<uint32_t>(region.kind)] = region;
    }
    return GraphSlotStatus::Ok;
}

}  // namespace hbg
