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
 * A prepared call is canonical: its image names no device region, and binding it
 * to one is a separate pass that only reads it. These tests pin the three address
 * classes that pass has to bind — caller-argument slices by list, the graph heap
 * by window, the Definition objects by position — and the two properties that
 * make a second execution possible at all: binding leaves the canonical bytes
 * byte-identical, and a submission the canonical data does not describe is
 * refused rather than served.
 *
 * The register table and the counters are here too, because "the result outlived
 * the code it dispatches" and "the reused execution silently re-orchestrated" are
 * both failures no golden can see.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "host_build_graph/graph_execution.h"
#include "host_build_graph/prepared_call.h"
#include "host_build_graph/shared_memory.h"
#include "host_build_graph/task_id.h"

namespace {

using simpler::hbg::ArgSliceRelocation;
using simpler::hbg::DefinitionBinding;
using simpler::hbg::PreparedArg;
using simpler::hbg::PreparedArgKind;
using simpler::hbg::PreparedCall;
using simpler::hbg::PreparedCallPtr;

constexpr uint64_t WINDOW = 32;  // stands in for the mirror's task capacity
constexpr uint64_t SUBMITTED = 4;
constexpr int32_t TENSORS_PER_TASK = 2;

// A plausible retained temporary buffer: a base far from zero and a used extent,
// so an address inside it is distinguishable from a heap address and from zero.
constexpr uint64_t TEMP_BASE = 0x40000000;
constexpr uint64_t TEMP_BYTES = 4096;
constexpr uint64_t SLICE_BYTES = 1024;

constexpr uint64_t HEAP_REAL_BASE = 0x20000000;
constexpr uint64_t PACKED_STRIDE = 512;
constexpr uint64_t DEFINITION_BLOCK_BASE = 0x30000000;

class AlignedImage {
public:
    explicit AlignedImage(uint64_t bytes) :
        storage_(bytes + CHIP_ALIGN_SIZE, std::byte{0}) {
        base_ = reinterpret_cast<char *>(
            (reinterpret_cast<uintptr_t>(storage_.data()) + CHIP_ALIGN_SIZE - 1) &
            ~static_cast<uintptr_t>(CHIP_ALIGN_SIZE - 1)
        );
    }
    char *base() { return base_; }

private:
    std::vector<std::byte> storage_;
    char *base_{nullptr};
};

sm_layout::BindUsage usage_for(uint64_t submitted) {
    return sm_layout::BindUsage{submitted, 0, submitted * static_cast<uint64_t>(TENSORS_PER_TASK), 0};
}

// A mirror in the state the orchestrator leaves it, with the three address classes
// a bind has to resolve present at once: tensor 0 of each task is a graph-heap
// output (virtual), tensor 1 is a caller-argument slice of the temporary buffer,
// and every task's packed buffer and predicate are virtual heap addresses. Task 0
// is the outer Graph task.
class Mirror {
public:
    Mirror() :
        image_(sm_layout::segment_offsets(WINDOW).end) {
        const auto off = sm_layout::segment_offsets(WINDOW);
        auto *header = reinterpret_cast<SharedMemoryHeader *>(image_.base());
        header->tasks.total_tasks = static_cast<int32_t>(SUBMITTED);
        storage_ = reinterpret_cast<ChipTaskStorage *>(image_.base() + off.storage);
        header->tasks.task_storage = storage_;
        header->tasks.task_states = reinterpret_cast<std::atomic<ChipTaskState> *>(image_.base() + off.task_states);
        auto *tensors = reinterpret_cast<simpler::hbg::Tensor *>(image_.base() + off.tensor_pool);

        for (uint64_t i = 0; i < SUBMITTED; ++i) {
            ChipTaskStorage &entry = storage_[i];
            entry.task.task_id = TaskId::make_global(static_cast<int32_t>(i));
            entry.payload.tensor_count = TENSORS_PER_TASK;
            entry.payload.bind_regions(tensors + i * TENSORS_PER_TASK, nullptr, nullptr);
            const uint64_t packed = HEAP_VIRTUAL_BASE + i * PACKED_STRIDE;
            entry.task.packed_buffer_base = reinterpret_cast<void *>(packed);
            entry.task.packed_buffer_end = reinterpret_cast<void *>(packed + PACKED_STRIDE);
            entry.payload.predicate.addr = packed + 8;
            entry.payload.tensor_data()[0].buffer.addr = packed;
            entry.payload.tensor_data()[1].buffer.addr = TEMP_BASE + i * SLICE_BYTES;
            entry.slot.task_kind = i == 0 ? TaskKind::GRAPH : TaskKind::KERNEL;
            entry.slot.graph_context = nullptr;
        }
    }

