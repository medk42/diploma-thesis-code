#pragma once

#include "module_interface_.h"

namespace aergo::module::dll
{
    class IDllModule : public IModule
    {
    public:
        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        virtual bool threadStart(uint32_t timeout_ms) noexcept = 0;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        virtual bool threadStop(uint32_t timeout_ms) noexcept = 0;
    };
}