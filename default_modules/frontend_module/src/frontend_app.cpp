#include "webapp/frontend_app.h"
#include "webapp/ui/helper/reusable_dialog.h"
#include "module_helpers/activation_wrapper/message_types.h"


#include <map>


// TODO update to the correct activation request ID
#define ACTIVATION_REQUEST_ID 0


using namespace aergo::default_modules::frontend_module::webapp;
using namespace aergo::module;
using namespace aergo::module::helpers::activation_wrapper;



FrontendApp::FrontendApp(const Wt::WEnvironment& env, Wt::WServer* server, FrontendState* frontend_state, aergo::module::BaseModule* base_module)
: Wt::WApplication(env), connected_(true), server_(server), frontend_state_(frontend_state), base_module_(base_module), core_(base_module->getCoreControl()), life_guard_(std::make_shared<int>(0))
{
    enableUpdates(true);                          // enable updates from other threads
    setTitle("Aergo Frontend");                   // set window title
    addMetaHeader("theme-color", "#000000");    // Helps Android color the system UI
    // Inject <link rel="manifest"> to allow "Add to Home screen"
    doJavaScript(R"JS(
        var l=document.createElement('link');
        l.rel='manifest'; l.href='/manifest.json';
        document.head.appendChild(l);
    )JS");
    useStyleSheet("styles.css");
    session_id_ = sessionId();


    std::string name;
    {
        std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
        if (frontend_state_->active_app_ != nullptr)
        {
            FrontendApp* old_app = frontend_state_->active_app_;
            frontend_state_->active_app_->disconnect();
        }

        frontend_state_->active_app_ = this;
        setupState();
    }

    setupUi();
    
    base_module_->log(aergo::module::logging::LogType::INFO, "FrontendApp created: " + session_id_);
}



FrontendApp::~FrontendApp()
{
    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
    if (frontend_state_->active_app_ == this)
    {
        frontend_state_->active_app_ = nullptr;
    }

    base_module_->log(aergo::module::logging::LogType::INFO, "FrontendApp destroyed: " + session_id_);
}



void FrontendApp::disconnect()
{
    connected_ = false;
    base_module_->log(aergo::module::logging::LogType::INFO, "Disconnecting FrontendApp: " + session_id_);

    std::weak_ptr<int> guard = life_guard_;
    server_->post(session_id_, [this, guard] {
        if (guard.expired()) return;          // widget gone; do nothing

        dismissDialog();

        reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, non-dismissible
            "Disconnected", 
            "Another instance has connected and taken over. This instance is now disconnected. Please reload the page to try to connect again.", 
            std::vector<ui::helper::ButtonDescription> {  }
        ));

        update_timer_->stop();

        triggerUpdate();
    });
}



void FrontendApp::setupUi()
{
    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    root()->setStyleClass("app-root");

    main_container_ = root()->addWidget(std::make_unique<Wt::WStackedWidget>());
    main_container_->setStyleClass("main-container");
    std::vector<const aergo::module::ModuleInfo*> available_modules;

    add_module_ui_ = main_container_->addWidget(std::make_unique<ui::AddModuleUi>(frontend_state_->available_modules_));
    activation_ui_ = main_container_->addWidget(std::make_unique<ui::ActivationUi>());

    update_timer_ = root()->addChild(std::make_unique<Wt::WTimer>());
    update_timer_->setInterval(std::chrono::milliseconds(200));
    update_timer_->timeout().connect(this, &FrontendApp::timerUpdate);
    update_timer_->start();

    setupCallbacks();

    // set UI from state
    loadUiFromState();
}



void FrontendApp::loadUiFromState()
{
    main_container_->setCurrentIndex((int)frontend_state_->current_screen_);

    switch (frontend_state_->running_task_)
    {
        case RunningTask::NONE:
            break;
        case RunningTask::CREATE_MODULE:
            createModuleCreatedDialog();
            break;
        case RunningTask::DESTROY_MODULE:
            createModuleDestroyedDialog();
            break;
        case RunningTask::LOAD_CUSTOM_VALUE:
            createLoadCustomValueDialog();
            break;
        case RunningTask::ACTIVATING:
        case RunningTask::DEACTIVATING:
            createActivationDialog();
            updateActivationDialogProgress();
            break;
    }

    for (uint64_t i = 0; i < frontend_state_->known_running_modules_.size(); ++i)
    {
        if (frontend_state_->known_running_modules_[i])
        {
            const aergo::module::ModuleInfo* module_info = frontend_state_->known_running_modules_info_[i];
            if (!module_info->auto_create_)
            {
                activation_ui_->addModule(i, module_info->display_name_, module_info->display_description_, false, false, {});
            }
        }
    }
    // TODO here we only load the modules, but not activation!

    refreshRunningModules();

    // TODO load correctly activation state
}



void FrontendApp::setupCallbacks()
{
    add_module_ui_->onClose().connect([this]() {
        if (!connected_) return;

        std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
        frontend_state_->current_screen_ = webapp::FrontendScreen::SETUP_MODULES;
        main_container_->setCurrentIndex((int)frontend_state_->current_screen_);
    });

    add_module_ui_->onCreateModule().connect([this](size_t available_module_id, ui::AddModuleUi::AddModuleData creation_data) {
        if (!connected_) return;

        handleModuleCreation(available_module_id, std::move(creation_data));
    });

    activation_ui_->onAddNew().connect([this]() {
        if (!connected_) return;

        std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
        frontend_state_->current_screen_ = webapp::FrontendScreen::ADD_MODULE;
        main_container_->setCurrentIndex((int)frontend_state_->current_screen_);
    });

    activation_ui_->onRemoveModule().connect([this](uint64_t running_module_index) {
        if (!connected_) return;

        handleModuleDestructionDependencyCheck(running_module_index);
    });

    activation_ui_->addListEntry().connect([this](ui::ActivationUi::ModuleSingleParameter param) { requestAddRemoveListEntry(param, true); });
    activation_ui_->removeListEntry().connect([this](ui::ActivationUi::ModuleSingleParameter param) { requestAddRemoveListEntry(param, false); });
    activation_ui_->onParameterChanged().connect([this](ui::ActivationUi::ModuleSingleParameter param, ui::helper::value_t new_value) { requestParameterChange(param, new_value); });

    activation_ui_->onActivate().connect([this](uint64_t running_module_index) { requestActivate(running_module_index, true);});
    activation_ui_->onDeactivate().connect([this](uint64_t running_module_index) { requestActivate(running_module_index, false); });
}



