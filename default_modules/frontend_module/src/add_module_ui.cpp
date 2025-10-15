#include "webapp/ui/add_module_ui.h"

#include "webapp/ui/helper/topbar.h"
#include "webapp/ui/helper/left_module_list.h"


using namespace aergo::default_modules::frontend_module::webapp::ui;
using namespace aergo::module::helpers::parameter_description;



AddModuleUi::AddModuleUi(const std::vector<const aergo::module::ModuleInfo*>& available_modules)
: available_modules_(available_modules)
{
    setStyleClass("add-module-ui");

    auto top_bar = addWidget(std::make_unique<helper::TopBar>(
        "Add Module",
        std::vector<helper::ButtonDescription> {},
        std::vector<helper::ButtonDescription> {
            {"Close", helper::ButtonStyle::Danger, true}
        }
    ));

    top_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Close
        {
            onClose_.emit();
        }
    });

    auto content_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    content_container->setStyleClass("content");

    auto module_list = content_container->addWidget(std::make_unique<helper::LeftModuleList>(false, false)); // list of modules

    details_view_container_ = content_container->addWidget(std::make_unique<Wt::WStackedWidget>());    // widget showing details of the selected module, contains all detail views
    details_view_container_->setStyleClass("content-right");

    for (size_t i = 0, inner_module_id = 0; i < available_modules.size(); ++i)
    {
        auto module_info = available_modules[i];
        if (module_info->auto_create_) {
            available_module_id_to_inner_index_.push_back(0);
            continue; // there can only be one instance of auto-created modules and it already exists
        }
        
        available_module_id_to_inner_index_.push_back(inner_module_id++);

        module_list->addModule(module_info->display_name_, false);

        std::vector<helper::RightModuleView::ParameterSection> sections;
        bool has_subscribe_section = false, has_request_section = false;

        std::vector<uint32_t> subscribe_consumer_id_to_inner_index;
        std::vector<ParameterDescription> subscribe_params;
        for (uint32_t j = 0; j < module_info->subscribe_consumer_count_; ++j)
        {
            auto& consumer = module_info->subscribe_consumers_[j];

            if (consumer.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                subscribe_consumer_id_to_inner_index.push_back(0); // not displayed
                continue; // not displayed
            }
            subscribe_consumer_id_to_inner_index.push_back(static_cast<uint32_t>(subscribe_params.size()));

            subscribe_params.push_back(ParameterDescription {
                .type_ = ParameterType::ENUM,
                .param_name_ = consumer.display_name_,
                .param_desc_ = consumer.display_description_,
                .as_list_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE),
                .list_size_min_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE) ? static_cast<uint16_t>(consumer.min_) : uint16_t{1},
                .list_size_max_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE) ? static_cast<uint16_t>(consumer.max_) : uint16_t{1},
            });
        }
        subscribe_enum_to_channel_info_.emplace_back(subscribe_params.size()); // we need to specify type for subscribe_params.size() parameters, starting with empty enums
        if (!subscribe_params.empty())
        {
            sections.push_back( { "Subscribe Parameters", std::move(subscribe_params) } );
            has_subscribe_section = true;
        }
        subscribe_consumer_id_to_inner_index_.push_back(std::move(subscribe_consumer_id_to_inner_index));
        
        std::vector<uint32_t> request_consumer_id_to_inner_index;
        std::vector<ParameterDescription> request_params;
        for (uint32_t j = 0; j < module_info->request_consumer_count_; ++j)
        {
            auto& consumer = module_info->request_consumers_[j];

            if (consumer.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                request_consumer_id_to_inner_index.push_back(0); // not displayed
                continue; // not displayed
            }
            request_consumer_id_to_inner_index.push_back(static_cast<uint32_t>(request_params.size()));

            request_params.push_back(ParameterDescription {
                .type_ = ParameterType::ENUM,
                .param_name_ = consumer.display_name_,
                .param_desc_ = consumer.display_description_,
                .as_list_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE),
                .list_size_min_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE) ? static_cast<uint16_t>(consumer.min_) : uint16_t{1},
                .list_size_max_ = (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE) ? static_cast<uint16_t>(consumer.max_) : uint16_t{1},
            });
        }
        request_enum_to_channel_info_.emplace_back(request_params.size()); // we need to specify type for request_params.size() parameters, starting with empty enums
        if (!request_params.empty())
        {
            sections.push_back( { "Request Parameters", std::move(request_params) } );
            has_request_section = true;
        }
        request_consumer_id_to_inner_index_.push_back(std::move(request_consumer_id_to_inner_index));

        auto detail_view = details_view_container_->addWidget(
            std::make_unique<helper::RightModuleView>(
                module_info->display_name_, 
                module_info->display_description_, 
                std::vector<helper::RightModuleView::ButtonDescriptionValid>(),
                std::vector<helper::RightModuleView::ButtonDescriptionValid> {
                    {{"Create", helper::ButtonStyle::Primary, true}, true}
                },
                std::move(sections)
            )
        );

        module_index_to_section_presence_.emplace_back(has_subscribe_section, has_request_section);

        detail_view->onButtonClicked().connect([this, i](size_t index){
            if (index == 0) // Announce creation
            {
                emitCreateModule(i);
            }
        });

        detail_view->onShowDescription().connect([this] (helper::RightModuleView::ParameterId param_id, std::string name, std::string description) {            
            if (!parameter_dialog_)
            {
                showDialog(std::make_unique<helper::ReusableDialog>(
                    name,
                    description,
                    std::vector<helper::ButtonDescription> { {"Close", helper::ButtonStyle::Secondary, true} }
                ));
            }
        });

        detail_views_.push_back(detail_view);
    }


    module_list->setSelected(0);
    details_view_container_->setCurrentIndex(0);
    module_list->moduleSelected().connect([this](size_t index){
        details_view_container_->setCurrentIndex(static_cast<int>(index));
    });
}



