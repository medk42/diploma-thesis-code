#include "module_common/dll_interface_threads.h"

using namespace aergo::module::dll;


bool save_toolkit::serialize(const aergo::module::ISerializableModule::SaveData& save_data, std::vector<uint8_t>& out_serialized_data)
{
    auto push_data = [&](const void* data, size_t size)
    {
        const uint8_t* byte_data = static_cast<const uint8_t*>(data);
        out_serialized_data.insert(out_serialized_data.end(), byte_data, byte_data + size);
    };

    uint8_t supports_saving = save_data.supports_saving_ ? 1 : 0;
    push_data(&supports_saving, sizeof(supports_saving));
    push_data(&save_data.schema_version_, sizeof(save_data.schema_version_));
    size_t json_header_size = save_data.json_header_.size();
    push_data(&json_header_size, sizeof(json_header_size));
    if (json_header_size > 0)
    {
        push_data(save_data.json_header_.data(), json_header_size);
    }
    size_t blob_count = save_data.blobs_.size();
    push_data(&blob_count, sizeof(blob_count));
    for (const auto& blob : save_data.blobs_)
    {
        size_t name_size = blob.name_.size();
        push_data(&name_size, sizeof(name_size));
        if (name_size > 0)
        {
            push_data(blob.name_.data(), name_size);
        }
        size_t data_size = blob.data_.size();
        push_data(&data_size, sizeof(data_size));
        if (data_size > 0)
        {
            push_data(blob.data_.data(), data_size);
        }
    }

    return true;
}



bool save_toolkit::deserialize(const uint8_t* data, uint64_t data_size, aergo::module::ISerializableModule::SaveData& out_data)
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

    uint8_t supports_saving;
    if (!read_data(&supports_saving, sizeof(supports_saving)))
    {
        return false;
    }
    out_data.supports_saving_ = (supports_saving != 0);

    if (!read_data(&out_data.schema_version_, sizeof(out_data.schema_version_)))
    {
        return false;
    }

    size_t json_header_size;
    if (!read_data(&json_header_size, sizeof(json_header_size)))
    {
        return false;
    }

    if (json_header_size > 0)
    {
        out_data.json_header_.resize(json_header_size);
        if (!read_data(out_data.json_header_.data(), json_header_size))
        {
            return false;
        }
    }

    size_t blob_count;
    if (!read_data(&blob_count, sizeof(blob_count)))
    {
        return false;
    }

    out_data.blobs_.clear();
    for (size_t i = 0; i < blob_count; ++i)
    {
        size_t name_size;
        if (!read_data(&name_size, sizeof(name_size)))
        {
            return false;
        }

        aergo::module::ISerializableModule::SavedBlob blob;
        if (name_size > 0)
        {
            blob.name_.resize(name_size);
            if (!read_data(blob.name_.data(), name_size))
            {
                return false;
            }
        }

        size_t data_size;
        if (!read_data(&data_size, sizeof(data_size)))
        {
            return false;
        }

        if (data_size > 0)
        {
            blob.data_.resize(data_size);
            if (!read_data(blob.data_.data(), data_size))
            {
                return false;
            }
        }

        out_data.blobs_.push_back(std::move(blob));
    }

    return true;
}