void FrontendApp::handleModuleCreation(size_t available_module_id, ui::AddModuleUi::AddModuleData creation_data)
{
    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    if (available_module_id >= frontend_state_->available_modules_.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid available_module_id in handleModuleCreation");
        return;
    }

    if (frontend_state_->running_task_ != RunningTask::NONE)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Another task is already running in handleModuleCreation");
        return;
    }

    if (frontend_state_->creation_data_.get() != nullptr || frontend_state_->async_task_.get() != nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Module creation or other task already in progress in handleModuleCreation");
        return;
    }


    // prepare the data
    frontend_state_->creation_data_ = std::make_unique<CreateStateData>();

    for (const auto& sub : creation_data.subscribe_channels_)
    {
        std::vector<aergo::module::ChannelIdentifier> channel_ids;
        for (const auto& ch : sub)
        {
            channel_ids.push_back(aergo::module::ChannelIdentifier {ch.running_module_index_, ch.channel_id_});
        }
        frontend_state_->creation_data_->subscribe_channels_ptrs_.push_back(
            aergo::module::InputChannelMapInfo::IndividualChannelInfo {
                .channel_identifier_ = channel_ids.data(),
                .channel_identifier_count_ = static_cast<uint32_t>(channel_ids.size())
            }
        );
        frontend_state_->creation_data_->subscribe_channels_.push_back(std::move(channel_ids));
    }
    
    for (const auto& req : creation_data.request_channels_)
    {
        std::vector<aergo::module::ChannelIdentifier> channel_ids;
        for (const auto& ch : req)
        {
            channel_ids.push_back(aergo::module::ChannelIdentifier {ch.running_module_index_, ch.channel_id_});
        }
        frontend_state_->creation_data_->request_channels_ptrs_.push_back(
            aergo::module::InputChannelMapInfo::IndividualChannelInfo {
                .channel_identifier_ = channel_ids.data(),
                .channel_identifier_count_ = static_cast<uint32_t>(channel_ids.size())
            }
        );
        frontend_state_->creation_data_->request_channels_.push_back(std::move(channel_ids));
    }

    aergo::module::InputChannelMapInfo creation_channel_map_info {
        .subscribe_consumer_info_ = frontend_state_->creation_data_->subscribe_channels_ptrs_.data(),
        .subscribe_consumer_info_count_ = static_cast<uint32_t>(frontend_state_->creation_data_->subscribe_channels_ptrs_.size()),
        .request_consumer_info_ = frontend_state_->creation_data_->request_channels_ptrs_.data(),
        .request_consumer_info_count_ = static_cast<uint32_t>(frontend_state_->creation_data_->request_channels_ptrs_.size())
    };


    // start the creation
    aergo::module::ICoreControl* core_ptr = core_;
    frontend_state_->async_task_ = std::make_unique<aergo::module::helpers::activation_wrapper::AsyncTask<bool>>(
        [core_ptr, available_module_id, creation_channel_map_info](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled_flag) -> bool
        {
            return core_ptr->addModule(available_module_id, creation_channel_map_info); // this task can not be cancelled
        }
    );
    frontend_state_->async_task_->start();

    createModuleCreatedDialog();

    frontend_state_->running_task_ = RunningTask::CREATE_MODULE;
}



void FrontendApp::dismissDialog()
{
    if (reusable_dialog_ != nullptr)
    {
        root()->removeChild(reusable_dialog_);
        reusable_dialog_ = nullptr;
    }
}



void FrontendApp::createModuleCreatedDialog()
{
    dismissDialog();
    reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, non-dismissible
        "Creating Module", 
        "The module is being created. Please wait...", 
        std::vector<ui::helper::ButtonDescription> {  }
    )); 
}



void FrontendApp::createModuleDestroyedDialog()
{
    dismissDialog();
    reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, non-dismissible
        "Removing Module", 
        "The module is being removed. Please wait...", 
        std::vector<ui::helper::ButtonDescription> {  }
    ));
}



void FrontendApp::timerUpdate()
{
    if (!connected_) return;

    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    if (frontend_state_->running_task_ == RunningTask::NONE)
    {
        uint64_t current_state_id = core_->getModulesMappingStateId();
        if (current_state_id != frontend_state_->last_modules_mapping_state_id_)
        {
            refreshRunningModules();
        }

        processPendingActivationResponses();

        return;
    }
    else if (frontend_state_->running_task_ == RunningTask::CREATE_MODULE)
    {
        if (frontend_state_->async_task_ == nullptr)
        {
            frontend_state_->creation_data_.reset();
            frontend_state_->running_task_ = RunningTask::NONE;
            dismissDialog();
            base_module_->log(aergo::module::logging::LogType::ERROR, "No async task in CREATE_MODULE state");
        }
        else if (frontend_state_->async_task_->getState() == aergo::module::helpers::activation_wrapper::AsyncTaskState::CANCELLED)
        {
            frontend_state_->creation_data_.reset();
            frontend_state_->running_task_ = RunningTask::NONE;
            dismissDialog();
            frontend_state_->async_task_.reset();
            base_module_->log(aergo::module::logging::LogType::INFO, "Task is not cancellable but was cancelled in CREATE_MODULE state");
        }        
        else if (frontend_state_->async_task_->getState() == aergo::module::helpers::activation_wrapper::AsyncTaskState::COMPLETED)
        {
            bool success = frontend_state_->async_task_->getResult().value_or(false); // value is always there because state is COMPLETED

            frontend_state_->creation_data_.reset();
            frontend_state_->running_task_ = RunningTask::NONE;
            frontend_state_->async_task_.reset();
            dismissDialog();

            if (!success)
            {
                reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, dismissible
                    "Creating Module", 
                    "The module creation has failed.", 
                    std::vector<ui::helper::ButtonDescription> { 
                        ui::helper::ButtonDescription {
                            .text_ = "OK",
                            .style_ = ui::helper::ButtonStyle::Secondary,
                            .enabled_ = true
                        }
                    }
                ));
                reusable_dialog_->onButtonClicked().connect(this, &FrontendApp::dismissDialog);
                reusable_dialog_->onBackgroundClicked().connect(this, &FrontendApp::dismissDialog);
            }
            else
            {
                refreshRunningModules();

                frontend_state_->current_screen_ = webapp::FrontendScreen::SETUP_MODULES;
                main_container_->setCurrentIndex((int)frontend_state_->current_screen_);
            }
        }
    }
    else if (frontend_state_->running_task_ == RunningTask::DESTROY_MODULE)
    {
        if (frontend_state_->async_task_ == nullptr)
        {
            frontend_state_->running_task_ = RunningTask::NONE;
            dismissDialog();
            base_module_->log(aergo::module::logging::LogType::ERROR, "No async task in DESTROY_MODULE state");
        }
        else if (frontend_state_->async_task_->getState() != aergo::module::helpers::activation_wrapper::AsyncTaskState::RUNNING)
        {
            frontend_state_->running_task_ = RunningTask::NONE;
            frontend_state_->async_task_.reset();
            dismissDialog();

            refreshRunningModules();
        }
    }
    else if (frontend_state_->running_task_ == RunningTask::LOAD_CUSTOM_VALUE
    || frontend_state_->running_task_ == RunningTask::ACTIVATING
    || frontend_state_->running_task_ == RunningTask::DEACTIVATING)
    {
        message_types::Request progress_request { .request_type_ = message_types::ReqType::GET_STATUS };
        message::MessageHeader header {
            .data_ = reinterpret_cast<uint8_t*>(&progress_request),
            .data_len_ = sizeof(progress_request),
            .blobs_ = nullptr,
            .blob_count_ = 0
        };
        base_module_->sendRequest(
            ACTIVATION_REQUEST_ID, 
            ChannelIdentifier {
                frontend_state_->current_custom_parameter_.running_module_id_, 
                frontend_state_->known_running_modules_activation_data_[frontend_state_->current_custom_parameter_.running_module_id_].activation_channel_id_
            }, 
            header
        );

        processPendingActivationResponses();
    }
}



