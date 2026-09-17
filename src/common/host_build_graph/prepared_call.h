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
 * host_build_graph prepared call: the owned host result of one host orchestration.
 *
 * A bind has two halves. The first runs the orchestration entry and ends at a
 * `PreparedCall` — a canonical, unbound, immutable description of the graph that
 * owns every byte it needs and borrows nothing from the runner's per-slot scratch.
 * The second binds that description to one arena bank and publishes it. Ordinary
 * `Worker.run` does both and drops the result; a caller that names a prepared-call
 * register keeps it and publishes it again, which runs the same graph with fresh
 * working state and no second orchestration.
 *
 * Canonical means four things, and each is what makes the result bank-independent:
 *
 *   - every heap address in `image` is still in the HEAP_VIRTUAL_BASE window, so
 *     `sm_layout::rebase_image` can bind it to whichever heap the execution's bank
 *     committed;
 *   - every outer Graph task's `graph_context` is null, and `definition_bindings`
 *     says which Definition object each one must point at once the block has a
 *     device address;
 *   - every tensor addressing a caller-argument slice is listed in `arg_slices` by
 *     its offset from the retained temp buffer's base, so a different slot's buffer
 *     — or a grown one — is bound by rewriting those fields;
 *   - `args` records what the submission that built this result passed, so a later
 *     submission can be rejected before anything on the device is touched.
 *
 * Publication only ever reads a `PreparedCall`. A rejected or partly-failed
 * publication therefore cannot damage it, and a retry starts from a state that has
 * already executed.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "host_build_graph/runtime_core.h"
#include "host_build_graph/shared_memory.h"
#include "task_interface/arg_direction.h"
#include "task_interface/tensor.h"
#include "worker/runtime_c_api.h"

namespace simpler::hbg {

// A byte buffer whose base satisfies CHIP_ALIGN_SIZE. Every shared-memory segment
// offset is CHIP_ALIGN_SIZE-aligned and ChipTaskStorage is alignas(64), neither of
// which a byte vector's data() guarantees, so the storage is over-allocated and the
// base rounded up.
//
// The round-up is recomputed on every access rather than cached as an offset: a
// cached one is derived from the allocation's address, so a copy of this object —
// whose vector may land on a different alignment — would name a base that is not
// aligned.
class AlignedBuffer {
public:
    void assign(const char *source, size_t bytes);
    void resize(size_t bytes);

    char *data() noexcept { return align(storage_.empty() ? nullptr : storage_.data()); }
    const char *data() const noexcept { return align(storage_.empty() ? nullptr : storage_.data()); }
    size_t size() const noexcept { return bytes_; }
    size_t host_bytes() const noexcept { return storage_.size(); }
    bool empty() const noexcept { return bytes_ == 0; }

private:
    template <typename Byte>
    static auto align(Byte *base) noexcept -> std::conditional_t<std::is_const_v<Byte>, const char *, char *> {
        using Result = std::conditional_t<std::is_const_v<Byte>, const char *, char *>;
        if (base == nullptr) return nullptr;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(base);
        return reinterpret_cast<Result>((addr + CHIP_ALIGN_SIZE - 1) & ~static_cast<uintptr_t>(CHIP_ALIGN_SIZE - 1));
    }

