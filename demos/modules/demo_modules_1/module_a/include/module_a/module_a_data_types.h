#pragma once

#include "stdint.h"

namespace aergo::demo_modules_1::module_a::messages
{
    namespace publish_consume
    {
        struct SmallMessage
        {
            int32_t counter1_;
            uint8_t counter2_;
        };

        struct LargeMessage
        {
            uint8_t counter_start_;
        };
    }

    namespace request_response
    {
        struct LargeVariableResp
        {};

        struct LargeVariableReq
        {
            int32_t requested_size_;
            uint8_t counter_start_;
        };
    }
}