void FrontendApp::refreshRunningModules()
{
    if (!connected_) return;

    // first, check for removed modules
    for (uint64_t i = 0; i < frontend_state_->known_running_modules_.size(); ++i)
    {
        if (!core_->getRunningModulesInfo(i).exists_ && frontend_state_->known_running_modules_[i])
        { // if module stopped existing since last check

            activation_ui_->removeModule(i);
            frontend_state_->known_running_modules_[i] = false;

            // remove from channel lookups
            for (uint32_t channel_id = 0; channel_id < frontend_state_->known_running_modules_info_[i]->publish_producer_count_; ++channel_id)
            {
                const auto& channel = frontend_state_->known_running_modules_info_[i]->publish_producers_[channel_id];
                auto it = frontend_state_->running_modules_publish_channel_lookup_.find(channel.channel_type_identifier_);
                if (it != frontend_state_->running_modules_publish_channel_lookup_.end())
                {
                    auto& vec = it->second;
                    vec.erase(std::remove_if(vec.begin(), vec.end(), [i, channel](const ui::AddModuleUi::ChannelInfo& info) {
                        return info.running_module_index_ == i;
                    }), vec.end());
                    if (vec.empty())
                    {
                        frontend_state_->running_modules_publish_channel_lookup_.erase(it);
                    }
                }
            }

            for (uint32_t channel_id = 0; channel_id < frontend_state_->known_running_modules_info_[i]->response_producer_count_; ++channel_id)
            {
                const auto& channel = frontend_state_->known_running_modules_info_[i]->response_producers_[channel_id];
                auto it = frontend_state_->running_modules_response_channel_lookup_.find(channel.channel_type_identifier_);
                if (it != frontend_state_->running_modules_response_channel_lookup_.end())
                {
                    auto& vec = it->second;
                    vec.erase(std::remove_if(vec.begin(), vec.end(), [i, channel](const ui::AddModuleUi::ChannelInfo& info) {
                        return info.running_module_index_ == i;
                    }), vec.end());
                    if (vec.empty())
                    {
                        frontend_state_->running_modules_response_channel_lookup_.erase(it);
                    }
                }
            }
        }
    }

    // then add new modules
    if (frontend_state_->known_running_modules_.size() < core_->getRunningModulesCount())
    {
        aergo::module::message::SharedDataBlob activation_channels = core_->getExistingResponseChannelsByName(aergo::module::helpers::activation_wrapper::message_types::activation_response_producer.channel_type_identifier_);
        std::map<uint64_t, uint32_t> activation_channels_map; // map of producer module index to producer channel id
        if (activation_channels.valid() && activation_channels.size() >= sizeof(uint64_t))
        {
            uint64_t channel_count;
            memcpy(&channel_count, activation_channels.data(), sizeof(uint64_t));
            aergo::module::ChannelIdentifier* channels = reinterpret_cast<aergo::module::ChannelIdentifier*>(activation_channels.data() + sizeof(uint64_t));

            if (activation_channels.size() == sizeof(uint64_t) + channel_count * sizeof(aergo::module::ChannelIdentifier))
            {
                for (uint64_t i = 0; i < channel_count; ++i)
                {
                    activation_channels_map[channels[i].producer_module_id_] = channels[i].producer_channel_id_;
                }
            }
        }

        for (uint64_t i = frontend_state_->known_running_modules_.size(); i < core_->getRunningModulesCount(); ++i)
        {
            auto running_info = core_->getRunningModulesInfo(i);
            if (running_info.exists_)
            {
                auto activation_it = activation_channels_map.find(i);
                bool can_activate = activation_it != activation_channels_map.end() && !running_info.module_info_->auto_create_;
                
                std::vector<aergo::module::helpers::activation_wrapper::params::ParameterDescription> empty_params;
                if (!running_info.module_info_->auto_create_)
                {
                    activation_ui_->addModule(i, running_info.module_info_->display_name_, running_info.module_info_->display_description_, can_activate, can_activate, empty_params);
                }


                // update state
                frontend_state_->known_running_modules_.push_back(true);
                frontend_state_->known_running_modules_info_.push_back(running_info.module_info_);

                size_t available_module_index = 0;
                auto it = std::find(frontend_state_->available_modules_.begin(), frontend_state_->available_modules_.end(), running_info.module_info_);
                if (it != frontend_state_->available_modules_.end())
                {
                    available_module_index = std::distance(frontend_state_->available_modules_.begin(), it);
                }

                // add to channel lookups
                for (uint32_t channel_id = 0; channel_id < running_info.module_info_->publish_producer_count_; ++channel_id)
                {
                    const auto& producer = running_info.module_info_->publish_producers_[channel_id];
                    frontend_state_->running_modules_publish_channel_lookup_[producer.channel_type_identifier_].push_back(
                        ui::AddModuleUi::ChannelInfo {
                            .available_module_index_ = available_module_index,
                            .running_module_index_ = i,
                            .channel_id_ = channel_id
                        }
                    );
                }

                for (uint32_t channel_id = 0; channel_id < running_info.module_info_->response_producer_count_; ++channel_id)
                {
                    const auto& producer = running_info.module_info_->response_producers_[channel_id];
                    frontend_state_->running_modules_response_channel_lookup_[producer.channel_type_identifier_].push_back(
                        ui::AddModuleUi::ChannelInfo {
                            .available_module_index_ = available_module_index,
                            .running_module_index_ = i,
                            .channel_id_ = channel_id
                        }
                    );
                }



                // prompt for activation parameters
                if (can_activate)
                {
                    aergo::module::helpers::activation_wrapper::message_types::Request request {
                        .request_type_ = aergo::module::helpers::activation_wrapper::message_types::ReqType::READ_ACTIVATION_PARAMETERS
                    };
                    
                    aergo::module::message::MessageHeader header
                    {
                        .data_ = reinterpret_cast<uint8_t*>(&request),
                        .data_len_ = sizeof(request),
                        .blobs_ = nullptr,
                        .blob_count_ = 0
                    };

                    // TODO finish activation support
                    // TODO first we design it as if we start with no running modules
                    //     later we can add support for activating already running modules
                    base_module_->sendRequest(
                        ACTIVATION_REQUEST_ID,
                        aergo::module::ChannelIdentifier { i, activation_it->second },
                        header
                    );

                    frontend_state_->known_running_modules_activation_data_.push_back(ActivationData { 
                        .is_activable_ = true, 
                        .waiting_for_parameters_ = true,
                        .waiting_for_parameter_values_ = true,
                        .activation_channel_id_ = activation_it->second
                    });
                }
                else
                {
                    frontend_state_->known_running_modules_activation_data_.push_back(ActivationData { .is_activable_ = false });
                }
            }
            else
            {   // module does not exist, but we need to keep the index
                frontend_state_->known_running_modules_.push_back(false);
                frontend_state_->known_running_modules_info_.push_back(nullptr);
                frontend_state_->known_running_modules_activation_data_.push_back(ActivationData { .is_activable_ = false });
            }
        }
    }

    add_module_ui_->updateRunningModules(frontend_state_->running_modules_publish_channel_lookup_, frontend_state_->running_modules_response_channel_lookup_);

    frontend_state_->last_modules_mapping_state_id_ = core_->getModulesMappingStateId();
}