    const char *base() const { return const_cast<AlignedImage &>(image_).base(); }
    static constexpr uint64_t heap_used() { return SUBMITTED * PACKED_STRIDE; }

private:
    AlignedImage image_;
    ChipTaskStorage *storage_{nullptr};
};

// The contract the Mirror's slices correspond to: one declared host-memory
// argument per task, at the offset that task's tensor 1 addresses.
std::vector<PreparedArg> declared_args() {
    std::vector<PreparedArg> args;
    for (uint64_t i = 0; i < SUBMITTED; ++i) {
        args.push_back(
            PreparedArg{PreparedArgKind::HostMemory, ArgDirection::INOUT, SLICE_BYTES, 0, i * SLICE_BYTES, 0}
        );
    }
    return args;
}

// A sealed call over the Mirror above: the canonical image, the argument contract,
// the collected slice relocations, and one Definition binding for task 0.
PreparedCall seal(Mirror &mirror) {
    PreparedCall call;
    call.task_capacity = WINDOW;
    call.total_tasks = static_cast<int32_t>(SUBMITTED);
    call.layout_digest = simpler::hbg::prepared_call_layout_digest(WINDOW);
    call.usage = usage_for(SUBMITTED);
    call.heap_bytes = Mirror::heap_used();
    call.args = declared_args();
    const uint64_t bytes = sm_layout::segment_offsets(sm_layout::image_extents(call.usage)).end;
    call.image.resize(static_cast<size_t>(bytes));
    EXPECT_EQ(sm_layout::compact_live_image(mirror.base(), WINDOW, call.usage, call.image.data()), bytes);
    EXPECT_TRUE(simpler::hbg::prepared_call_collect_arg_slices(call, TEMP_BASE, TEMP_BYTES));
    call.definition_bindings.push_back(DefinitionBinding{0, 128});
    return call;
}

ChipTaskStorage *storage_of(char *image_base, const sm_layout::BindUsage &usage) {
    return reinterpret_cast<ChipTaskStorage *>(
        image_base + sm_layout::segment_offsets(sm_layout::image_extents(usage)).storage
    );
}

// One bound working image for `call`, against the given bases.
class Bound {
public:
    Bound(const PreparedCall &call, uint64_t temp_base, uint64_t heap_base, uint64_t definition_base) :
        image_(call.image_bytes()) {
        simpler::hbg::prepared_call_bind_image(
            call, image_.base(), temp_base, sm_layout::HeapRebase{heap_base, call.heap_bytes}, definition_base
        );
        storage_ = storage_of(image_.base(), call.usage);
    }

    ChipTaskStorage &task(uint64_t index) { return storage_[index]; }

private:
    AlignedImage image_;
    ChipTaskStorage *storage_{nullptr};
};

}  // namespace

// The canonical form names no device region: every heap address is still in the
// window, and no outer Graph task points at a Definition object.
TEST(HbgPreparedCall, CanonicalImageNamesNoDeviceRegion) {
    Mirror mirror;
    PreparedCall call = seal(mirror);

    ChipTaskStorage *storage = storage_of(call.image.data(), call.usage);
    for (uint64_t i = 0; i < SUBMITTED; ++i) {
        EXPECT_GE(reinterpret_cast<uint64_t>(storage[i].task.packed_buffer_base), HEAP_VIRTUAL_BASE);
        EXPECT_GE(storage[i].payload.tensor_data()[0].buffer.addr, HEAP_VIRTUAL_BASE);
        EXPECT_EQ(storage[i].slot.graph_context, nullptr);
    }
}

// Every field addressing the temporary buffer has to be in the relocation list,
// or a later execution against a moved buffer would keep a stale address.
TEST(HbgPreparedCall, CollectsEveryArgumentSliceAddress) {
    Mirror mirror;
    PreparedCall call = seal(mirror);

    ASSERT_EQ(call.arg_slices.size(), SUBMITTED);
    for (uint64_t i = 0; i < SUBMITTED; ++i) {
        const ArgSliceRelocation &relocation = call.arg_slices[i];
        EXPECT_EQ(relocation.task_index, i);
        EXPECT_EQ(relocation.tensor_index, 1);
        EXPECT_EQ(relocation.slice_offset, i * SLICE_BYTES);
    }
}

