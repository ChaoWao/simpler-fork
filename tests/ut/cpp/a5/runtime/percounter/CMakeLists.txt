# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

function(add_percounter_test name)
    simpler_ut_add_target(NAME ${name}
        SOURCES ${ARGN}
        ARCH a5 RUNTIME percounter
        INCLUDES
            ${SIMPLER_SRC}/a5/runtime/percounter/runtime
            ${SIMPLER_SRC}/a5/runtime/percounter/aicore
            ${SIMPLER_SRC}/a5/runtime/percounter/host
            ${SIMPLER_SRC}/a5/runtime/host_build_graph/runtime
            ${SIMPLER_SRC}/a5/runtime/host_build_graph/common
            ${SIMPLER_SRC}/common/host_build_graph
            ${SIMPLER_SRC}/a5/platform/sim/aicore
        DEFINES __CPU_SIM SIMPLER_PLATFORM_NAME="a5sim"
        LABEL no_hardware TIMEOUT 30 PRIVATE_WORKDIR)
endfunction()

add_percounter_test(test_a5_percounter_payload test_payload.cpp)
add_percounter_test(test_a5_percounter_scalar_memory scalar_memory_unprofiled.cpp test_scalar_memory.cpp)
add_percounter_test(test_a5_percounter_cluster_scheduler test_cluster_scheduler.cpp)
add_percounter_test(test_a5_percounter_resource_projection test_resource_projection.cpp)
add_percounter_test(test_a5_percounter_completion_state test_completion_state.cpp)
add_percounter_test(test_a5_percounter_deferred_groups test_deferred_groups.cpp)