void FrontendApp::handleModuleDestruction(uint64_t running_module_index)
{
    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    if (running_module_index >= frontend_state_->known_running_modules_.size() || !frontend_state_->known_running_modules_[running_module_index])
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid running_module_index in handleModuleDestruction");
        return;
    }

    if (frontend_state_->running_task_ != RunningTask::NONE)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Another task is already running in handleModuleDestruction");
        return;
    }

    // start the destruction
    aergo::module::ICoreControl* core_ptr = core_;
    frontend_state_->async_task_ = std::make_unique<aergo::module::helpers::activation_wrapper::AsyncTask<bool>>(
        [core_ptr, running_module_index](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled_flag) -> bool
        {
            return core_ptr->removeModuleById(running_module_index, true); // this task can not be cancelled
        }
    );
    frontend_state_->async_task_->start();

    createModuleDestroyedDialog();

    frontend_state_->running_task_ = RunningTask::DESTROY_MODULE;
}



void FrontendApp::handleModuleDestructionDependencyCheck(uint64_t running_module_index)
{
    if (!connected_) return;

    auto dependencies = core_->collectDependencies(running_module_index);
    if (!dependencies.valid() || dependencies.size() < sizeof(uint64_t))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to get dependencies in handleModuleDestruction");
        return;
    }

    uint64_t dependency_count;
    memcpy(&dependency_count, dependencies.data(), sizeof(uint64_t));

    if (dependencies.size() != sizeof(uint64_t) + dependency_count * sizeof(uint64_t))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid dependencies size in handleModuleDestruction");
        return;
    }

    uint64_t* dependency_indices = reinterpret_cast<uint64_t*>(dependencies.data() + sizeof(uint64_t));

    std::vector<uint64_t> dependencies_vect;
    for (uint64_t i = 0; i < dependency_count; ++i)
    {
        dependencies_vect.push_back(dependency_indices[i]);
    }

    if (dependency_count == 0)
    {
        std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
        refreshRunningModules();
    }
    else if (dependency_count == 1)
    {
        handleModuleDestruction(running_module_index);
    }
    else
    {
        std::string dependent_modules;
        for (uint64_t i = 0; i < dependencies_vect.size(); ++i)
        {
            if (dependencies_vect[i] == running_module_index)
                continue;

            dependent_modules += "\t";

            auto running_data = core_->getRunningModulesInfo(dependencies_vect[i]);
            if (running_data.exists_)
            {
                dependent_modules += std::string(running_data.module_info_->display_name_);
            }
            else if (dependencies_vect[i] < frontend_state_->known_running_modules_info_.size())
            {
                dependent_modules += std::string(frontend_state_->known_running_modules_info_[dependencies_vect[i]]->display_name_);
            }
            else
            {
                dependent_modules += "UNKNOWN MODULE";
            }
            dependent_modules += " (ID: " + std::to_string(dependencies_vect[i]) + ")\n";
        }

        dismissDialog();
        reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, dismissible
            "Removing " + std::to_string(dependencies_vect.size()) + " modules...", 
            "The following modules depend on the module you are trying to remove and will be removed too:\n" +
            dependent_modules,
            std::vector<ui::helper::ButtonDescription> { 
                ui::helper::ButtonDescription {
                    .text_ = "Cancel",
                    .style_ = ui::helper::ButtonStyle::Secondary,
                    .enabled_ = true
                },
                ui::helper::ButtonDescription {
                    .text_ = "OK",
                    .style_ = ui::helper::ButtonStyle::Danger,
                    .enabled_ = true
                }
            }
        ));

        reusable_dialog_->onButtonClicked().connect([this, running_module_index](size_t button_index) {
            if (button_index == 0)
            { // Cancel
                dismissDialog();
            }
            else if (button_index == 1)
            { // OK
                dismissDialog();
                handleModuleDestruction(running_module_index);
            }
        });
    }
}



