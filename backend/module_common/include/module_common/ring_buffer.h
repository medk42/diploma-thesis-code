#pragma once

#include <vector>
#include <concepts>
#include <optional>

namespace aergo::module
{
    template<typename T> requires 
        std::default_initializable<T> && 
        std::is_move_constructible_v<T> && 
        std::is_move_assignable_v<T>
    class RingBuffer
    {
    public:
        explicit RingBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity), head_(0), size_(0) {}

        std::optional<T> tryPush(T item)
        {
            if (full()) return std::move(item); // buffer full

            size_t tail = (head_ + size_) % capacity_;
            buffer_[tail] = std::move(item);
            ++size_;

            return std::nullopt;
        }

        std::optional<T> tryPop()
        {
            if (empty()) return std::nullopt; // buffer empty

            size_t head = head_;
            head_ = (head_ + 1) % capacity_;
            --size_;

            return std::move(buffer_[head]);
        }

        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }
        bool empty() const { return size_ == 0; }
        bool full() const { return size_ == capacity_; }

    private:
        size_t capacity_;
        std::vector<T> buffer_;
        size_t head_;
        size_t size_;
    };
}