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

#include "host_build_graph/host_tensor_access.h"

#include <cstring>
#include <limits>
#include <vector>

struct HostTensorAccessor::Impl {
    struct Region {
        uint64_t base;
        uint64_t size;
        bool readable;
        bool writable;
    };
    std::vector<Region> regions;
    uint64_t bytes = 0;

    const Region *find(uint64_t addr, uint64_t bytes) const {
        for (const auto &region : regions) {
            if (addr < region.base) continue;
            const uint64_t offset = addr - region.base;
            if (offset <= region.size && bytes <= region.size - offset) return &region;
        }
        return nullptr;
    }
};

HostTensorAccessor::HostTensorAccessor() :
    impl_(new Impl{}) {}
HostTensorAccessor::~HostTensorAccessor() { delete impl_; }

bool HostTensorAccessor::add(uint64_t host_base, uint64_t size, bool readable, bool writable) {
    if (host_base == 0 || size == 0 || size > std::numeric_limits<uint64_t>::max() - host_base ||
        size > std::numeric_limits<uint64_t>::max() - impl_->bytes)
        return false;
    impl_->regions.push_back({host_base, size, readable, writable});
    impl_->bytes += size;
    return true;
}

bool HostTensorAccessor::read(uint64_t host_addr, void *dst, uint64_t bytes) const {
    const auto *region = impl_->find(host_addr, bytes);
    if (region == nullptr || !region->readable || dst == nullptr) return false;
    std::memcpy(dst, reinterpret_cast<const void *>(host_addr), bytes);
    return true;
}

bool HostTensorAccessor::write(uint64_t host_addr, const void *src, uint64_t bytes) const {
    const auto *region = impl_->find(host_addr, bytes);
    if (region == nullptr || !region->writable || src == nullptr) return false;
    std::memcpy(reinterpret_cast<void *>(host_addr), src, bytes);
    return true;
}

size_t HostTensorAccessor::region_count() const noexcept { return impl_->regions.size(); }
uint64_t HostTensorAccessor::host_bytes() const noexcept { return impl_->bytes; }
void HostTensorAccessor::close() noexcept {
    impl_->regions.clear();
    impl_->bytes = 0;
}
bool host_tensor_read(HostTensorAccessor *accessor, uint64_t addr, void *dst, uint64_t bytes) {
    return accessor != nullptr && accessor->read(addr, dst, bytes);
}
bool host_tensor_write(HostTensorAccessor *accessor, uint64_t addr, const void *src, uint64_t bytes) {
    return accessor != nullptr && accessor->write(addr, src, bytes);
}