    std::vector<std::byte> storage_;
    size_t bytes_{0};
};

// Why a sealed result may not be published a second time. The fresh path is
// unaffected by any of these — they only bar reuse.
enum class PreparedCallReuseBar : uint32_t {
    None = 0,
    // Host orchestration wrote through HostTensorAccessor into a caller tensor that
    // lives in device memory. That write landed in memory the caller owns and no
    // per-submission transfer reproduces it, so the effect cannot be replayed.
    HostOrchestrationWroteDeviceTensor = 1,
};

const char *prepared_call_reuse_bar_name(PreparedCallReuseBar bar) noexcept;

// One outer Graph task's Definition object, named by position in the block rather
// than by address so the block may be reallocated between executions.
struct DefinitionBinding {
    uint64_t task_index;
    uint64_t object_offset;  // of the object's header, from the block base
};

// One tensor field in the image that addresses a caller-argument slice.
struct ArgSliceRelocation {
    uint64_t task_index;
    int32_t tensor_index;
    uint64_t slice_offset;  // from the retained temp buffer's base
};

enum class PreparedArgKind : uint8_t {
    // Addresses nothing: a zero-byte tensor carries a null address rather than one
    // aliasing its neighbour's.
    Empty = 0,
    // A caller host buffer the bind copies into a retained-temp-buffer slice.
    HostMemory = 1,
    // Device memory the caller owns. Its address is passed through untouched and
    // must be the same on every submission of this result.
    DeviceMemory = 2,
};

// What one orchestration tensor argument was, as the sealing submission passed it.
struct PreparedArg {
    PreparedArgKind kind{PreparedArgKind::Empty};
    ArgDirection direction{ArgDirection::INOUT};
    uint64_t nbytes{0};
    // Everything about the argument a graph can be built from except its address:
    // shape, strides, dtype, element offset and address space. Two arguments of
    // equal byte size can still be different geometry, and orchestration reads
    // geometry to shape the graph.
    uint64_t geometry{0};
    uint64_t slice_offset{0};  // HostMemory: from the retained temp buffer's base
    uint64_t device_addr{0};   // DeviceMemory: the address orchestration saw
};

// Digest of one argument's geometry, for PreparedArg::geometry. Built from the
// named fields rather than the struct's bytes: a wire struct has padding, and
// padding a caller left indeterminate would make an identical submission look
// different.
uint64_t prepared_arg_geometry_digest(const ChipTensor &tensor) noexcept;

// Arch-private preparation output (a5 keeps its AICore scheduler plan here). Opaque
// to everything shared, so neither arch's representation leaks into the other's.
struct PreparedCallArchPayload {
    PreparedCallArchPayload() = default;
    virtual ~PreparedCallArchPayload() = default;
    PreparedCallArchPayload(const PreparedCallArchPayload &) = delete;
    PreparedCallArchPayload &operator=(const PreparedCallArchPayload &) = delete;
};

struct PreparedCall {
    // ---- identity and compatibility ------------------------------------------
    // Content-derived callable identity, so a result can never be published for a
    // different graph even if it would fit.
    uint64_t callable_hash{0};
    // Fingerprint of the struct shapes and constants this result was built against.
    uint64_t layout_digest{0};
    uint64_t task_capacity{0};
    int32_t total_tasks{0};

    // ---- the canonical image and the arena zone that travels with it ---------
    AlignedBuffer image;
    sm_layout::BindUsage usage{};
    std::vector<std::byte> copied_zone;

    // ---- what the device must provide ---------------------------------------
    RuntimeArenaLayout arena_layout{};
    uint64_t sm_size{0};
    uint64_t heap_bytes{0};
    uint64_t device_arena_bytes{0};

    // ---- Definition objects, owned ------------------------------------------
    std::vector<std::byte> definition_block;
    size_t definition_count{0};
    size_t definition_spills{0};
    std::vector<DefinitionBinding> definition_bindings;

    // ---- per-execution binding ----------------------------------------------
    std::vector<PreparedArg> args;
    // Orchestration reads scalar arguments and can embed them in the graph or
    // branch on them, so they are preparation inputs and a changed one bars
    // republication rather than being carried per submission.
    std::vector<uint64_t> scalars;
    uint64_t temp_bytes{0};
    std::vector<ArgSliceRelocation> arg_slices;

    std::unique_ptr<PreparedCallArchPayload> arch;

    PreparedCallReuseBar reuse_bar{PreparedCallReuseBar::None};