void FrontendApp::processPendingActivationResponses()
{
    if (!connected_) return;

    for (const auto& response : frontend_state_->pending_activation_responses_)
    {
        if (!response.success_)
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Received failed activation response from module: " + std::to_string(response.running_module_index_));
            continue;
        }

        if (response.running_module_index_ >= frontend_state_->known_running_modules_activation_data_.size())
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Received activation response for invalid module index: " + std::to_string(response.running_module_index_));
            continue;
        }

        auto& activation_data = frontend_state_->known_running_modules_activation_data_[response.running_module_index_];
        if (response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::READ_ACTIVATION_PARAMETERS)
        {
            if (!activation_data.is_activable_ || !activation_data.waiting_for_parameters_)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received activation parameters for module that is not activable or not waiting for parameters: " + std::to_string(response.running_module_index_));
                continue;
            }

            if (response.response_.result_ != message_types::Result::SUCCESS)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received failed READ_ACTIVATION_PARAMETERS response from module: " + std::to_string(response.running_module_index_));
                continue;
            }
            
            std::string params_str(reinterpret_cast<const char*>(response.data_blob_.data()), response.data_blob_.size());
            activation_data.activation_parameters_ = std::move(aergo::module::helpers::activation_wrapper::params::ParameterList::fromString(params_str));
            activation_data.waiting_for_parameters_ = false;

            activation_ui_->setParameters(response.running_module_index_, activation_data.activation_parameters_.getParameters());
            
            requestReadCurrentParameters(response.running_module_index_);
        }
        else if (response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::READ_VALUES)
        {
            if (!activation_data.is_activable_ || activation_data.waiting_for_parameters_)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received current parameters for module that is not activable or still waiting for parameters: " + std::to_string(response.running_module_index_));
                continue;
            }

            if (response.response_.result_ != message_types::Result::SUCCESS)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received failed READ_VALUES response from module: " + std::to_string(response.running_module_index_));
                continue;
            }
            
            if (parseParameterValues(response.data_blob_, activation_data))
            {

                activation_data.waiting_for_parameter_values_ = false;
                activation_ui_->setParameterValues(response.running_module_index_, activation_data.parameter_values_);
            }
            else
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to parse current parameters for module: " + std::to_string(response.running_module_index_));
            }
        }
        else if (response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::SET_VALUE
        || response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::LIST_ADD
        || response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::LIST_REMOVE)
        {
            if (!activation_data.is_activable_ || activation_data.waiting_for_parameters_ || activation_data.waiting_for_parameter_values_)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received set parameters response for module that is not activable or still waiting for parameters/values: " + std::to_string(response.running_module_index_));
                continue;
            }

            if (response.response_.result_ == message_types::Result::FAIL)
            {
                requestReadCurrentParameters(response.running_module_index_); // read to confirm
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received failed response from module for changing parameters: " + std::to_string(response.running_module_index_));
                continue;
            }
        }
        else if (response.response_.request_type_ == message_types::ReqType::CANCEL_TASK)
        {
            if (frontend_state_->running_task_ == RunningTask::LOAD_CUSTOM_VALUE)
            {
                dismissDialog();
                if (response.response_.result_ == message_types::Result::FAIL) // task finished before cancel, we need to show that we read the custom value
                {
                    activation_ui_->setValue( // show in UI that we received the custom value
                        frontend_state_->current_custom_parameter_.running_module_id_,
                        0,
                        frontend_state_->current_custom_parameter_.parameter_id_,
                        true,
                        frontend_state_->current_custom_parameter_.list_id_
                    );
                }
                frontend_state_->running_task_ = RunningTask::NONE;
            }
            else if (frontend_state_->running_task_ == RunningTask::ACTIVATING
                  || frontend_state_->running_task_ == RunningTask::DEACTIVATING)
            {} // do nothing, cancel request was processed, we will find out the result after the activation/deactivation finishes/cancels via GET_STATUS requests
            else
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received CANCEL_TASK response from module: " + std::to_string(response.running_module_index_) + ", but no task was running");
            }
        }
        else if (response.response_.request_type_ == message_types::ReqType::GET_STATUS)
        {
            if (frontend_state_->running_task_ == RunningTask::LOAD_CUSTOM_VALUE)
            {
                if (response.response_.result_ == message_types::Result::SUCCESS)
                {
                    activation_ui_->setValue( // show in UI that we received the custom value
                        frontend_state_->current_custom_parameter_.running_module_id_,
                        0,
                        frontend_state_->current_custom_parameter_.parameter_id_,
                        true,
                        frontend_state_->current_custom_parameter_.list_id_
                    );

                    dismissDialog();
                    frontend_state_->running_task_ = RunningTask::NONE;
                }
            }
            else if (frontend_state_->running_task_ == RunningTask::ACTIVATING || frontend_state_->running_task_ == RunningTask::DEACTIVATING)
            {
                if (response.response_.result_ == message_types::Result::SUCCESS)
                {
                    dismissDialog();
                    frontend_state_->running_task_ = RunningTask::NONE;
                }
                else if (response.response_.result_ == message_types::Result::RUNNING)
                {
                    frontend_state_->current_progress_ = response.response_.progress_;
                    updateActivationDialogProgress();
                }
            }
        }
        else if (response.response_.request_type_ == message_types::ReqType::ACTIVATE)
        {
            if (frontend_state_->running_task_ != RunningTask::ACTIVATING)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received ACTIVATE response from module: " + std::to_string(response.running_module_index_) + ", but no activation was in progress");
                continue;
            }

            if (response.response_.result_ == message_types::Result::FAIL)
            {
                dismissDialog();
                frontend_state_->running_task_ = RunningTask::NONE;

                reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, dismissible
                    "Activating Module", 
                    "The module activation has failed. Double check the parameters and try again.",
                    std::vector<ui::helper::ButtonDescription> { 
                        ui::helper::ButtonDescription {
                            .text_ = "OK",
                            .style_ = ui::helper::ButtonStyle::Secondary,
                            .enabled_ = true
                        }
                    }
                ));
                reusable_dialog_->onButtonClicked().connect(this, &FrontendApp::dismissDialog);
                reusable_dialog_->onBackgroundClicked().connect(this, &FrontendApp::dismissDialog);
            }
            else if (response.response_.result_ == message_types::Result::RUNNING)
            {
                frontend_state_->current_progress_ = response.response_.progress_;
                updateActivationDialogProgress();
            }
            
        }
        else if (response.response_.request_type_ == message_types::ReqType::DEACTIVATE)
        {
            if (frontend_state_->running_task_ != RunningTask::DEACTIVATING)
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Received DEACTIVATE response from module: " + std::to_string(response.running_module_index_) + ", but no deactivation was in progress");
                continue;
            }

            if (response.response_.result_ == message_types::Result::FAIL)
            {
                dismissDialog();
                frontend_state_->running_task_ = RunningTask::NONE;

                reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>( // overlay, dismissible
                    "Deactivating Module", 
                    "The module deactivation has failed. Please try again.",
                    std::vector<ui::helper::ButtonDescription> { 
                        ui::helper::ButtonDescription {
                            .text_ = "OK",
                            .style_ = ui::helper::ButtonStyle::Secondary,
                            .enabled_ = true
                        }
                    }
                ));
                reusable_dialog_->onButtonClicked().connect(this, &FrontendApp::dismissDialog);
                reusable_dialog_->onBackgroundClicked().connect(this, &FrontendApp::dismissDialog);
            }
            else if (response.response_.result_ == message_types::Result::RUNNING)
            {
                frontend_state_->current_progress_ = response.response_.progress_;
                updateActivationDialogProgress();
            }
        }

        activation_ui_->setActive(response.running_module_index_, response.response_.activated_);
        // TODO handle other response types
    }

    frontend_state_->pending_activation_responses_.clear();
}



