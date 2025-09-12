#include "webapp/frontend_app.h"
#include "webapp/ui/helper/reusable_dialog.h"
#include "module_helpers/activation_wrapper/message_types.h"


#include <map>


// TODO update to the correct activation request ID
#define ACTIVATION_REQUEST_ID 0


using namespace aergo::default_modules::frontend_module::webapp;



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
    }

    for (uint64_t i = 0; i < frontend_state_->known_running_modules_.size(); ++i)
    {
        if (frontend_state_->known_running_modules_[i])
        {
            const aergo::module::ModuleInfo* module_info = frontend_state_->known_running_modules_info_[i];
            activation_ui_->addModule(i, module_info->display_name_, module_info->display_description_, false, false, {});
        }
    }

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
                bool can_activate = activation_it != activation_channels_map.end();
                
                std::vector<aergo::module::helpers::activation_wrapper::params::ParameterDescription> empty_params;
                activation_ui_->addModule(i, running_info.module_info_->display_name_, running_info.module_info_->display_description_, can_activate, can_activate, empty_params);


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
                    base_module_->sendRequest(
                        ACTIVATION_REQUEST_ID,
                        aergo::module::ChannelIdentifier { i, activation_it->second },
                        header
                    );
                }
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
        if (response.success_)
        {
            if (response.response_.request_type_ == aergo::module::helpers::activation_wrapper::message_types::ReqType::READ_ACTIVATION_PARAMETERS)
            {
                // TODO read activation data from data_blob_ and set it in the UI
            }
            // TODO handle other response types
        }
        else
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Module failed to produce response for activation request: " + std::to_string(response.running_module_index_));
        }
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

    frontend_state_->setup_done_ = true;

    // TODO load correctly known running modules
    // TODO also load activation state / activated modules
}