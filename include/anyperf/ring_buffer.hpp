#pragma once

#include <vector>
#include <cstddef>
#include <algorithm>

namespace anyperf {

template <typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer() : data_(Capacity, T{}), head_(0), size_(0) {}

    void push(const T& item) {
        data_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        if (size_ < Capacity) {
            size_++;
        }
    }

    void clear() {
        head_ = 0;
        size_ = 0;
        std::fill(data_.begin(), data_.end(), T{});
    }

    size_t size() const { return size_; }
    size_t capacity() const { return Capacity; }
    bool empty() const { return size_ == 0; }

    // Returns element at logical index (0 is oldest, size - 1 is newest)
    T at(size_t index) const {
        if (index >= size_) return T{};
        size_t actual_idx = (Capacity + head_ - size_ + index) % Capacity;
        return data_[actual_idx];
    }

    T latest() const {
        if (size_ == 0) return T{};
        size_t idx = (head_ + Capacity - 1) % Capacity;
        return data_[idx];
    }

    // Exports contiguous sequential data (from oldest to newest) into output vector
    void get_linear(std::vector<T>& out) const {
        out.resize(size_);
        for (size_t i = 0; i < size_; ++i) {
            out[i] = at(i);
        }
    }

    // Returns raw pointer for ImPlot stride/offset if needed
    const T* raw_data() const { return data_.data(); }
    size_t raw_offset() const { return size_ < Capacity ? 0 : head_; }

private:
    std::vector<T> data_;
    size_t head_ = 0;
    size_t size_ = 0;
};

} // namespace anyperf
