#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <set>

#include "module_common/module_interface_.h"

using namespace aergo::module;


class TestSharedData : public ISharedData
{
public:
    TestSharedData(bool valid, uint8_t* data, uint64_t size)
    : valid_(valid), data_(data), size_(size) { }

    bool valid() noexcept override { return valid_; }

    uint8_t* data() noexcept override { return data_; }

    uint64_t size() noexcept override { return size_; }
    
    uint64_t ref_count_ = 0;
private:
    bool valid_;
    uint8_t* data_;
    uint64_t size_;
};



class TestAllocator : public IAllocator
{
public:
    virtual message::SharedDataBlob allocate(uint64_t number_of_bytes) noexcept override
    {
        number_of_bytes_set_.insert(number_of_bytes); // to easily track the allocated data from the test
        std::cout << "\tAllocating " << number_of_bytes << " bytes with id " << alloc_id_ << std::endl;
        return message::SharedDataBlob(new TestSharedData(true, (uint8_t*)alloc_id_++, number_of_bytes), this);
    }

    bool exists(uint64_t number_of_bytes)
    {
        std::cout << "Checking existence of " << number_of_bytes << " exists: " << (number_of_bytes_set_.contains(number_of_bytes) ? "exists" : "DOES NOT EXIST") << std::endl;
        return number_of_bytes_set_.contains(number_of_bytes);
    }

protected:
    virtual void addOwner(ISharedData* data) noexcept override
    {
        std::cout << "\t\tAdding owner to data with id " << (uint64_t)data->data() << ", new ref count " << ((TestSharedData*)data)->ref_count_ + 1 << std::endl;
        ((TestSharedData*)data)->ref_count_++;
    }

    virtual void removeOwner(ISharedData* data) noexcept override
    {
        std::cout << "\t\tRemoving owner from data with id " << (uint64_t)data->data() << ", new ref count " << ((TestSharedData*)data)->ref_count_ - 1 << std::endl;
        ((TestSharedData*)data)->ref_count_--;
        if (((TestSharedData*)data)->ref_count_ == 0)
        {
            number_of_bytes_set_.erase((uint64_t)data->size());
            std::cout << "\tDeleting data with id " << (uint64_t)data->data() << std::endl;
            delete data;
        }
    }
private:
    uint64_t alloc_id_ = 0;

    std::set<uint64_t> number_of_bytes_set_;
};



TEST_CASE("SharedDataBlob basic functionality", "[shared_data_blob]")
{
    TestAllocator allocator;

    REQUIRE(!allocator.exists(1));
    allocator.allocate(1);
    REQUIRE(!allocator.exists(1));

    REQUIRE(!allocator.exists(2));
    {
        auto blob = allocator.allocate(2);
        REQUIRE(allocator.exists(2));
    }
    REQUIRE(!allocator.exists(2));

    REQUIRE(!allocator.exists(3));
    {
        message::SharedDataBlob blob3_copy; 
        {
            auto blob3 = allocator.allocate(3);
            REQUIRE(allocator.exists(3));
            blob3_copy = blob3;
            REQUIRE(blob3.data() == blob3_copy.data());
        }
        REQUIRE(allocator.exists(3));

        
        auto b1 = blob3_copy;
        auto b2 = blob3_copy;
        auto b3 = blob3_copy;
        auto b4 = blob3_copy;
        REQUIRE(blob3_copy.data() == b1.data());
        REQUIRE(blob3_copy.data() == b2.data());
        REQUIRE(blob3_copy.data() == b3.data());
        REQUIRE(blob3_copy.data() == b4.data());

        REQUIRE(allocator.exists(3));
    }
    
    REQUIRE(!allocator.exists(3));

    REQUIRE(!allocator.exists(4));
    {
        auto blob4 = allocator.allocate(4);
        uint64_t b4_data = (uint64_t)blob4.data();
        REQUIRE(allocator.exists(4));
        auto blob4_moved = std::move(blob4);
        REQUIRE(allocator.exists(4));
        REQUIRE(b4_data == (uint64_t)blob4_moved.data());
        auto blob5 = std::move(blob4_moved);
        REQUIRE(b4_data == (uint64_t)blob5.data());
        REQUIRE(allocator.exists(4));
        auto blob6 = blob5;
        REQUIRE(b4_data == (uint64_t)blob6.data());
        REQUIRE(blob5.data() == blob6.data());
        REQUIRE(allocator.exists(4));
    }
    REQUIRE(!allocator.exists(4));

}