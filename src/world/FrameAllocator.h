#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace world
{

class FrameAllocator
{
  public:
    static constexpr std::size_t kDefaultCapacity = 256 * 1024;

    FrameAllocator();
    explicit FrameAllocator(std::size_t capacity);

    FrameAllocator(const FrameAllocator &) = delete;
    FrameAllocator &operator=(const FrameAllocator &) = delete;
    FrameAllocator(FrameAllocator &&) noexcept = default;
    FrameAllocator &operator=(FrameAllocator &&) noexcept = default;

    void reset();

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t used() const noexcept { return m_offset; }

    void *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    template <typename T>
    T *allocateArray(std::size_t count)
    {
        void *memory = allocate(sizeof(T) * count, alignof(T));
        return static_cast<T *>(memory);
    }

    template <typename T>
    struct Allocator
    {
        using value_type = T;
        using propagate_on_container_copy_assignment = std::false_type;
        using propagate_on_container_move_assignment = std::false_type;
        using propagate_on_container_swap = std::false_type;

        FrameAllocator *frameAllocator = nullptr;

        Allocator() noexcept = default;
        explicit Allocator(FrameAllocator &allocator) noexcept : frameAllocator(&allocator) {}

        template <typename U>
        Allocator(const Allocator<U> &other) noexcept : frameAllocator(other.frameAllocator) {}

        [[nodiscard]] T *allocate(std::size_t n)
        {
            if (!frameAllocator)
            {
                throw std::bad_alloc();
            }
            return frameAllocator->allocateArray<T>(n);
        }

        void deallocate(T *, std::size_t) noexcept {}

        template <typename U>
        struct rebind
        {
            using other = Allocator<U>;
        };

        template <typename U>
        bool operator==(const Allocator<U> &other) const noexcept
        {
            return frameAllocator == other.frameAllocator;
        }

        template <typename U>
        bool operator!=(const Allocator<U> &other) const noexcept
        {
            return !(*this == other);
        }
    };

  private:
    std::unique_ptr<std::byte[]> m_storage;
    std::size_t m_capacity;
    std::size_t m_offset;
};

inline FrameAllocator::FrameAllocator()
    : FrameAllocator(kDefaultCapacity)
{
}

inline FrameAllocator::FrameAllocator(std::size_t capacity)
    : m_storage(std::make_unique<std::byte[]>(capacity)),
      m_capacity(capacity),
      m_offset(0)
{
    reset();
}

inline void FrameAllocator::reset()
{
    if (m_storage)
    {
        std::memset(m_storage.get(), 0, m_capacity);
    }
    m_offset = 0;
}

inline void *FrameAllocator::allocate(std::size_t size, std::size_t alignment)
{
    if (alignment == 0)
    {
        alignment = 1;
    }
    const std::size_t alignMask = alignment - 1;
    std::size_t alignedOffset = (m_offset + alignMask) & ~alignMask;
    if (alignedOffset < m_offset)
    {
        throw std::bad_alloc();
    }
    if (size > m_capacity || alignedOffset > m_capacity - size)
    {
        throw std::bad_alloc();
    }

    void *ptr = m_storage.get() + alignedOffset;
    std::memset(ptr, 0, size);
    m_offset = alignedOffset + size;
    return ptr;
}

template <typename T>
using FrameVector = std::vector<T, FrameAllocator::Allocator<T>>;

} // namespace world

