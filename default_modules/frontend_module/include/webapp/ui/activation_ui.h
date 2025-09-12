#pragma once

#include "helper/parameter_value.h"
#include "module_helpers/activation_wrapper/parameter_description.h"
#include "helper/right_module_view.h"
#include "helper/reusable_dialog.h"
#include "helper/left_module_list.h"

#include <vector>
#include <map>

#include <Wt/WContainerWidget.h>
#include <Wt/WStackedWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui
{
    class ActivationUi : public Wt::WContainerWidget
    {
    public:
        struct ModuleSingleParameter; // identifies module (running module id), parameter (parameter id) and list id within parameter (for list parameters, 0 for non-list parameters)

        ActivationUi();

        /// @brief 
        /// @param running_module_id 
        /// @param module_name 
        /// @param module_description 
        /// @param supports_activation If false, module is always active and cannot be deactivated. If true, module can be activated/deactivated (Activate/Deactivate buttons shown).
        /// @param parameters_delayed If true, parameters will be set later using setParameters(), and no parameters are shown now. If false, parameters are shown now.
        /// @param parameters parameters if supports_activation is true and parameters_delayed is false, otherwise empty vector
        void addModule(uint64_t running_module_id, const char* module_name, const char* module_description, bool supports_activation, bool parameters_delayed, const std::vector<aergo::module::helpers::activation_wrapper::params::ParameterDescription>& parameters);
        void removeModule(uint64_t running_module_id);  // removes module with the specified ID from the list, "running_module_id" never repeats, so this module will never be re-added

        void setParameters(uint64_t running_module_id, const std::vector<aergo::module::helpers::activation_wrapper::params::ParameterDescription>& parameters); // resets all set values to defaults
        bool setParameterValues(uint64_t running_module_id, const std::vector<std::vector<helper::value_t>>& values); // sets all parameters of the module, size of values and inner vectors must match the parameter descriptions, returns true if successful, false if not (e.g. invalid running_module_id or size mismatch)
        void setActive(uint64_t running_module_id, bool active);


        Wt::Signal<>& onClose() { return onClose_; } // close button clicked
        Wt::Signal<>& onSave() { return onSave_; } // save button clicked
        Wt::Signal<>& onLoad() { return onLoad_; } // load button clicked
        Wt::Signal<>& onAddNew() { return onAddNew_; }  // add new module button clicked

        Wt::Signal<uint64_t>& onActivate() { return onActivate_; } // activate button clicked on one of the modules, passes the running module id
        Wt::Signal<uint64_t>& onDeactivate() { return onDeactivate_; } // deactivate button clicked on one of the modules, passes the running module id
        Wt::Signal<uint64_t>& onRemoveModule() { return onRemoveModule_; } // remove module button clicked on one of the modules, passes the running module id

        Wt::Signal<ModuleSingleParameter, helper::value_t>& onParameterChanged() { return onParameterChanged_; } // module parameter changed, passes module+parameter+list ids and the new value
        Wt::Signal<ModuleSingleParameter>& addListEntry() { return addListEntry_; } // add list entry button clicked in parameter, passes module+parameter+list ids (list id is the id of the new entry, i.e. current list size before adding)
        Wt::Signal<ModuleSingleParameter>& removeListEntry() { return removeListEntry_; } // remove list entry button clicked in parameter, passes module+parameter+list ids

    private:
        void showDialog(std::unique_ptr<helper::ReusableDialog> dialog);

        Wt::WStackedWidget* details_view_container_{ nullptr };
        std::vector<helper::RightModuleView*> detail_views_; // one per running module, indexed by inner module index
        helper::LeftModuleList* module_list_{ nullptr };
        helper::ReusableDialog* parameter_dialog_ { nullptr };

        std::map<uint64_t, size_t> running_module_id_to_inner_index_; // maps running module id to index, because running module ids never repeat, but modules can be removed
        std::map<uint64_t, bool> supports_activation_; // maps running module id to whether it supports activation or is always active

        
        Wt::Signal<> onClose_; // close button clicked
        Wt::Signal<> onSave_; // save button clicked
        Wt::Signal<> onLoad_; // load button clicked
        Wt::Signal<> onAddNew_;  // add new module button clicked

        Wt::Signal<uint64_t> onActivate_; // activate button clicked on one of the modules, passes the running module id
        Wt::Signal<uint64_t> onDeactivate_; // deactivate button clicked on one of the modules, passes the running module id
        Wt::Signal<uint64_t> onRemoveModule_; // remove module button clicked on one of the modules, passes the running module id

        Wt::Signal<ModuleSingleParameter, helper::value_t> onParameterChanged_; // module parameter changed, passes module+parameter+list ids and the new value
        Wt::Signal<ModuleSingleParameter> addListEntry_; // add list entry button clicked in parameter, passes module+parameter+list ids (list id is the id of the new entry, i.e. current list size before adding)
        Wt::Signal<ModuleSingleParameter> removeListEntry_; // remove list entry button clicked in parameter, passes module+parameter+list ids

    };

    struct ActivationUi::ModuleSingleParameter
    {
        uint64_t running_module_id_; // identifies the running module
        size_t parameter_id_; // identifies the parameter within the module
        size_t list_id_; // for list parameters, identifies the entry within the list, 0 for non-list parameters
    };
}