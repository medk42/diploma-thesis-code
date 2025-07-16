#include "utils/memory_allocation/memory_allocator.h"

#include <cstdlib>

using namespace aergo::core::memory_allocation;



void* DefaultAllocator::malloc(size_t size)
{
    return std::malloc(size);
}



void DefaultAllocator::free(void* block)
{
    std::free(block);
}