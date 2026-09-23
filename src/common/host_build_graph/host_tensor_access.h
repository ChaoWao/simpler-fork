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

// A run's explicitly HOST regions. Addresses are host addresses, never device
// aliases; this module has no device-copy or mapping API. Ownership remains with
// the caller through host orchestration. Device consumers must use a separate
// DEVICE or HOST_TO_DEVICE tensor argument.
class HostTensorAccessor {
public:
    HostTensorAccessor();
    ~HostTensorAccessor();
    HostTensorAccessor(const HostTensorAccessor &) = delete;
    HostTensorAccessor &operator=(const HostTensorAccessor &) = delete;

    bool add(uint64_t host_base, uint64_t size, bool readable, bool writable);
    bool read(uint64_t host_addr, void *dst, uint64_t bytes) const;
    bool write(uint64_t host_addr, const void *src, uint64_t bytes) const;
    size_t region_count() const noexcept;
    uint64_t host_bytes() const noexcept;
    void close() noexcept;

private:
    struct Impl;
    Impl *impl_;
};

bool host_tensor_read(HostTensorAccessor *accessor, uint64_t host_addr, void *dst, uint64_t bytes);
bool host_tensor_write(HostTensorAccessor *accessor, uint64_t host_addr, const void *src, uint64_t bytes);