void AddModuleUi::emitCreateModule(size_t available_module_index)
{
    AddModuleData creation_data;

    size_t inner_module_index = available_module_id_to_inner_index_[available_module_index];
    auto module_info = available_modules_[available_module_index];

    auto [has_subscribe_section, has_request_section] = module_index_to_section_presence_[inner_module_index];
    helper::RightModuleView* detail_view = detail_views_[inner_module_index];

    size_t subscribe_section_id = 0;
    size_t request_section_id = has_subscribe_section ? 1 : 0;

    auto fill_channels = [detail_view, this](uint32_t consumer_count, const aergo::module::communication_channel::Consumer* consumers, bool has_section, size_t section_id, const std::vector<uint32_t>& consumer_id_to_inner_index, const std::vector<std::vector<ChannelInfo>>& enum_to_channel_info_vector, std::vector<std::vector<ChannelInfo>>& out_channels, bool subscribe_channel)
    {
        for (size_t i = 0; i < consumer_count; ++i)
        {
            auto& consumer = consumers[i];
            if (consumer.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                out_channels.push_back({}); // empty, AUTO_ALL
                continue;
            }

            if (!has_section)
            {
                showDialog(std::make_unique<helper::ReusableDialog>(
                    "Error",
                    "Internal error: subscribe section not found, but subscribe consumer exists.",
                    std::vector<helper::ButtonDescription> { {"Close", helper::ButtonStyle::Secondary, true} }
                ));
                return false; // should not happen
            }

            uint32_t param_inner_index = consumer_id_to_inner_index[i];
            size_t param_count = detail_view->listSize(section_id, param_inner_index);       // we know that the section exists
            if ((consumer.count_ == aergo::module::communication_channel::Consumer::Count::SINGLE && param_count != 1)
            || (consumer.count_ == aergo::module::communication_channel::Consumer::Count::RANGE && (param_count < consumer.min_ || param_count > consumer.max_))
            )
            {
                showDialog(std::make_unique<helper::ReusableDialog>(
                    "Error",
                    "Wrong number of parameters for " + std::string(subscribe_channel ? "subscribe" : "request") + " channel " + std::to_string(i),
                    std::vector<helper::ButtonDescription> { {"Close", helper::ButtonStyle::Secondary, true} }
                ));
                return false; // should not happen
            }

            auto &enum_to_channel_info = enum_to_channel_info_vector[param_inner_index];

            std::vector<ChannelInfo> channels_for_consumer;
            for (size_t p = 0; p < param_count; ++p)
            {
                auto value = detail_view->value(section_id, param_inner_index, p);
                if (!value.has_value() || !std::holds_alternative<int>(value.value()) || std::get<int>(value.value()) < 0 
                || std::get<int>(value.value()) >= static_cast<int>(enum_to_channel_info.size()))
                {
                    showDialog(std::make_unique<helper::ReusableDialog>(
                        "Error",
                        "No value selected for subscribe channel " + std::to_string(i) + ", list id " + std::to_string(p),
                        std::vector<helper::ButtonDescription> { {"Close", helper::ButtonStyle::Secondary, true} }
                    ));
                    return false; // should not happen
                }

                ChannelInfo channel_info = enum_to_channel_info[std::get<int>(value.value())];
                channels_for_consumer.push_back(channel_info);
            }
            out_channels.push_back(std::move(channels_for_consumer));
        }

        return true;
    };

    if (!fill_channels(
        module_info->subscribe_consumer_count_, module_info->subscribe_consumers_, 
        has_subscribe_section, subscribe_section_id, subscribe_consumer_id_to_inner_index_[inner_module_index], 
        subscribe_enum_to_channel_info_[inner_module_index], creation_data.subscribe_channels_, true))
    {
        return; // error
    }

    if (!fill_channels(
        module_info->request_consumer_count_, module_info->request_consumers_, 
        has_request_section, request_section_id, request_consumer_id_to_inner_index_[inner_module_index], 
        request_enum_to_channel_info_[inner_module_index], creation_data.request_channels_, false)) // no request channels supported yet
    {
        return; // error
    }
    
    onCreateModule_.emit(available_module_index, creation_data);
}



