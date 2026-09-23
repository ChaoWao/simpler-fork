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

#include "task_args.h"
#include "utils/retained_temp_bump.h"

/**
 * How many bytes of retained temporary buffer one run's device arguments need.
 *
 * This is one half of a pair that must not drift: a bind sizes the buffer with
 * this function and then slices it in its own argument loop, so the two have to
 * agree on which tensors take a slice and how much each consumes. A tensor
 * counted here but not sliced there — or the reverse — shifts every later slice
 * off the offsets this size was computed from, and nothing detects it until a
 * kernel reads the wrong bytes. It lives here, in one copy shared by both
 * runtimes and both architectures, precisely because that failure is silent.
 *
 * Only nonempty HOST_TO_DEVICE arguments consume slices. HOST stays on the
 * host, DEVICE is already allocated, and pure OUT still needs a device slice
 * even though it has no copy-in.
 *
 * Separate from `utils/retained_temp_bump.h` so that header stays free of
 * task_interface types; it needs only <cstddef>, and its unit test compiles
 * without the task_interface include path this header requires.
 */
inline size_t packed_temp_bytes(const ChipStorageTaskArgs *orch_args) {
    size_t required = 0;
    for (int i = 0; i < orch_args->tensor_count(); i++) {
        ChipTensor t = orch_args->tensor(i);
        if (t.address_space != AddressSpace::HOST_TO_DEVICE || t.nbytes() == 0) {
            continue;
        }
        required += RetainedTempBump::align_up(static_cast<size_t>(t.nbytes()));
    }
    return required;
}
