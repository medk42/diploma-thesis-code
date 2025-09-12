#pragma once

#include <variant>
#include <optional>
#include <string>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    using value_t = std::variant<
        bool,          // boolean or custom channel captured flag
        int64_t,       // long
        double,        // double
        std::string,   // string
        int            // enum index (or -1 for no selection)
    >;
    using value_opt_t = std::optional<
        value_t
    >;
}