void FrontendApp::setupState()
{
    if (frontend_state_->setup_done_)
    {
        return;
    }

    frontend_state_->current_screen_ = webapp::FrontendScreen::SETUP_MODULES;

    for (uint64_t i = 0; i < base_module_->getCoreControl()->getLoadedModulesCount(); ++i)
    {
        frontend_state_->available_modules_.push_back(base_module_->getCoreControl()->getLoadedModulesInfo(i));
    }

    frontend_state_->allocator_ = base_module_->createDynamicAllocator();

    frontend_state_->setup_done_ = true;
}



void FrontendApp::requestReadCurrentParameters(uint64_t running_module_index)
{
    if (!connected_) return;

    if (running_module_index >= frontend_state_->known_running_modules_activation_data_.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid running_module_index in requestReadCurrentParameters");
        return;
    }
    auto& activation_data = frontend_state_->known_running_modules_activation_data_[running_module_index];
    if (!activation_data.is_activable_ || activation_data.waiting_for_parameters_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Module is not activable or still waiting for parameters in requestReadCurrentParameters: " + std::to_string(running_module_index));
        return;
    }

    aergo::module::helpers::activation_wrapper::message_types::Request request {
        .request_type_ = aergo::module::helpers::activation_wrapper::message_types::ReqType::READ_VALUES
    };
    
    aergo::module::message::MessageHeader header
    {
        .data_ = reinterpret_cast<uint8_t*>(&request),
        .data_len_ = sizeof(request),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    base_module_->sendRequest(
        ACTIVATION_REQUEST_ID,
        aergo::module::ChannelIdentifier { running_module_index, activation_data.activation_channel_id_ },
        header
    );
}



bool FrontendApp::parseParameterValues(const std::vector<uint8_t>& data_blob, ActivationData& activation_data)
{
    const auto& params = activation_data.activation_parameters_.getParameters();

    const uint8_t* data = data_blob.data();
    size_t data_size = data_blob.size();

    auto read_values = [&data, &data_size](void* dest, size_t size) -> bool {
        if (data_size < size) return false;
        memcpy(dest, data, size);
        data += size;
        data_size -= size;
        return true;
    };

    size_t param_count;
    if (!read_values(&param_count, sizeof(param_count)))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read param_count in parseParameterValues");
        return false;
    }

    if (param_count != activation_data.activation_parameters_.getParameters().size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Parameter count mismatch in parseParameterValues");
        return false;
    }

    std::vector<std::vector<ui::helper::value_t>> parameter_values;
    for (size_t i = 0; i < param_count; ++i)
    {
        const auto& param = params[i];

        size_t list_size;
        if (!read_values(&list_size, sizeof(list_size)))
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read value_count in parseParameterValues");
            return false;
        }

        if ((!param.as_list_ && list_size != 1) || (param.as_list_ && (list_size < param.list_size_min_ || list_size > param.list_size_max_)))
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Value count mismatch in parseParameterValues for parameter: " + param.param_name_);
            return false;
        }

        std::vector<ui::helper::value_t> values;
        for (size_t j = 0; j < list_size; ++j)
        {
            size_t param_value_size;
            if (!read_values(&param_value_size, sizeof(param_value_size)))
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read param_value_size in parseParameterValues");
                return false;
            }

            switch (param.type_)
            {
                case aergo::module::helpers::activation_wrapper::params::ParameterType::LONG:
                {
                    if (param_value_size != sizeof(int64_t))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid long parameter value size in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    int64_t long_value;
                    if (!read_values(&long_value, sizeof(long_value)))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read long parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    if ((param.limit_min_ && long_value < param.min_value_long_) || (param.limit_max_ && long_value > param.max_value_long_))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Long parameter value out of range in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    values.push_back(long_value);
                    break;
                }
                case aergo::module::helpers::activation_wrapper::params::ParameterType::DOUBLE:
                {
                    if (param_value_size != sizeof(double))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid double parameter value size in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    double double_value;
                    if (!read_values(&double_value, sizeof(double_value)))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read double parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    if ((param.limit_min_ && double_value < param.min_value_double_) || (param.limit_max_ && double_value > param.max_value_double_))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Double parameter value out of range in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    values.push_back(double_value);
                    break;
                }
                case aergo::module::helpers::activation_wrapper::params::ParameterType::STRING:
                {
                    if (param_value_size > data_size)
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid string parameter value size in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    std::string string_value(reinterpret_cast<const char*>(data), param_value_size);
                    data_size -= param_value_size;
                    data += param_value_size;
                    values.push_back(string_value);
                    break;
                }
                case aergo::module::helpers::activation_wrapper::params::ParameterType::ENUM:
                {
                    if (param_value_size != sizeof(size_t))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid enum parameter value size in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    size_t enum_index;
                    if (!read_values(&enum_index, sizeof(enum_index)))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read enum parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    if (enum_index >= param.enum_values_.size())
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid enum parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    values.push_back(static_cast<int>(enum_index)); // store as int, value is the index of the selected enum value
                    break;
                }
                case aergo::module::helpers::activation_wrapper::params::ParameterType::BOOL:
                case aergo::module::helpers::activation_wrapper::params::ParameterType::CUSTOM:
                {
                    if (param_value_size != 1)
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid custom/bool parameter value size in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    uint8_t bool_value;
                    if (!read_values(&bool_value, sizeof(bool_value)))
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to read custom/bool parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    if (bool_value != 0 && bool_value != 1)
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid custom/bool parameter value in parseParameterValues for parameter: " + param.param_name_);
                        return false;
                    }
                    values.push_back(bool_value == 1); // store as bool, value is 1 if custom/bool value is set, 0 if not
                    break;
                }
            }
        }

        parameter_values.push_back(std::move(values));
    }

    activation_data.parameter_values_ = std::move(parameter_values);
    return true;
}



