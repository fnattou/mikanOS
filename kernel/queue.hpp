#pragma once 

#include <cstddef>
#include <array>

#include "error.hpp"


template <typename T>
class ArrayQueue {
public:
    template <size_t N>
    ArrayQueue(std::array<T, N>& buf)
    : ArrayQueue(buf.data(), N)
    { }

    ArrayQueue(T* buf, size_t size)
    : data_{buf}, read_pos_{0}, write_pos_{0}, capacity_{size}
    { }

    Error Push(const T& value) {
        if (count_ == capacity_) {
            return MAKE_ERROR(Error::kFull);
        }

        data_[write_pos_] = value;
        ++count_;
        ++write_pos_;
        if (write_pos_ == capacity_) {
            write_pos_ = 0;
        }
        return MAKE_ERROR(Error::kSuccess);
    }

    Error Pop() {
        if (count_ == 0) {
            return MAKE_ERROR(Error::kEmpty);
        }

        --count_;
        ++read_pos_;
        if (read_pos_ == capacity_) {
            read_pos_ = 0;
        }
        return MAKE_ERROR(Error::kSuccess);
    }

    size_t Count() const {
        return count_;
    }
    size_t Capacity() const {
        return capacity_;
    }

    const T& Front() const {
        return data_[read_pos_];
    }
    
private:
    T* data_;
    // points to an element to read
    size_t read_pos_;
    // points to a blank position
    size_t write_pos_;
    // number of elements available
    size_t count_;

    const size_t capacity_;
};
