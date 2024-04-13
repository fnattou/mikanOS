#include "memory_manager.hpp"
#include "logger.hpp"

namespace {
    BitmapMemoryManager::MapLineType GetBitMask(FrameID frame) {
        unsigned long bit_index = frame.ID() % BitmapMemoryManager::kBitsPerMapLine;
        return static_cast<BitmapMemoryManager::MapLineType>(1) << bit_index;
    }
}
BitmapMemoryManager::BitmapMemoryManager()
    : alloc_map_{}, range_begin_{FrameID{0}}, range_end_{FrameID{kFrameCount}}
{ }

WithError<FrameID> BitmapMemoryManager::Allocate(size_t num_frames) {
    //currentFrameIDから最大num_frames個まで空きフレーム数を数える
    auto countContinuousFreeFrame = [this, num_frames](size_t currentFrameID) -> size_t {
        for (size_t i = 0; i < num_frames; ++i) {
            if (currentFrameID + i >= range_end_.ID()) {
                return -1;
            }
            if (GetBit(FrameID{currentFrameID + i})) {
                // このフレームは割り当て済み
                return i;
            }
        }
        return num_frames;
    };

    size_t current_start_frame_id = range_begin_.ID();
    while(true) {
        size_t result = countContinuousFreeFrame(current_start_frame_id);
        if (result == num_frames) {
            MarkAllocated(FrameID{current_start_frame_id}, num_frames);
            return {FrameID{current_start_frame_id}, MAKE_ERROR(Error::kSuccess)};
        }
        if (result == -1) {
            return {kNullFrame, MAKE_ERROR(Error::kNoEnoughMemory)};
        }
        //次のフレームから再探索
        current_start_frame_id += result + 1;
    }
}

Error BitmapMemoryManager::Free(FrameID start_frame, size_t num_frames) {
    for (size_t i = 0; i < num_frames; ++i) {
        SetBit(FrameID{start_frame.ID() + i}, false);
    }
    return MAKE_ERROR(Error::kSuccess);
}

void BitmapMemoryManager::MarkAllocated(FrameID start_frame, size_t num_frames) {
    for (size_t i = 0; i < num_frames; ++i) {
        SetBit(FrameID{start_frame.ID() + i}, true);
    }
}

void BitmapMemoryManager::SetMemoryRange(FrameID range_begin, FrameID range_end) {
    range_begin_ = range_begin;
    range_end_ = range_end;
}

bool BitmapMemoryManager::GetBit(FrameID frame) const {
    unsigned long line_index = frame.ID() / kBitsPerMapLine;
    MapLineType bitMask = GetBitMask(frame);
    return (alloc_map_[line_index] & bitMask) != 0;
}

void BitmapMemoryManager::SetBit(FrameID frame, bool allocated) {
    unsigned long line_index = frame.ID() / kBitsPerMapLine;
    MapLineType bitMask = GetBitMask(frame);

    if (allocated) {
        alloc_map_[line_index] |= bitMask;
    } else {
        alloc_map_[line_index] &= ~(bitMask);
    }
}

extern "C" caddr_t program_break, program_break_end;

namespace {
    char memory_manager_buf[sizeof(BitmapMemoryManager)];

    Error InitializeHeap(BitmapMemoryManager& memory_manager) {
        const int kHeapFrames = 64 * 512;
        const auto heap_start = memory_manager.Allocate(kHeapFrames);
        if (heap_start.error) {
            return heap_start.error;
        }

        program_break = reinterpret_cast<caddr_t>(heap_start.value.ID() * kBytesPerFrame);
        program_break_end = program_break + kHeapFrames * kBytesPerFrame;
        return MAKE_ERROR(Error::kSuccess);
    }
}

BitmapMemoryManager* memory_manager;

void InitializeMemoryManager(const MemoryMap& memory_map) {
    ::memory_manager = new(memory_manager_buf) BitmapMemoryManager;
    const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);
    //      a_end          p_start   p_end = next a_end
    // ...-----|--------------|--------|----...
    //          <------------> <------>
    //                ||          ||
    //          (Mark as Used)   (Mark as Used if not available)
    uintptr_t available_end = 0;
    for (uintptr_t iter = memory_map_base;
    iter < memory_map_base + memory_map.map_size; iter += memory_map.descriptor_size) {
        auto desc = reinterpret_cast<const MemoryDescriptor*>(iter);
        if (available_end < desc->physical_start) {
            memory_manager->MarkAllocated(
                FrameID{available_end / kBytesPerFrame},
                (desc->physical_start - available_end) / kBytesPerFrame
            );
        }

        const auto physical_end = desc->physical_start + desc->number_of_pages * kUEFIPageSize;
        if (IsAvailable(static_cast<MemoryType>(desc->type))) {
            available_end = physical_end;
        }
        else {
            memory_manager->MarkAllocated(
                FrameID{desc->physical_start / kBytesPerFrame},
                desc->number_of_pages * kUEFIPageSize / kBytesPerFrame
            );
        }
    }
    memory_manager->SetMemoryRange(FrameID{1}, FrameID{available_end / kBytesPerFrame});

    if (auto err = InitializeHeap(*memory_manager)) {
        Log(kError, "failed to allocate pages: %s at %s:%d\n", 
            err.Name(), err.File(), err.Line());
        exit(1);
    }
}