// An address inside the buffer that no declared argument covers means the contract
// does not describe everything the image addresses, so the seal must fail rather
// than produce a list a later bind would silently under-apply.
TEST(HbgPreparedCall, RefusesASliceAddressNoArgumentDeclares) {
    Mirror mirror;
    PreparedCall call;
    call.usage = usage_for(SUBMITTED);
    const uint64_t bytes = sm_layout::segment_offsets(sm_layout::image_extents(call.usage)).end;
    call.image.resize(static_cast<size_t>(bytes));
    sm_layout::compact_live_image(mirror.base(), WINDOW, call.usage, call.image.data());
    // Only the first task's slice is declared; the other three are not.
    call.args.push_back(PreparedArg{PreparedArgKind::HostMemory, ArgDirection::INOUT, SLICE_BYTES, 0, 0, 0});

    EXPECT_FALSE(simpler::hbg::prepared_call_collect_arg_slices(call, TEMP_BASE, TEMP_BYTES));
}

// Binding resolves all three classes at once, and the caller-owned class is the
// one it must leave alone.
TEST(HbgPreparedCall, BindingResolvesHeapSlicesAndDefinitions) {
    Mirror mirror;
    PreparedCall call = seal(mirror);
    const uint64_t moved_temp = TEMP_BASE + 0x100000;
    Bound bound(call, moved_temp, HEAP_REAL_BASE, DEFINITION_BLOCK_BASE);

    for (uint64_t i = 0; i < SUBMITTED; ++i) {
        ChipTaskStorage &entry = bound.task(i);
        const uint64_t packed = HEAP_REAL_BASE + i * PACKED_STRIDE;
        EXPECT_EQ(reinterpret_cast<uint64_t>(entry.task.packed_buffer_base), packed);
        EXPECT_EQ(reinterpret_cast<uint64_t>(entry.task.packed_buffer_end), packed + PACKED_STRIDE);
        EXPECT_EQ(entry.payload.predicate.addr, packed + 8);
        EXPECT_EQ(entry.payload.tensor_data()[0].buffer.addr, packed);
        EXPECT_EQ(entry.payload.tensor_data()[1].buffer.addr, moved_temp + i * SLICE_BYTES);
    }
    EXPECT_EQ(
        reinterpret_cast<uint64_t>(bound.task(0).slot.graph_context),
        DEFINITION_BLOCK_BASE + 128 + sizeof(GraphDefinitionHeader)
    );
    // An ordinary task belongs to no Graph, and binding must not invent one.
    EXPECT_EQ(bound.task(1).slot.graph_context, nullptr);
}

// The property the whole split rests on: publication reads the canonical data and
// never writes it, so a second execution starts from the same bytes as the first.
TEST(HbgPreparedCall, BindingLeavesTheCanonicalBytesUntouched) {
    Mirror mirror;
    PreparedCall call = seal(mirror);
    std::vector<std::byte> before(call.image.size());
    std::memcpy(before.data(), call.image.data(), call.image.size());

    Bound first(call, TEMP_BASE, HEAP_REAL_BASE, DEFINITION_BLOCK_BASE);
    Bound second(call, TEMP_BASE + 0x200000, HEAP_REAL_BASE + 0x1000000, DEFINITION_BLOCK_BASE + 0x10000);
    (void)first;
    (void)second;

    EXPECT_EQ(std::memcmp(before.data(), call.image.data(), call.image.size()), 0);
}

// Two executions on two banks: each gets addresses in its own regions, which a
// rebase applied to an already-bound image could not produce.
TEST(HbgPreparedCall, TwoBindingsLandInTheirOwnRegions) {
    Mirror mirror;
    PreparedCall call = seal(mirror);
    Bound first(call, TEMP_BASE, HEAP_REAL_BASE, DEFINITION_BLOCK_BASE);
    const uint64_t second_heap = HEAP_REAL_BASE + 0x1000000;
    Bound second(call, TEMP_BASE, second_heap, DEFINITION_BLOCK_BASE);

    EXPECT_EQ(reinterpret_cast<uint64_t>(first.task(2).task.packed_buffer_base), HEAP_REAL_BASE + 2 * PACKED_STRIDE);
    EXPECT_EQ(reinterpret_cast<uint64_t>(second.task(2).task.packed_buffer_base), second_heap + 2 * PACKED_STRIDE);
}