void FrontendApp::requestAddRemoveListEntry(ui::ActivationUi::ModuleSingleParameter param, bool add)
{
    if (!connected_) return;

    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    if (param.running_module_id_ >= frontend_state_->known_running_modules_.size() || !frontend_state_->known_running_modules_[param.running_module_id_] || 
        !frontend_state_->known_running_modules_activation_data_[param.running_module_id_].is_activable_ || frontend_state_->known_running_modules_activation_data_[param.running_module_id_].waiting_for_parameters_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid running_module_id or module not activable or waiting for parameters in add/remove list entry callback");
        return;
    }

    const auto& activation_data = frontend_state_->known_running_modules_activation_data_[param.running_module_id_];
    const auto& params_list = activation_data.activation_parameters_.getParameters();
    if (param.parameter_id_ >= params_list.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid parameter_id in add/remove list entry callback");
        return;
    }

    message_types::Request add_list_request {
        .request_type_ = add ? message_types::ReqType::LIST_ADD : message_types::ReqType::LIST_REMOVE,
        .parameter_type_ = params_list[param.parameter_id_].type_,
        .param_id_ = param.parameter_id_,
        .list_id_ = param.list_id_
    };

    message::MessageHeader header {
        .data_ = reinterpret_cast<uint8_t*>(&add_list_request),
        .data_len_ = sizeof(add_list_request),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    base_module_->sendRequest(
        ACTIVATION_REQUEST_ID,
        ChannelIdentifier {param.running_module_id_, activation_data.activation_channel_id_},
        header
    );
}



void FrontendApp::requestParameterChange(ui::ActivationUi::ModuleSingleParameter param, const ui::helper::value_t& value)
{
    if (!connected_) return;

    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);
    
    if (param.running_module_id_ >= frontend_state_->known_running_modules_.size() || !frontend_state_->known_running_modules_[param.running_module_id_] || 
        !frontend_state_->known_running_modules_activation_data_[param.running_module_id_].is_activable_ || frontend_state_->known_running_modules_activation_data_[param.running_module_id_].waiting_for_parameters_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid running_module_id or module not activable or waiting for parameters in parameterChange callback");
        return;
    }

    const auto& activation_data = frontend_state_->known_running_modules_activation_data_[param.running_module_id_];
    const auto& params_list = activation_data.activation_parameters_.getParameters();
    if (param.parameter_id_ >= params_list.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid parameter_id in parameterChange callback");
        return;
    }

    auto type = params_list[param.parameter_id_].type_;

    message::SharedDataBlob value_blob;
    switch (type)
    {
        case aergo::module::helpers::activation_wrapper::params::ParameterType::LONG:
        {
            int64_t long_value = std::get<int64_t>(value);
            value_blob = frontend_state_->allocator_->allocate(sizeof(long_value));
            if (!value_blob.valid() || value_blob.size() != sizeof(long_value))
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory for long parameter value in parameterChange callback");
                return;
            }
            memcpy(value_blob.data(), &long_value, sizeof(long_value));
            break;
        }
        case aergo::module::helpers::activation_wrapper::params::ParameterType::DOUBLE:
        {
            double double_value = std::get<double>(value);
            value_blob = frontend_state_->allocator_->allocate(sizeof(double_value));
            if (!value_blob.valid() || value_blob.size() != sizeof(double_value))
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory for double parameter value in parameterChange callback");
                return;
            }
            memcpy(value_blob.data(), &double_value, sizeof(double_value));
            break;
        }
        case aergo::module::helpers::activation_wrapper::params::ParameterType::STRING:
        {
            const std::string& string_value = std::get<std::string>(value);
            value_blob = frontend_state_->allocator_->allocate(string_value.size());
            if (!value_blob.valid() || value_blob.size() != string_value.size())
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory for string parameter value in parameterChange callback");
                return;
            }
            memcpy(value_blob.data(), string_value.data(), string_value.size());
            break;
        }
        case aergo::module::helpers::activation_wrapper::params::ParameterType::ENUM:
        {
            size_t enum_index = static_cast<size_t>(std::get<int>(value));
            value_blob = frontend_state_->allocator_->allocate(sizeof(enum_index));
            if (!value_blob.valid() || value_blob.size() != sizeof(enum_index))
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory for enum parameter value in parameterChange callback");
                return;
            }
            memcpy(value_blob.data(), &enum_index, sizeof(enum_index));
            break;
        }
        case aergo::module::helpers::activation_wrapper::params::ParameterType::BOOL:
        case aergo::module::helpers::activation_wrapper::params::ParameterType::CUSTOM:
        {
            uint8_t bool_value = std::get<bool>(value) ? 1 : 0;
            value_blob = frontend_state_->allocator_->allocate(sizeof(bool_value));
            if (!value_blob.valid() || value_blob.size() != sizeof(bool_value))
            {
                base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory for bool/custom parameter value in parameterChange callback");
                return;
            }
            memcpy(value_blob.data(), &bool_value, sizeof(bool_value));
            break;
        }
    }

    message_types::Request param_change_request {
        .request_type_ = message_types::ReqType::SET_VALUE,
        .parameter_type_ = type,
        .param_id_ = param.parameter_id_,
        .list_id_ = param.list_id_
    };

    message::MessageHeader header {
        .data_ = reinterpret_cast<uint8_t*>(&param_change_request),
        .data_len_ = sizeof(param_change_request),
        .blobs_ = &value_blob,
        .blob_count_ = 1
    };

    ChannelIdentifier channel_id {param.running_module_id_, activation_data.activation_channel_id_};
    base_module_->sendRequest(ACTIVATION_REQUEST_ID, channel_id, header);

    if (type == params::ParameterType::CUSTOM) 
    {
        if (value_blob.data()[0] == 1) // display dialog waiting to finish loading the custom value
        {
            frontend_state_->running_task_ = RunningTask::LOAD_CUSTOM_VALUE;
            frontend_state_->current_custom_value_name_ = params_list[param.parameter_id_].param_name_;
            frontend_state_->current_custom_parameter_ = param;

            createLoadCustomValueDialog();
        }
        else // removing is instant, so we can just update the UI
        {
            activation_ui_->setValue( // show in UI that we deleted the custom value
                param.running_module_id_,
                0,
                param.parameter_id_,
                false,
                param.list_id_
            );
        }
    }
}



