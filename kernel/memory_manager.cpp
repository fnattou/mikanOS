#include "memory_manager.hpp"
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