    bool reusable() const noexcept { return reuse_bar == PreparedCallReuseBar::None; }
    uint64_t image_bytes() const noexcept { return image.size(); }
    // Bytes this result holds on the host, for the retention accounting.
    uint64_t retained_host_bytes() const noexcept;
    // Bytes publication writes to the device, for the restore accounting.
    uint64_t restored_device_bytes() const noexcept;
};

using PreparedCallPtr = std::shared_ptr<const PreparedCall>;

// The shapes and constants a result is only valid against. A mismatch means the
// image's strides or the arena's internals moved, which no relocation repairs.
uint64_t prepared_call_layout_digest(uint64_t task_capacity) noexcept;

// Record every tensor field in the canonical image that addresses a slice of
// [temp_base, temp_base + temp_bytes), as an offset from temp_base.
//
// `call.args` must already hold the submission's argument contract: each relocation
// has to fall inside one declared HostMemory slice, which is what proves the list is
// complete rather than merely non-empty. Returns false when an address inside the
// span belongs to no declared slice, in which case the result must not be sealed —
// a missed field would keep a stale address across an execution that moved the
// buffer.
bool prepared_call_collect_arg_slices(PreparedCall &call, uint64_t temp_base, uint64_t temp_bytes);

// Write the working image for one execution into `out` (at least
// `call.image_bytes()` bytes, CHIP_ALIGN_SIZE-aligned).
//
// Copies the canonical bytes, then binds the three address classes in the order
// their windows require: caller-argument slices by list, the graph heap by window,
// and the Definition objects by position. `call` is untouched.
void prepared_call_bind_image(
    const PreparedCall &call, char *out, uint64_t temp_base, const sm_layout::HeapRebase &heap,
    uint64_t definition_block_base
);

// Whether `submission` can be served by `call`'s canonical data. Compares
// argument count, kind, direction, size, geometry, slice placement, the scalar
// values orchestration may have built the graph from, and — for device memory —
// the caller address, which no relocation may move. `why` receives a
// caller-facing reason on a mismatch.
bool prepared_call_submission_compatible(
    const PreparedCall &call, const std::vector<PreparedArg> &submission, const std::vector<uint64_t> &scalars,
    std::string *why
);

// =============================================================================
// Prepared-call registers
// =============================================================================
//
// A retained result is named by a small 1-based index rather than by an address or a
// pipeline slot: it has to survive reuse of the slot that built it, and a caller has
// to be able to ask for it again without holding a pointer into the runtime .so.
// Index 0 means "no register" — the ordinary path, which seals a result, publishes
// it once and drops it.

inline constexpr uint32_t kPreparedCallRegisterCount = 8;
static_assert(
    kPreparedCallRegisterCount == SIMPLER_PREPARED_CALL_REGISTER_MAX,
    "the register table must cover exactly the range a NativeRunDescriptor may name"
);

bool prepared_call_register_valid(uint32_t reg) noexcept;
// The result held in `reg`, pinned for as long as the caller holds the pointer, or
// null when the register is empty. Pinning is the point: a concurrent release must
// not free an image a publication is still reading.
PreparedCallPtr prepared_call_register_borrow(uint32_t reg);
bool prepared_call_register_store(uint32_t reg, PreparedCallPtr call);
bool prepared_call_register_release(uint32_t reg);
// Drop every register. Called when the code a result was built against goes away
// — a callable unregistration or a device finalize — because a retained image
// names its kernels by func_id and would otherwise be republished against
// whatever the next callable puts in that table.
void prepared_call_registers_clear() noexcept;

// =============================================================================
// Metrics
// =============================================================================
//
// `host_orchestration_entries` is the assertion a reuse test needs: publishing a
// retained result must not advance it. The byte counts are reported separately
// because they answer different questions — what a retained result costs in host
// memory, and what one execution writes to the device.

struct PreparedCallMetrics {
    uint64_t host_orchestration_entries{0};
    uint64_t definition_packs{0};
    uint64_t calls_sealed{0};
    uint64_t publications{0};
    uint64_t reused_publications{0};
    uint64_t retained_host_bytes{0};
    uint64_t last_restored_device_bytes{0};
};

PreparedCallMetrics prepared_call_metrics();
void prepared_call_metrics_reset() noexcept;
void prepared_call_note_host_orchestration() noexcept;
void prepared_call_note_definition_pack() noexcept;
void prepared_call_note_sealed() noexcept;
void prepared_call_note_published(uint64_t restored_device_bytes, bool reused) noexcept;

}  // namespace simpler::hbg