void AddModuleUi::showDialog(std::unique_ptr<helper::ReusableDialog> dialog)
{
    if (parameter_dialog_)
    {
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    }

    parameter_dialog_ = addWidget(std::move(dialog));

    parameter_dialog_->onButtonClicked().connect([this](size_t){
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    });

    parameter_dialog_->onBackgroundClicked().connect([this]{
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    });
}



void AddModuleUi::updateRunningModules(
    std::unordered_map<std::string, std::vector<ChannelInfo>>& publish_module_lookup,
    std::unordered_map<std::string, std::vector<ChannelInfo>>& response_module_lookup
)
{
    for (size_t i = 0; i < available_modules_.size(); ++i)
    {
        auto module_info = available_modules_[i];
        if (module_info->auto_create_) {
            continue; // we don't show auto-created modules
        }

        size_t inner_module_index = available_module_id_to_inner_index_[i];

        auto [has_subscribe_section, has_request_section] = module_index_to_section_presence_[inner_module_index];
        auto detail_view = detail_views_[inner_module_index];

        size_t subscribe_section_id = 0;
        size_t request_section_id = has_subscribe_section ? 1 : 0;

        auto update_enum_value = [detail_view, this](
            std::unordered_map<std::string, std::vector<ChannelInfo>>& module_lookup, std::vector<std::vector<ChannelInfo>>& enum_to_channel_info, std::vector<uint32_t>& consumer_id_to_inner_index, 
            uint32_t consumer_count, const aergo::module::communication_channel::Consumer* consumers, size_t section_id, bool is_subscribe_section)
        {
            for (size_t j = 0; j < consumer_count; ++j)
            {
                auto& consumer = consumers[j];
                if (consumer.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
                {
                    continue; // not displayed
                }

                auto param_type = consumer.channel_type_identifier_;
                auto it = module_lookup.find(param_type);

                std::vector<ChannelInfo> empty;
                auto& available_channels = (it != module_lookup.end()) ? it->second : empty;

                uint32_t inner_param_index = consumer_id_to_inner_index[j];
                enum_to_channel_info[inner_param_index] = available_channels;
                
                std::vector<std::string> enum_values;
                for (auto& available_channel : available_channels)
                {
                    auto other_module_info = available_modules_[available_channel.available_module_index_];
                    std::string enum_value = other_module_info->display_name_ + std::string(" (ID: ") + 
                        std::to_string(available_channel.running_module_index_) + ") - ";
                    enum_value += is_subscribe_section ? other_module_info->publish_producers_[available_channel.channel_id_].display_name_
                                                     : other_module_info->response_producers_[available_channel.channel_id_].display_name_;

                    enum_values.emplace_back(std::move(enum_value));
                }

                
                for (size_t k = 0; k < detail_view->listSize(section_id, inner_param_index); ++k)
                {
                    helper::IParamInput* param_widget_raw = detail_view->getRawParameterWidget(section_id, inner_param_index, k);
                    helper::EnumSelect* enum_widget = dynamic_cast<helper::EnumSelect*>(param_widget_raw);
                    if (enum_widget)
                    {
                        enum_widget->setOptions(enum_values);
                    }
                }
            }
        };

        if (has_subscribe_section)
        {
            update_enum_value(publish_module_lookup, subscribe_enum_to_channel_info_[inner_module_index], subscribe_consumer_id_to_inner_index_[inner_module_index],
                module_info->subscribe_consumer_count_, module_info->subscribe_consumers_, subscribe_section_id, true);   
        }

        if (has_request_section)
        {
            update_enum_value(response_module_lookup, request_enum_to_channel_info_[inner_module_index], request_consumer_id_to_inner_index_[inner_module_index],
                module_info->request_consumer_count_, module_info->request_consumers_, request_section_id, false);   
        }
    }
}