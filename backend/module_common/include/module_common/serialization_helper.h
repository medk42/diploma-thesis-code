#pragma once

#include "module_helpers/serialization_helper/serialization_helper.h"
#include "module_common/module_interface_.h"

#include <vector>

namespace aergo::module
{
    namespace serialize
    {
        namespace ser = aergo::module::helpers::serialization_helper::serialization;

        /// @brief Push existing channels to buffer: [u64 channel_count_][channel_count_ * [u64 module_id_][u32 local_channel_id_]]
        template<class ByteT>
        inline void pushExistingChannels(std::vector<ByteT>& buf, const std::vector<aergo::module::ChannelIdentifier>& channel_list)
        {
            ser::push<uint64_t>(buf, static_cast<uint64_t>(channel_list.size())); // [u64 channel_count_]
            for (const auto& channel_id : channel_list)
            {
                ser::push<uint64_t>(buf, channel_id.module_id_);   // [u64 module_id_]
                ser::push<uint32_t>(buf, channel_id.local_channel_id_);  // [u32 local_channel_id_]
            }
        }
    }

    namespace deserialize
    {
        namespace des = aergo::module::helpers::serialization_helper::deserialization;

        /// @brief Read existing channels from buffer: [u64 channel_count_][channel_count_ * [u64 module_id_][u32 local_channel_id_]]
        inline bool readExistingChannels(des::BufferReader& reader, std::vector<aergo::module::ChannelIdentifier>& out_channel_list)
        {
            uint64_t channel_count = 0;
            if (!reader.read<uint64_t>(channel_count))
            {
                return false; // failed to read channel_count_
            }

            out_channel_list.clear();
            out_channel_list.resize(channel_count);
            for (uint64_t channel_id = 0; channel_id < channel_count; ++channel_id)
            {
                aergo::module::ChannelIdentifier& channel_identifier = out_channel_list[channel_id];
                if (!reader.read<uint64_t>(channel_identifier.module_id_))
                {
                    return false; // failed to read module_id_
                }
                if (!reader.read<uint32_t>(channel_identifier.local_channel_id_))
                {
                    return false; // failed to read local_channel_id_
                }
            }

            return true;
        }
    }
}