void FrontendApp::createLoadCustomValueDialog()
{
    dismissDialog();
    reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>(
        "Loading Custom Value", 
        "Loading custom value for parameter \"" + frontend_state_->current_custom_value_name_ + "\". Please wait...", 
        std::vector<ui::helper::ButtonDescription> { 
            ui::helper::ButtonDescription {
                .text_ = "Cancel",
                .style_ = ui::helper::ButtonStyle::Danger,
                .enabled_ = true
            }
        }
    ));

    reusable_dialog_->onButtonClicked().connect([this](size_t button_index) {
        message_types::Request cancel_request {
            .request_type_ = message_types::ReqType::CANCEL_TASK
        };

        message::MessageHeader header {
            .data_ = reinterpret_cast<uint8_t*>(&cancel_request),
            .data_len_ = sizeof(cancel_request),
            .blobs_ = nullptr,
            .blob_count_ = 0
        };

        base_module_->sendRequest(
            ACTIVATION_REQUEST_ID, 
            {
                .producer_module_id_ = frontend_state_->current_custom_parameter_.running_module_id_,
                .producer_channel_id_ = frontend_state_->known_running_modules_activation_data_[frontend_state_->current_custom_parameter_.running_module_id_].activation_channel_id_
            }, 
            header
        );
    });
}



void FrontendApp::requestActivate(uint64_t running_module_index, bool activate)
{
    if (!connected_) return;

    std::lock_guard<std::mutex> lk(frontend_state_->mutex_);

    if (running_module_index >= frontend_state_->known_running_modules_.size() || !frontend_state_->known_running_modules_[running_module_index])
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Invalid running_module_index in requestActivate");
        return;
    }

    auto& activation_data = frontend_state_->known_running_modules_activation_data_[running_module_index];
    if (!activation_data.is_activable_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Module is not activable in requestActivate: " + std::to_string(running_module_index));
        return;
    }

    if (activation_data.waiting_for_parameter_values_ || activation_data.waiting_for_parameters_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Module is still waiting for parameters/values in requestActivate: " + std::to_string(running_module_index));
        return;
    }

    message_types::Request activation_request {
        .request_type_ = activate ? message_types::ReqType::ACTIVATE : message_types::ReqType::DEACTIVATE
    };

    message::MessageHeader header {
        .data_ = reinterpret_cast<uint8_t*>(&activation_request),
        .data_len_ = sizeof(activation_request),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    frontend_state_->current_custom_parameter_.running_module_id_ = running_module_index;

    base_module_->sendRequest(
        ACTIVATION_REQUEST_ID,
        ChannelIdentifier { running_module_index, activation_data.activation_channel_id_ },
        header
    );

    frontend_state_->running_task_ = activate ? RunningTask::ACTIVATING : RunningTask::DEACTIVATING;
    frontend_state_->current_progress_.progress_type_ = message_types::ProgressType::NONE; // reset progress since we are starting a new task
    createActivationDialog();
}



void FrontendApp::createActivationDialog()
{
    dismissDialog();

    reusable_dialog_ = root()->addWidget(std::make_unique<ui::helper::ReusableDialog>(
        frontend_state_->running_task_ == RunningTask::ACTIVATING ? "Activating Module" : "Deactivating Module", 
        frontend_state_->running_task_ == RunningTask::ACTIVATING ? "Activating module. Please wait..." : "Deactivating module. Please wait...", 
        std::vector<ui::helper::ButtonDescription> { 
            ui::helper::ButtonDescription {
                .text_ = "Cancel",
                .style_ = ui::helper::ButtonStyle::Danger,
                .enabled_ = true
            }
        }
    ));

    reusable_dialog_->onButtonClicked().connect([this](size_t button_index) {
        message_types::Request cancel_request {
            .request_type_ = message_types::ReqType::CANCEL_TASK
        };

        message::MessageHeader header {
            .data_ = reinterpret_cast<uint8_t*>(&cancel_request),
            .data_len_ = sizeof(cancel_request),
            .blobs_ = nullptr,
            .blob_count_ = 0
        };

        base_module_->sendRequest(
            ACTIVATION_REQUEST_ID, 
            {
                .producer_module_id_ = frontend_state_->current_custom_parameter_.running_module_id_,
                .producer_channel_id_ = frontend_state_->known_running_modules_activation_data_[frontend_state_->current_custom_parameter_.running_module_id_].activation_channel_id_
            }, 
            header
        );
    });
}

void FrontendApp::updateActivationDialogProgress()
{
    if (!reusable_dialog_) return;

    std::string progress_text;
    if (frontend_state_->current_progress_.progress_type_ == message_types::ProgressType::NONE)
    {
        progress_text = frontend_state_->running_task_ == RunningTask::ACTIVATING ? "Activating module. Please wait..." : "Deactivating module. Please wait...";
    }
    else if (frontend_state_->current_progress_.progress_type_ == message_types::ProgressType::DOUBLE)
    {
        progress_text = (frontend_state_->running_task_ == RunningTask::ACTIVATING ? "Activating module: " : "Deactivating module: ") + 
            std::to_string(static_cast<int>(frontend_state_->current_progress_.progress_current_value_double_ * 100 + 0.5)) + "% completed. Please wait...";
    }
    else if (frontend_state_->current_progress_.progress_type_ == message_types::ProgressType::INT)
    {
        progress_text = (frontend_state_->running_task_ == RunningTask::ACTIVATING ? "Activating module: " : "Deactivating module: ") + 
            std::to_string(frontend_state_->current_progress_.progress_current_value_int_) + "/" + 
            std::to_string(frontend_state_->current_progress_.progress_max_int_) + ". Please wait...";
    }

    reusable_dialog_->setContent(progress_text);
}