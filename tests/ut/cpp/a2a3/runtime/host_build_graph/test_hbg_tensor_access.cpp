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

#include <array>
#include <cstdint>
#include <limits>

#include "host_build_graph/host_tensor_access.h"

TEST(HbgTensorAccess, ExplicitHostRegionReadsAndWritesWithoutDeviceServices) {
    std::array<uint32_t, 4> values{7, 11, 13, 17};
    HostTensorAccessor access;
    const auto base = reinterpret_cast<uint64_t>(values.data());
    ASSERT_TRUE(access.add(base, sizeof(values), true, true));
    uint32_t result = 0;
    EXPECT_TRUE(access.read(base + sizeof(uint32_t), &result, sizeof(result)));
    EXPECT_EQ(result, 11u);
    const uint32_t replacement = 23;
    EXPECT_TRUE(access.write(base + 2 * sizeof(uint32_t), &replacement, sizeof(replacement)));
    EXPECT_EQ(values[2], replacement);
    EXPECT_EQ(access.region_count(), 1u);
    EXPECT_EQ(access.host_bytes(), sizeof(values));
    access.close();
    EXPECT_FALSE(access.read(base, &result, sizeof(result)));
    EXPECT_EQ(access.region_count(), 0u);
    EXPECT_EQ(access.host_bytes(), 0u);
}

TEST(HbgTensorAccess, RejectsUnregisteredAddressesAndOutOfRangeAccess) {
    std::array<uint8_t, 8> values{};
    HostTensorAccessor access;
    const auto base = reinterpret_cast<uint64_t>(values.data());
    ASSERT_TRUE(access.add(base, values.size(), true, true));
    uint32_t result = 0;
    EXPECT_FALSE(access.read(1, &result, sizeof(result)));
    EXPECT_FALSE(access.write(1, &result, sizeof(result)));
    EXPECT_FALSE(access.read(base + 5, &result, sizeof(result)));
    EXPECT_FALSE(access.write(base + 5, &result, sizeof(result)));
    EXPECT_FALSE(access.add(0, 8, true, true));
    EXPECT_FALSE(access.add(base, 0, true, true));
    EXPECT_FALSE(access.add(std::numeric_limits<uint64_t>::max() - 3, 8, true, true));
}

TEST(HbgTensorAccess, DirectionRestrictsHostAccess) {
    uint32_t input = 7;
    uint32_t output = 11;
    uint32_t value = 19;
    HostTensorAccessor access;
    ASSERT_TRUE(access.add(reinterpret_cast<uint64_t>(&input), sizeof(input), true, false));
    ASSERT_TRUE(access.add(reinterpret_cast<uint64_t>(&output), sizeof(output), false, true));
    EXPECT_FALSE(access.write(reinterpret_cast<uint64_t>(&input), &value, sizeof(value)));
    EXPECT_FALSE(access.read(reinterpret_cast<uint64_t>(&output), &value, sizeof(value)));
    EXPECT_TRUE(access.write(reinterpret_cast<uint64_t>(&output), &value, sizeof(value)));
    EXPECT_EQ(input, 7u);
    EXPECT_EQ(output, 19u);
    EXPECT_FALSE(host_tensor_read(nullptr, 1, &value, sizeof(value)));
    EXPECT_FALSE(host_tensor_write(nullptr, 1, &value, sizeof(value)));
}