// Every way a submission can fail to be the one a result was sealed for. Each is
// rejected before anything on the device is touched, and each names itself.
TEST(HbgPreparedCall, RejectsASubmissionTheResultDoesNotDescribe) {
    Mirror mirror;
    PreparedCall call = seal(mirror);
    std::string why;

    EXPECT_TRUE(simpler::hbg::prepared_call_submission_compatible(call, declared_args(), {}, &why)) << why;

    std::vector<PreparedArg> shorter = declared_args();
    shorter.pop_back();
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, shorter, {}, &why));
    EXPECT_NE(why.find("count"), std::string::npos) << why;

    std::vector<PreparedArg> kind_changed = declared_args();
    kind_changed[1].kind = PreparedArgKind::DeviceMemory;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, kind_changed, {}, &why));
    EXPECT_NE(why.find("address space"), std::string::npos) << why;

    std::vector<PreparedArg> direction_changed = declared_args();
    direction_changed[1].direction = ArgDirection::IN;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, direction_changed, {}, &why));
    EXPECT_NE(why.find("direction"), std::string::npos) << why;

    std::vector<PreparedArg> size_changed = declared_args();
    size_changed[2].nbytes = SLICE_BYTES * 2;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, size_changed, {}, &why));
    EXPECT_NE(why.find("size"), std::string::npos) << why;

    std::vector<PreparedArg> slice_moved = declared_args();
    slice_moved[2].slice_offset += 64;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, slice_moved, {}, &why));
    EXPECT_NE(why.find("slice"), std::string::npos) << why;

    // Equal byte counts, different view. Orchestration reads geometry, so this is
    // a different graph and the size check alone would let it through.
    std::vector<PreparedArg> reshaped = declared_args();
    reshaped[2].geometry += 1;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, reshaped, {}, &why));
    EXPECT_NE(why.find("geometry"), std::string::npos) << why;
}

// Scalars are preparation inputs: orchestration can embed one in the graph or
// branch on it, so a changed value bars republication rather than being carried
// per submission.
TEST(HbgPreparedCall, RejectsAChangedScalar) {
    PreparedCall call;
    call.scalars = {7, 11};
    std::string why;

    EXPECT_TRUE(simpler::hbg::prepared_call_submission_compatible(call, {}, {7, 11}, &why)) << why;

    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, {}, {7, 12}, &why));
    EXPECT_NE(why.find("scalar 1"), std::string::npos) << why;

    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, {}, {7}, &why));
    EXPECT_NE(why.find("scalar count"), std::string::npos) << why;
}

// The geometry digest separates the views a byte count cannot, and ignores the
// one field publication is allowed to move.
TEST(HbgPreparedCall, GeometryDigestSeesTheViewButNotTheAddress) {
    ChipTensor base{};
    base.buffer.addr = 0x1000;
    base.buffer.size = 4096;
    base.start_offset = 0;
    base.ndims = 2;
    base.shapes[0] = 32;
    base.shapes[1] = 32;
    base.strides[0] = 32;
    base.strides[1] = 1;
    base.dtype = DataType::FLOAT32;
    base.address_space = AddressSpace::HOST;

    ChipTensor moved = base;
    moved.buffer.addr = 0x900000;
    EXPECT_EQ(simpler::hbg::prepared_arg_geometry_digest(base), simpler::hbg::prepared_arg_geometry_digest(moved));

    // 32x32 and 16x64 cover the same element count, so only geometry separates them.
    ChipTensor reshaped = base;
    reshaped.shapes[0] = 16;
    reshaped.shapes[1] = 64;
    reshaped.strides[0] = 64;
    EXPECT_NE(simpler::hbg::prepared_arg_geometry_digest(base), simpler::hbg::prepared_arg_geometry_digest(reshaped));

    ChipTensor retyped = base;
    retyped.dtype = DataType::FLOAT16;
    EXPECT_NE(simpler::hbg::prepared_arg_geometry_digest(base), simpler::hbg::prepared_arg_geometry_digest(retyped));

    ChipTensor on_device = base;
    on_device.address_space = AddressSpace::DEVICE;
    EXPECT_NE(simpler::hbg::prepared_arg_geometry_digest(base), simpler::hbg::prepared_arg_geometry_digest(on_device));
}

// A caller-owned device address is the one class publication may not move, so a
// submission that changed one cannot be served by a retained result.
TEST(HbgPreparedCall, RejectsAChangedCallerDeviceAddress) {
    PreparedCall call;
    call.args.push_back(PreparedArg{PreparedArgKind::DeviceMemory, ArgDirection::IN, 256, 0, 0, 0x90000000});
    std::vector<PreparedArg> moved = call.args;
    moved[0].device_addr = 0x91000000;

    std::string why;
    EXPECT_FALSE(simpler::hbg::prepared_call_submission_compatible(call, moved, {}, &why));
    EXPECT_NE(why.find("device address"), std::string::npos) << why;
}

