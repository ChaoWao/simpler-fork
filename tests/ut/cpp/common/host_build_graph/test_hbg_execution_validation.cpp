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
#include <gtest/gtest.h>

#include <cstring>

#include "host_build_graph/kernel_graph_execution_validation.h"
#include "host_build_graph/kernel_graph_slot_registry.h"

namespace {
class HbgExecutionValidationTest : public ::testing::Test {
protected:
    alignas(1024) hbg::GraphSlotRegistry registry{};
    hbg::GraphSlotRegistration registration{};

    void SetUp() override {
        registration.magic = hbg::GRAPH_SLOT_MAGIC;
        registration.version = hbg::GRAPH_SLOT_VERSION;
        registration.bytes = sizeof(registration);
        registration.flags = hbg::GRAPH_SLOT_FROZEN_SERIAL;
        registration.device_id = 0;
        registration.slot_generation = 8;
        registration.runtime_binary_id = 123;
        registration.destinations[0] = {0x100000, 4096};
        registration.destinations[1] = {0x200000, 4096};
        registration.registry = {reinterpret_cast<uintptr_t>(&registry), sizeof(registry)};
        seal(registration);
        ASSERT_TRUE(hbg::valid_graph_slot_registration(registration));
    }
    void TearDown() override {
        if (auto *current = hbg::current_graph_slot_registry()) hbg::detach_graph_slot_registry(current);
    }
    static void seal(hbg::GraphSlotRegistration &value) {
        ASSERT_TRUE(hbg::graph_slot_packet_size(value.destinations, value.max_packet_bytes));
        value.checksum = hbg::graph_slot_checksum(value);
    }
    hbg::GraphSlotStatus install() {
        return hbg::install_graph_execution_slot(&registration, sizeof(registration));
    }
};

TEST_F(HbgExecutionValidationTest, OverlappingPacketIsRejectedBeforeReadingAndLeavesOutputUntouched) {
    ASSERT_EQ(install(), hbg::GraphSlotStatus::Ok);
    hbg::ValidatedGraphExecutionBinding output{};
    output.graph.device_id = 55;
    const auto before = output;
    bool read = false;
    hbg::GraphPacketReadOps ops{&read, [](void *context, const void *, size_t) {
        *static_cast<bool *>(context) = true;
        return true;
    }};
    const simpler::kernel::PreparedInvocationView trusted{};
    EXPECT_EQ(hbg::validate_graph_execution_binding(&registry, sizeof(registry), 0, 123, trusted, output, ops),
              hbg::GraphSlotStatus::SourceOverlap);
    EXPECT_FALSE(read);
    EXPECT_EQ(std::memcmp(&before, &output, sizeof(output)), 0);
}

TEST_F(HbgExecutionValidationTest, MissingRegistryLeavesOutputUntouched) {
    hbg::ValidatedGraphExecutionBinding output{};
    output.graph.device_id = 55;
    const auto before = output;
    const simpler::kernel::PreparedInvocationView trusted{};
    EXPECT_EQ(hbg::validate_graph_execution_binding(nullptr, 0, 0, 123, trusted, output),
              hbg::GraphSlotStatus::NotReady);
    EXPECT_EQ(std::memcmp(&before, &output, sizeof(output)), 0);
}

TEST_F(HbgExecutionValidationTest, PoisonedRegistryRejectsValidationWithoutChangingOutput) {
    ASSERT_EQ(install(), hbg::GraphSlotStatus::Ok);
    ASSERT_EQ(hbg::poison_graph_execution_slot(&registry), hbg::GraphSlotStatus::Ok);
    hbg::ValidatedGraphExecutionBinding output{};
    output.graph.device_id = 55;
    const auto before = output;
    const simpler::kernel::PreparedInvocationView trusted{};
    EXPECT_EQ(hbg::validate_graph_execution_binding(nullptr, 0, 0, 123, trusted, output),
              hbg::GraphSlotStatus::Poisoned);
    EXPECT_EQ(std::memcmp(&before, &output, sizeof(output)), 0);
}

TEST_F(HbgExecutionValidationTest, UnavailablePacketLeavesRegistryAndOutputUntouched) {
    ASSERT_EQ(install(), hbg::GraphSlotStatus::Ok);
    const auto registry_before = registry;
    std::byte packet[sizeof(SimplerKernelInvocationHeader) + sizeof(hbg::GraphPacketHeader)]{};
    hbg::ValidatedGraphExecutionBinding output{};
    output.graph.device_id = 55;
    const auto before = output;
    const simpler::kernel::PreparedInvocationView trusted{};
    bool read = false;
    hbg::GraphPacketReadOps ops{&read, [](void *context, const void *, size_t) {
        *static_cast<bool *>(context) = true;
        return false;
    }};
    EXPECT_EQ(hbg::validate_graph_execution_binding(packet, sizeof(packet), 0, 123, trusted, output, ops),
              hbg::GraphSlotStatus::SourceUnavailable);
    EXPECT_TRUE(read);
    EXPECT_EQ(std::memcmp(&before, &output, sizeof(output)), 0);
    EXPECT_EQ(std::memcmp(&registry_before, &registry, sizeof(registry)), 0);
}

}  // namespace
