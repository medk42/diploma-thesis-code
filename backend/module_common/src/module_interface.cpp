#include "module_common/module_interface_.h"

#include <utility>

using namespace aergo::module::message;
using namespace aergo::module;



SharedDataBlob::SharedDataBlob()
: data_(nullptr), allocator_(nullptr) {}

SharedDataBlob::SharedDataBlob(ISharedData* data, IAllocator* allocator)
: data_(data), allocator_(allocator)
{
    if (allocator_)
    {
        allocator_->addOwner(data_);
    }
}



SharedDataBlob::~SharedDataBlob()
{
    if (allocator_)
    {
        allocator_->removeOwner(data_);
    }
}



SharedDataBlob::SharedDataBlob(const SharedDataBlob& other)
: data_(other.data_), allocator_(other.allocator_)
{
    if (allocator_)
    {
        allocator_->addOwner(data_);
    }
}



SharedDataBlob& SharedDataBlob::operator=(SharedDataBlob& other)
{
    if (this != &other)
    {
        if (allocator_)
        {
            allocator_->removeOwner(data_);
        }
        allocator_ = other.allocator_;
        data_ = other.data_;
        if (allocator_)
        {
            allocator_->addOwner(data_);
        }
    }

    return *this;
}



SharedDataBlob::SharedDataBlob(SharedDataBlob&& other) noexcept
: data_(other.data_), allocator_(other.allocator_)
{
    other.data_ = nullptr;
    other.allocator_ = nullptr;
}



SharedDataBlob& SharedDataBlob::operator=(SharedDataBlob&& other) noexcept
{
    if (this != &other)
    {
        data_ = other.data_;
        allocator_ = other.allocator_;

        other.data_ = nullptr;
        other.allocator_ = nullptr;
    }

    return *this;
}



bool SharedDataBlob::valid()
{
    return allocator_ != nullptr && data_ != nullptr && data_->valid();
}



uint8_t* SharedDataBlob::data()
{
    return data_->data();
}



uint64_t SharedDataBlob::size()
{
    return data_->size();
}



bool save_toolkit::serializeSaveState(const std::string& json_header, const std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& extra_blobs, std::vector<uint8_t>& out_data) noexcept
{
    auto serialize = [&](const void* data, size_t size) {
        const uint8_t* byte_data = reinterpret_cast<const uint8_t*>(data);
        out_data.insert(out_data.end(), byte_data, byte_data + size);
    };

    size_t json_header_size = json_header.size();
    serialize(&json_header_size, sizeof(json_header_size));
    serialize(json_header.data(), json_header_size);

    size_t extra_data_count = extra_blobs.size();
    serialize(&extra_data_count, sizeof(extra_data_count));
    for (const auto& [instance_name, blobs] : extra_blobs)
    {
        size_t instance_name_size = instance_name.size();
        serialize(&instance_name_size, sizeof(instance_name_size));
        serialize(instance_name.data(), instance_name_size);

        size_t blob_count = blobs.size();
        serialize(&blob_count, sizeof(blob_count));
        for (const auto& blob : blobs)
        {
            size_t name_size = blob.name_.size();
            serialize(&name_size, sizeof(name_size));
            serialize(blob.name_.data(), name_size);

            size_t blob_size = blob.data_.size();
            serialize(&blob_size, sizeof(blob_size));
            serialize(blob.data_.data(), blob_size);
        }
    }

    return true;
}



bool save_toolkit::deserializeSaveState(const uint8_t* data, uint64_t data_size, std::string& out_json_header, std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& out_blobs) noexcept
{
    auto read_data = [&](void* dest, size_t size) -> bool
    {
        if (data_size < size)
        {
            return false;
        }
        std::memcpy(dest, data, size);
        data += size;
        data_size -= size;
        return true;
    };

    size_t json_header_size;
    if (!read_data(&json_header_size, sizeof(json_header_size)))
    {
        return false;
    }

    if (json_header_size > 0)
    {
        out_json_header.resize(json_header_size);
        if (!read_data(out_json_header.data(), json_header_size))
        {
            return false;
        }
    }
    else
    {
        out_json_header.clear();
    }

    size_t extra_data_count;
    if (!read_data(&extra_data_count, sizeof(extra_data_count)))
    {
        return false;
    }
    out_blobs.clear();
    for (size_t i = 0; i < extra_data_count; ++i)
    {
        size_t instance_name_size;
        if (!read_data(&instance_name_size, sizeof(instance_name_size)))
        {
            return false;
        }

        std::string instance_name;
        if (instance_name_size > 0)
        {
            instance_name.resize(instance_name_size);
            if (!read_data(instance_name.data(), instance_name_size))
            {
                return false;
            }
        }
        else
        {
            instance_name.clear();
        }

        size_t blob_count;
        if (!read_data(&blob_count, sizeof(blob_count)))
        {
            return false;
        }

        std::vector<aergo::module::ISerializableModule::SavedBlob> blobs;
        blobs.clear();
        for (size_t j = 0; j < blob_count; ++j)
        {
            size_t name_size;
            if (!read_data(&name_size, sizeof(name_size)))
            {
                return false;
            }

            std::string name;
            if (name_size > 0)
            {
                name.resize(name_size);
                if (!read_data(name.data(), name_size))
                {
                    return false;
                }
            }
            else
            {
                name.clear();
            }

            size_t blob_size;
            if (!read_data(&blob_size, sizeof(blob_size)))
            {
                return false;
            }

            std::vector<uint8_t> blob_data;
            if (blob_size > 0)
            {
                blob_data.resize(blob_size);
                if (!read_data(blob_data.data(), blob_size))
                {
                    return false;
                }
            }
            else
            {
                blob_data.clear();
            }

            blobs.push_back(aergo::module::ISerializableModule::SavedBlob {
                .name_ = std::move(name),
                .data_ = std::move(blob_data),
            });
        }

        out_blobs.push_back(std::make_tuple(std::move(instance_name), std::move(blobs)));
    }

    return true;
}