// The digest exists to refuse a result whose image strides no longer match the
// runtime's, which no relocation could repair.
TEST(HbgPreparedCall, LayoutDigestSeparatesTaskCapacities) {
    EXPECT_EQ(simpler::hbg::prepared_call_layout_digest(WINDOW), simpler::hbg::prepared_call_layout_digest(WINDOW));
    EXPECT_NE(simpler::hbg::prepared_call_layout_digest(WINDOW), simpler::hbg::prepared_call_layout_digest(WINDOW * 2));
}

// A register is named by index, so it survives reuse of whatever slot built it;
// and a borrow pins the result, so a concurrent release cannot free an image a
// publication is still reading.
TEST(HbgPreparedCall, RegistersOwnOneResultAndABorrowPinsIt) {
    simpler::hbg::prepared_call_registers_clear();
    EXPECT_FALSE(simpler::hbg::prepared_call_register_valid(0));
    EXPECT_TRUE(simpler::hbg::prepared_call_register_valid(1));
    EXPECT_TRUE(simpler::hbg::prepared_call_register_valid(simpler::hbg::kPreparedCallRegisterCount));
    EXPECT_FALSE(simpler::hbg::prepared_call_register_valid(simpler::hbg::kPreparedCallRegisterCount + 1));

    Mirror mirror;
    auto owned = std::make_shared<const PreparedCall>(seal(mirror));
    EXPECT_TRUE(simpler::hbg::prepared_call_register_store(1, owned));
    EXPECT_FALSE(simpler::hbg::prepared_call_register_store(0, owned));

    PreparedCallPtr borrowed = simpler::hbg::prepared_call_register_borrow(1);
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(borrowed.get(), owned.get());

    EXPECT_TRUE(simpler::hbg::prepared_call_register_release(1));
    EXPECT_EQ(simpler::hbg::prepared_call_register_borrow(1), nullptr);
    // Releasing the register dropped its reference, not the result: a publication
    // holding this borrow can still read every byte of it.
    EXPECT_GT(borrowed->image_bytes(), 0u);
    // Releasing an empty register is reported as "nothing was held", not as an error.
    EXPECT_FALSE(simpler::hbg::prepared_call_register_release(1));
    simpler::hbg::prepared_call_registers_clear();
}

// Retention is what the registers hold right now; the counters are process
// totals. Both are reported because they answer different questions.
TEST(HbgPreparedCall, MetricsReportRetentionAndProgressSeparately) {
    simpler::hbg::prepared_call_registers_clear();
    simpler::hbg::prepared_call_metrics_reset();
    EXPECT_EQ(simpler::hbg::prepared_call_metrics().retained_host_bytes, 0u);

    Mirror mirror;
    auto owned = std::make_shared<const PreparedCall>(seal(mirror));
    ASSERT_TRUE(simpler::hbg::prepared_call_register_store(2, owned));
    EXPECT_EQ(simpler::hbg::prepared_call_metrics().retained_host_bytes, owned->retained_host_bytes());

    simpler::hbg::prepared_call_note_host_orchestration();
    simpler::hbg::prepared_call_note_definition_pack();
    simpler::hbg::prepared_call_note_sealed();
    simpler::hbg::prepared_call_note_published(owned->restored_device_bytes(), /*reused=*/false);
    simpler::hbg::prepared_call_note_published(owned->restored_device_bytes(), /*reused=*/true);

    const simpler::hbg::PreparedCallMetrics metrics = simpler::hbg::prepared_call_metrics();
    EXPECT_EQ(metrics.host_orchestration_entries, 1u);
    EXPECT_EQ(metrics.definition_packs, 1u);
    EXPECT_EQ(metrics.calls_sealed, 1u);
    EXPECT_EQ(metrics.publications, 2u);
    EXPECT_EQ(metrics.reused_publications, 1u);
    EXPECT_EQ(metrics.last_restored_device_bytes, owned->restored_device_bytes());

    simpler::hbg::prepared_call_registers_clear();
    EXPECT_EQ(simpler::hbg::prepared_call_metrics().retained_host_bytes, 0u);
    simpler::hbg::prepared_call_metrics_reset();
}

// A result carrying a reuse bar is never republished; the bar names itself so the
// warning a caller sees says which effect could not be replayed.
TEST(HbgPreparedCall, AReuseBarIsReportedAndNamed) {
    PreparedCall call;
    EXPECT_TRUE(call.reusable());
    call.reuse_bar = simpler::hbg::PreparedCallReuseBar::HostOrchestrationWroteDeviceTensor;
    EXPECT_FALSE(call.reusable());
    EXPECT_NE(
        std::string(simpler::hbg::prepared_call_reuse_bar_name(call.reuse_bar)).find("device-memory"), std::string::npos
    );
}
