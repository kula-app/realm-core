
#ifndef REALM_UTIL_SCRATCH_ALLOCATOR_HPP
#define REALM_UTIL_SCRATCH_ALLOCATOR_HPP

#include <stddef.h>
#include <new>
#include <memory>

#include <realm/util/assert.hpp>
#include <realm/util/backtrace.hpp>
#include <realm/util/allocator.hpp>
#include <realm/util/allocation_metrics.hpp>
#include <vector>

namespace realm {
namespace util {

struct ScratchArena;

/// Backing storage for a scratch allocator.
///
/// It is intended that a thread or task owns an instance of this type and
/// reuses it between unrelated (non-overlapping) invocations.
///
/// Ideas for future improvements:
///
///   - Add a runtime parameter `max_size` to set an upper bound on the memory
///     consumption of a single instance of ScratchMemory. This could be useful
///     to limit the allotted amount of memory per thread/task.
///
///   - Transparently support allocations larger than the block size
///     (per-arena).
///
///   - Let `block_size` be a template parameter.
struct ScratchMemory {
    static const size_t block_size = 16 << 20; // 16 MB
    static const size_t alignment = 16;

    ScratchMemory(AllocatorBase& allocator = DefaultAllocator::get_default()) noexcept;
    ~ScratchMemory();

    struct Position {
        size_t block_index = 0;
        size_t offset = 0;

        size_t bytes() const
        {
            return block_index * block_size + offset;
        }
    };

    Position get_current_position() const noexcept;
    Position get_high_mark() const noexcept;


    /// Free currently unused blocks. This function should not be used
    /// overzealously, because it defeats the purpose of the scratch allocator.
    /// However, it can be necessary to control memory usage at certain
    /// checkpoints in the program.
    void shrink_to_fit() noexcept;

private:
    friend struct ScratchArena;

    /// Reset the position in memory, normally in connection with destruction
    /// of an arena. This is a very cheap operation. All objects allocated
    /// through the arena become invalid.
    void reset(const ScratchArena& current_arena, const ScratchArena* previous, Position checkpoint) noexcept;

    /// Set the area as the current arena, and return the previous arena.
    /// ScratchMemory keeps track of the current arena with the sole purpose
    /// of preventing errors. Entering an arena temporarily disables allocation
    /// from a previous arena.
    const ScratchArena* enter_arena(const ScratchArena&) noexcept;

    /// Bump m_position by size, and allocate new blocks if necessary.
    /// Note that blocks are never freed.
    void* allocate(const ScratchArena&, size_t size);

    AllocatorBase& m_allocator;
    Position m_position;
    Position m_high_mark;
    using Block = std::unique_ptr<char[], STLDeleter<char[]>>;
    std::vector<Block> m_blocks;

    const ScratchArena* m_current_arena = nullptr;
};

/// Create a scoped arena based on scratch memory.
///
/// Any previously associated arena for the instance of ScratchMemory will be
/// immutable for the duration of the lifetime of the new instance of
/// ScratchArena.
///
/// Allocating memory through a ScratchArena is very cheap (pointer bump),
/// and freeing memory is a no-op. Therefore you must make sure to manage the
/// lifetime of a ScratchArena, such that it is periodically reset.
struct ScratchArena : AllocatorBase {
    explicit ScratchArena(ScratchMemory&) noexcept;
    ~ScratchArena();

    void* allocate(size_t size, size_t align) noexcept override final;
    void free(void*, size_t size) noexcept override final;

    /// Return the number of bytes that have been "freed" by calls to free().
    /// Use this to gather statistics about usage patterns.
    size_t get_dead_memory() const noexcept;

private:
    ScratchMemory& m_memory;
    const ScratchArena* m_previous = nullptr;
    ScratchMemory::Position m_checkpoint;
    size_t m_dead_memory = 0;

