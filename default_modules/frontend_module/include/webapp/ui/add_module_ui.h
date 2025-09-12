#pragma once


#include "module_common/module_interface_.h"
#include "helper/right_module_view.h"
#include "helper/reusable_dialog.h"

#include <vector>
#include <tuple>
#include <memory>
#include <unordered_map>

#include <Wt/WContainerWidget.h>
#include <Wt/WStackedWidget.h>
#include <Wt/WSignal.h>


namespace aergo::default_modules::frontend_module::webapp::ui
{
    class AddModuleUi : public Wt::WContainerWidget
    {
    public:
        struct ChannelInfo;
        struct AddModuleData;
        struct RunningModuleInfo;

        AddModuleUi(const std::vector<const aergo::module::ModuleInfo*>& available_modules);

        void updateRunningModules(
            std::unordered_map<std::string, std::vector<ChannelInfo>>& publish_module_lookup,
            std::unordered_map<std::string, std::vector<ChannelInfo>>& response_module_lookup
        ); // update the displayed enum values and corresponding vectors; module_lookup maps channel type to available modules and their channel

        Wt::Signal<>& onClose() { return onClose_; }
        Wt::Signal<size_t, AddModuleData>& onCreateModule() { return onCreateModule_; } // index in available_modules, creation data

    private:

        void showDialog(std::unique_ptr<helper::ReusableDialog> dialog); // all buttons and background clicks close the dialog
        void emitCreateModule(size_t available_module_index);

        Wt::WStackedWidget* details_view_container_{ nullptr };
        std::vector<helper::RightModuleView*> detail_views_; // one per available module, indexed by inner module index (i.e. without auto-create modules)

        helper::ReusableDialog* parameter_dialog_ { nullptr };

        std::vector<const aergo::module::ModuleInfo*> available_modules_;

        std::vector<size_t> available_module_id_to_inner_index_; // maps module id to index, because auto-create modules are not displayed
        std::vector<std::vector<uint32_t>> subscribe_consumer_id_to_inner_index_; // maps subscribe consumer id to index, because AUTO_ALL consumers are not be displayed (outer vector indexed by inner module index)
        std::vector<std::vector<uint32_t>> request_consumer_id_to_inner_index_;   // maps request consumer id to index, because AUTO_ALL consumers are not be displayed (outer vector indexed by inner module index)
        std::vector<std::tuple<bool, bool>> module_index_to_section_presence_; // for each module (by inner index), whether it has a subscribe section and whether it has a request section
        std::vector<std::vector<std::vector<ChannelInfo>>> subscribe_enum_to_channel_info_; // for each module (by inner index), for each parameter in the subscribe section (by inner index), for each enum value, the mapped channel info
        std::vector<std::vector<std::vector<ChannelInfo>>> request_enum_to_channel_info_;   // for each module (by inner index), for each parameter in the request section (by inner index), for each enum value, the mapped channel info

        Wt::Signal<> onClose_;
        Wt::Signal<size_t, AddModuleData> onCreateModule_;
    };

    struct AddModuleUi::ChannelInfo
    {
        size_t available_module_index_;  // index in available modules list
        size_t running_module_index_;    // index of instance of this module
        uint32_t channel_id_;  // id of the channel within the module
    };

    struct AddModuleUi::AddModuleData
    {
        std::vector<std::vector<ChannelInfo>> subscribe_channels_; // for each subscribe consumer, one or more mapped channels
        std::vector<std::vector<ChannelInfo>> request_channels_;   // for each request consumer, one or more mapped channels
    };
}