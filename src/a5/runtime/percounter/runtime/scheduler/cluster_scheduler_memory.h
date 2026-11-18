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

#include "scheduler/cluster_scheduler.h"
#include "scheduler/gm.h"
#include "scheduler/ssbuf.h"

namespace simpler::percounter {

static_assert(sizeof(PercounterClusterSchedulerRegion) == PERCOUNTER_SSBUF_REGION_SIZE);

class PercounterClusterSchedulerMemory {
public:
    __aicore__ PercounterClusterSchedulerMemory(
        __gm__ PercounterClusterOwnerSector *owner, PERCOUNTER_SSBUF PercounterClusterSchedulerRegion *region
    ) :
        owner_(owner),
        region_(region) {}

    __aicore__ uint64_t owner_load() {
        return percounter_gm_read(owner_->owner, __ATOMIC_ACQUIRE, ProfilePhase::OwnerLoad);
    }
    __aicore__ uint64_t owner_cas(uint32_t expected, uint32_t desired) {
        return percounter_gm_compare_exchange(owner_->owner, uint64_t{expected}, uint64_t{desired});
    }
    __aicore__ void owner_store(uint32_t value) { percounter_gm_store(owner_->owner, uint64_t{value}); }
    __aicore__ uint32_t load(uint32_t index) {
        auto *words = reinterpret_cast<PERCOUNTER_SSBUF volatile uint32_t *>(region_);
        return percounter_ssbuf_load_field(words + index);
    }
    __aicore__ void store(uint32_t index, uint32_t value) {
        auto *words = reinterpret_cast<PERCOUNTER_SSBUF volatile uint32_t *>(region_);
        percounter_ssbuf_store_field(words + index, value);
    }
    __aicore__ void acquire() { fence(); }
    __aicore__ void release() { fence(); }

private:
    __aicore__ void fence() {
        percounter_scalar_memory_issue(ProfilePhase::DsbAll, 1, []() __aicore__ {
#if defined(__CCE_AICORE__)
            dsb((mem_dsb_t)0);
#else
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
        });
    }
    __gm__ PercounterClusterOwnerSector *owner_;
    PERCOUNTER_SSBUF PercounterClusterSchedulerRegion *region_;
};

}  // namespace simpler::percounter