    void* allocate_large(size_t size);
    using LargeBlock = std::unique_ptr<char[], STLDeleter<char[], MeteredAllocator>>;
    std::vector<LargeBlock> m_large_allocations;
};

/// STL-compatible allocator
template <class T>
using ScratchAllocator = STLAllocator<T, ScratchArena>;

// Implementation:

inline bool operator<(const ScratchMemory::Position& a, const ScratchMemory::Position& b)
{
    if (a.block_index == b.block_index)
        return a.offset < b.offset;
    return a.block_index < b.block_index;
}

inline bool operator<=(const ScratchMemory::Position& a, const ScratchMemory::Position& b)
{
    if (a.block_index == b.block_index)
        return a.offset <= b.offset;
    return a.block_index < b.block_index;
}

inline ScratchMemory::ScratchMemory(AllocatorBase& allocator) noexcept
    : m_allocator(allocator)
{
}

inline ScratchMemory::~ScratchMemory()
{
    REALM_ASSERT(m_current_arena == nullptr);
}

inline auto ScratchMemory::get_current_position() const noexcept -> Position
{
    return m_position;
}

inline auto ScratchMemory::get_high_mark() const noexcept -> Position
{
    return m_high_mark;
}

inline void ScratchMemory::shrink_to_fit() noexcept
{
    if (m_blocks.size() > m_position.block_index + 1) {
        m_blocks.erase(m_blocks.begin() + m_position.block_index + 1, m_blocks.end());
    }
}

inline void ScratchMemory::reset(const ScratchArena& current_arena, const ScratchArena* previous_arena,
                                 Position checkpoint) noexcept
{
    REALM_ASSERT(&current_arena == m_current_arena);
    REALM_ASSERT(checkpoint <= m_position);
    m_current_arena = previous_arena;
    if (m_high_mark < m_position)
        m_high_mark = m_position;
    m_position = checkpoint;
}

inline const ScratchArena* ScratchMemory::enter_arena(const ScratchArena& new_arena) noexcept
{
    auto prev = m_current_arena;
    m_current_arena = &new_arena;
    return prev;
}

inline void* ScratchMemory::allocate(const ScratchArena& current_arena, size_t size)
{
    REALM_ASSERT(&current_arena == m_current_arena);

    // Round up to alignment
    size = (size + alignment - 1) / alignment * alignment;
    REALM_ASSERT(size % alignment == 0);

    if (size > block_size)
        throw util::bad_alloc{};

    Position pos;
    if (m_position.block_index < m_blocks.size() && size < block_size - m_position.offset) {
        // Allocation fits in current block
        pos = m_position;
        m_position.offset += size;
    }
    else {
        // Skip to next block
        pos.block_index = m_blocks.size();
        pos.offset = 0;
        m_blocks.emplace_back(util::make_unique<char[]>(m_allocator, block_size)); // Throws
        m_position.block_index = pos.block_index;
        m_position.offset = size;
    }

    char* block = m_blocks[pos.block_index].get();
    return static_cast<void*>(block + pos.offset);
}

inline ScratchArena::ScratchArena(ScratchMemory& memory) noexcept
    : m_memory(memory)
{
    m_previous = m_memory.enter_arena(*this);
    m_checkpoint = memory.get_current_position();
}

inline ScratchArena::~ScratchArena()
{
    m_memory.reset(*this, m_previous, m_checkpoint);
}

inline void* ScratchArena::allocate(size_t size, size_t align) noexcept
{
    static_cast<void>(align); // FIXME
    if (size > ScratchMemory::block_size)
        return allocate_large(size);
    return m_memory.allocate(*this, size);
}

inline void* ScratchArena::allocate_large(size_t size)
{
    m_large_allocations.emplace_back(util::make_unique<char[]>(MeteredAllocator::get_default(), size));
    return m_large_allocations.back().get();
}

inline void ScratchArena::free(void*, size_t size) noexcept
{
    m_dead_memory += size;
    // No-op
}

inline size_t ScratchArena::get_dead_memory() const noexcept
{
    return m_dead_memory;
}

template <class T>
using ScratchDeleter = STLDeleter<T, ScratchArena>;

} // namespace util
} // namespace realm


#endif // REALM_UTIL_SCRATCH_ALLOCATOR_HPP
