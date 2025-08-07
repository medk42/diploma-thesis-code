#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "utils/logging/console_logger.h"

#include "module_common/module_common.h"

#include <algorithm>

using namespace aergo::core;
using namespace aergo::core::logging;
using namespace aergo::tests::core_1;




TEST_CASE( "Core Test 1", "[core_test_1]" )
{
    ConsoleLogger logger;
    Core core(&logger);
    REQUIRE_NOTHROW(core.initialize("../../../../../../../backend/binaries/tests/test_core_1", "."));

    uint64_t last_state_id = core.getModulesMappingStateId();

    SECTION("Modules loaded correctly")
    {
        REQUIRE(core.getLoadedModulesCount() == 5);
        REQUIRE(core.getCreatedModulesCount() == 1);
        REQUIRE(core.collectDependentModules(0).size() == 1);
        REQUIRE(core.collectDependentModules(1).size() == 0);

        REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 0);
        
        // replace publish by response, no channels should be found in that case
        REQUIRE(core.getExistingResponseChannels("message_3/v1:int").size() == 0);
        REQUIRE(core.getExistingPublishChannels("message_4/v1:int").size() == 0);
        REQUIRE(core.getExistingResponseChannels("message_6/v1:int").size() == 0);
    }

    SECTION("Add non-existing module")
    {
        aergo::module::InputChannelMapInfo channel_map_info
        {
            .subscribe_consumer_info_ = nullptr,
            .subscribe_consumer_info_count_ = 0,
            .request_consumer_info_ = nullptr,
            .request_consumer_info_count_ = 0
        };

        REQUIRE(core.addModule(100, channel_map_info) == false);
    }

    SECTION("Create communication map")
    {
        aergo::core::structures::ModuleData* data_e = nullptr;
        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(1));
        REQUIRE(data_e == nullptr);
        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
        REQUIRE(data_e != nullptr);

        REQUIRE(data_e->mapping_subscribe_.size() == 1);
        REQUIRE(data_e->mapping_request_.size() == 0);
        REQUIRE(data_e->mapping_publish_.size() == 1);
        REQUIRE(data_e->mapping_response_.size() == 1);
        
        REQUIRE(data_e->mapping_subscribe_[0].size() == 0);
        REQUIRE(data_e->mapping_publish_[0].size() == 0);
        REQUIRE(data_e->mapping_response_[0].size() == 0);

        aergo::module::InputChannelMapInfo channel_map_info_a
        {
            .subscribe_consumer_info_ = nullptr,
            .subscribe_consumer_info_count_ = 0,
            .request_consumer_info_ = nullptr,
            .request_consumer_info_count_ = 0
        };
        REQUIRE(core.addModule(0, channel_map_info_a) == true);

        uint64_t new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id > last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.getCreatedModulesCount() == 2);
        REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
        REQUIRE(core.collectDependentModules(1).size() == 1);


        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
        REQUIRE(data_e != nullptr);

        aergo::core::structures::ModuleData* data_a = nullptr;
        REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
        REQUIRE(data_a != nullptr);

        REQUIRE(data_e->mapping_subscribe_.size() == 1);
        REQUIRE(data_e->mapping_request_.size() == 0);
        REQUIRE(data_e->mapping_publish_.size() == 1);
        REQUIRE(data_e->mapping_response_.size() == 1);
        
        REQUIRE(data_e->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_e->mapping_publish_[0].size() == 0);
        REQUIRE(data_e->mapping_response_[0].size() == 0);

        REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});

        
        REQUIRE(data_a->mapping_subscribe_.size() == 0);
        REQUIRE(data_a->mapping_request_.size() == 0);
        REQUIRE(data_a->mapping_publish_.size() == 2);
        REQUIRE(data_a->mapping_response_.size() == 0);
        
        REQUIRE(data_a->mapping_publish_[0].size() == 1);
        REQUIRE(data_a->mapping_publish_[1].size() == 0);

        REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});



        aergo::module::ChannelIdentifier channel_id_b {
            .producer_module_id_ = 1,
            .producer_channel_id_ = 1
        };

        aergo::module::InputChannelMapInfo::IndividualChannelInfo single_channel_info_b
        {
            .channel_identifier_ = &channel_id_b,
            .channel_identifier_count_ = 1
        };

        aergo::module::InputChannelMapInfo channel_map_info_b
        {
            .subscribe_consumer_info_ = &single_channel_info_b,
            .subscribe_consumer_info_count_ = 1,
            .request_consumer_info_ = nullptr,
            .request_consumer_info_count_ = 0
        };

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.addModule(1, channel_map_info_b) == true);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id > last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.getCreatedModulesCount() == 3);
        REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 2);
        REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
        REQUIRE(core.collectDependentModules(0).size() == 1);
        REQUIRE(core.collectDependentModules(1).size() == 2);
        REQUIRE(core.collectDependentModules(2).size() == 1);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.addModule(0, channel_map_info_b) == false);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        channel_id_b.producer_module_id_ = 1;
        channel_id_b.producer_channel_id_ = 0;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        channel_id_b.producer_module_id_ = 0;
        channel_id_b.producer_channel_id_ = 1;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        channel_id_b.producer_module_id_ = 0;
        channel_id_b.producer_channel_id_ = 100;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        channel_id_b.producer_module_id_ = 100;
        channel_id_b.producer_channel_id_ = 0;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        single_channel_info_b.channel_identifier_ = nullptr;
        single_channel_info_b.channel_identifier_count_ = 1;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        channel_map_info_b.subscribe_consumer_info_ = nullptr;
        channel_map_info_b.subscribe_consumer_info_count_ = 1;
        REQUIRE(core.addModule(1, channel_map_info_b) == false);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;
        


        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
        REQUIRE(data_e != nullptr);

        REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
        REQUIRE(data_a != nullptr);

        aergo::core::structures::ModuleData* data_b = nullptr;
        REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
        REQUIRE(data_b != nullptr);

        REQUIRE(data_e->mapping_subscribe_.size() == 1);
        REQUIRE(data_e->mapping_request_.size() == 0);
        REQUIRE(data_e->mapping_publish_.size() == 1);
        REQUIRE(data_e->mapping_response_.size() == 1);
        
        REQUIRE(data_e->mapping_subscribe_[0].size() == 2);
        REQUIRE(data_e->mapping_publish_[0].size() == 0);
        REQUIRE(data_e->mapping_response_[0].size() == 0);

        REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
        REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});

        
        REQUIRE(data_a->mapping_subscribe_.size() == 0);
        REQUIRE(data_a->mapping_request_.size() == 0);
        REQUIRE(data_a->mapping_publish_.size() == 2);
        REQUIRE(data_a->mapping_response_.size() == 0);
        
        REQUIRE(data_a->mapping_publish_[0].size() == 1);
        REQUIRE(data_a->mapping_publish_[1].size() == 1);

        REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});

        
        REQUIRE(data_b->mapping_subscribe_.size() == 1);
        REQUIRE(data_b->mapping_request_.size() == 0);
        REQUIRE(data_b->mapping_publish_.size() == 1);
        REQUIRE(data_b->mapping_response_.size() == 1);
        
        REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_b->mapping_publish_[0].size() == 1);
        REQUIRE(data_b->mapping_response_[0].size() == 0);

        REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});



        aergo::module::ChannelIdentifier channel_sub_id_c = {
            .producer_module_id_ = 1,
            .producer_channel_id_ = 1
        };

        aergo::module::ChannelIdentifier channel_req_id_c = {
            .producer_module_id_ = 0,
            .producer_channel_id_ = 0
        };

        aergo::module::InputChannelMapInfo::IndividualChannelInfo single_channel_sub_info_c
        {
            .channel_identifier_ = &channel_sub_id_c,
            .channel_identifier_count_ = 1
        };

        aergo::module::InputChannelMapInfo::IndividualChannelInfo single_channel_req_info_c
        {
            .channel_identifier_ = &channel_req_id_c,
            .channel_identifier_count_ = 1
        };

        aergo::module::InputChannelMapInfo channel_map_info_c
        {
            .subscribe_consumer_info_ = &single_channel_sub_info_c,
            .subscribe_consumer_info_count_ = 1,
            .request_consumer_info_ = &single_channel_req_info_c,
            .request_consumer_info_count_ = 1
        };

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.addModule(2, channel_map_info_c) == true);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id > last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.getCreatedModulesCount() == 4);
        REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 3);
        REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
        REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
        REQUIRE(core.collectDependentModules(0).size() == 2);
        REQUIRE(core.collectDependentModules(1).size() == 3);
        REQUIRE(core.collectDependentModules(2).size() == 1);
        REQUIRE(core.collectDependentModules(3).size() == 1);



        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
        REQUIRE(data_e != nullptr);
        
        REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
        REQUIRE(data_a != nullptr);
        
        REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
        REQUIRE(data_b != nullptr);

        aergo::core::structures::ModuleData* data_c = nullptr;
        REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
        REQUIRE(data_c != nullptr);

        REQUIRE(data_e->mapping_subscribe_.size() == 1);
        REQUIRE(data_e->mapping_request_.size() == 0);
        REQUIRE(data_e->mapping_publish_.size() == 1);
        REQUIRE(data_e->mapping_response_.size() == 1);
        
        REQUIRE(data_e->mapping_subscribe_[0].size() == 3);
        REQUIRE(data_e->mapping_publish_[0].size() == 0);
        REQUIRE(data_e->mapping_response_[0].size() == 1);

        REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
        REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
        REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
        REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});

        
        REQUIRE(data_a->mapping_subscribe_.size() == 0);
        REQUIRE(data_a->mapping_request_.size() == 0);
        REQUIRE(data_a->mapping_publish_.size() == 2);
        REQUIRE(data_a->mapping_response_.size() == 0);
        
        REQUIRE(data_a->mapping_publish_[0].size() == 1);
        REQUIRE(data_a->mapping_publish_[1].size() == 2);

        REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
        REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

        
        REQUIRE(data_b->mapping_subscribe_.size() == 1);
        REQUIRE(data_b->mapping_request_.size() == 0);
        REQUIRE(data_b->mapping_publish_.size() == 1);
        REQUIRE(data_b->mapping_response_.size() == 1);
        
        REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_b->mapping_publish_[0].size() == 1);
        REQUIRE(data_b->mapping_response_[0].size() == 0);

        REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});

        
        REQUIRE(data_c->mapping_subscribe_.size() == 1);
        REQUIRE(data_c->mapping_request_.size() == 1);
        REQUIRE(data_c->mapping_publish_.size() == 1);
        REQUIRE(data_c->mapping_response_.size() == 1);
        
        REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_c->mapping_request_[0].size() == 1);
        REQUIRE(data_c->mapping_publish_[0].size() == 1);
        REQUIRE(data_c->mapping_response_[0].size() == 0);

        REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});



        aergo::module::ChannelIdentifier channel_sub_id_d = {
            .producer_module_id_ = 0,
            .producer_channel_id_ = 0
        };

        aergo::module::ChannelIdentifier channel_req_ids_d[2] = {
            {
                .producer_module_id_ = 2,
                .producer_channel_id_ = 0
            },
            {
                .producer_module_id_ = 3,
                .producer_channel_id_ = 0
            }
        };

        aergo::module::InputChannelMapInfo::IndividualChannelInfo single_channel_sub_info_d
        {
            .channel_identifier_ = &channel_sub_id_d,
            .channel_identifier_count_ = 1
        };

        aergo::module::InputChannelMapInfo::IndividualChannelInfo single_channel_req_info_d
        {
            .channel_identifier_ = channel_req_ids_d,
            .channel_identifier_count_ = 2
        };

        aergo::module::InputChannelMapInfo channel_map_info_d
        {
            .subscribe_consumer_info_ = &single_channel_sub_info_d,
            .subscribe_consumer_info_count_ = 1,
            .request_consumer_info_ = &single_channel_req_info_d,
            .request_consumer_info_count_ = 1
        };

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.addModule(3, channel_map_info_d) == true);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id > last_state_id);
        last_state_id = new_state_id;

        REQUIRE(core.getCreatedModulesCount() == 5);
        REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
        REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
        REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
        REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
        REQUIRE(core.collectDependentModules(0).size() == 3);
        REQUIRE(core.collectDependentModules(1).size() == 4);
        REQUIRE(core.collectDependentModules(2).size() == 2);
        REQUIRE(core.collectDependentModules(3).size() == 2);
        REQUIRE(core.collectDependentModules(4).size() == 1);
        REQUIRE(core.collectDependentModules(5).size() == 0);
        REQUIRE(core.collectDependentModules(6).size() == 0);



        REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
        REQUIRE(data_e != nullptr);

        REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
        REQUIRE(data_a != nullptr);
        
        REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
        REQUIRE(data_b != nullptr);
        
        REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
        REQUIRE(data_c != nullptr);
        
        aergo::core::structures::ModuleData* data_d = nullptr;
        REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
        REQUIRE(data_d != nullptr);

        REQUIRE(data_e->mapping_subscribe_.size() == 1);
        REQUIRE(data_e->mapping_request_.size() == 0);
        REQUIRE(data_e->mapping_publish_.size() == 1);
        REQUIRE(data_e->mapping_response_.size() == 1);
        
        REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
        REQUIRE(data_e->mapping_publish_[0].size() == 1);
        REQUIRE(data_e->mapping_response_[0].size() == 1);

        REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
        REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
        REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
        REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
        REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
        REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

        
        REQUIRE(data_a->mapping_subscribe_.size() == 0);
        REQUIRE(data_a->mapping_request_.size() == 0);
        REQUIRE(data_a->mapping_publish_.size() == 2);
        REQUIRE(data_a->mapping_response_.size() == 0);
        
        REQUIRE(data_a->mapping_publish_[0].size() == 1);
        REQUIRE(data_a->mapping_publish_[1].size() == 2);

        REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
        REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

        
        REQUIRE(data_b->mapping_subscribe_.size() == 1);
        REQUIRE(data_b->mapping_request_.size() == 0);
        REQUIRE(data_b->mapping_publish_.size() == 1);
        REQUIRE(data_b->mapping_response_.size() == 1);
        
        REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_b->mapping_publish_[0].size() == 1);
        REQUIRE(data_b->mapping_response_[0].size() == 1);

        REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

        
        REQUIRE(data_c->mapping_subscribe_.size() == 1);
        REQUIRE(data_c->mapping_request_.size() == 1);
        REQUIRE(data_c->mapping_publish_.size() == 1);
        REQUIRE(data_c->mapping_response_.size() == 1);
        
        REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_c->mapping_request_[0].size() == 1);
        REQUIRE(data_c->mapping_publish_[0].size() == 1);
        REQUIRE(data_c->mapping_response_[0].size() == 1);

        REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

        
        REQUIRE(data_d->mapping_subscribe_.size() == 1);
        REQUIRE(data_d->mapping_request_.size() == 1);
        REQUIRE(data_d->mapping_publish_.size() == 2);
        REQUIRE(data_d->mapping_response_.size() == 0);
        
        REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
        REQUIRE(data_d->mapping_request_[0].size() == 2);
        REQUIRE(data_d->mapping_publish_[0].size() == 0);
        REQUIRE(data_d->mapping_publish_[1].size() == 1);

        REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
        REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
        REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});



        REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
        REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
        REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
        REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
        REQUIRE(core.getCreatedModulesInfo(4) != nullptr);
        REQUIRE(core.getCreatedModulesInfo(5) == nullptr);

        new_state_id = core.getModulesMappingStateId();
        REQUIRE(new_state_id == last_state_id);
        last_state_id = new_state_id;

        SECTION("Remove non-existing")
        {
            REQUIRE(core.removeModule(5, false) == Core::RemoveResult::DOES_NOT_EXIST);
            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.removeModule(5, true) == Core::RemoveResult::DOES_NOT_EXIST);
            
            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);

            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
            REQUIRE(data_e->mapping_publish_[0].size() == 1);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 1);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 1);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_d->mapping_subscribe_.size() == 1);
            REQUIRE(data_d->mapping_request_.size() == 1);
            REQUIRE(data_d->mapping_publish_.size() == 2);
            REQUIRE(data_d->mapping_response_.size() == 0);
            
            REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_d->mapping_request_[0].size() == 2);
            REQUIRE(data_d->mapping_publish_[0].size() == 0);
            REQUIRE(data_d->mapping_publish_[1].size() == 1);

            REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});
        }

        SECTION("Remove module E")
        {
            REQUIRE(core.removeModule(0, false) == Core::RemoveResult::HAS_DEPENDENCIES);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);

            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
            REQUIRE(data_e->mapping_publish_[0].size() == 1);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 1);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 1);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_d->mapping_subscribe_.size() == 1);
            REQUIRE(data_d->mapping_request_.size() == 1);
            REQUIRE(data_d->mapping_publish_.size() == 2);
            REQUIRE(data_d->mapping_response_.size() == 0);
            
            REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_d->mapping_request_[0].size() == 2);
            REQUIRE(data_d->mapping_publish_[0].size() == 0);
            REQUIRE(data_d->mapping_publish_[1].size() == 1);

            REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});




            REQUIRE(core.removeModule(0, true) == Core::RemoveResult::SUCCESS);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id > last_state_id);
            last_state_id = new_state_id;
            
            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 0);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 2);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e == nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c == nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d == nullptr);

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 0);
            REQUIRE(data_a->mapping_publish_[1].size() == 1);

            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 0);
            REQUIRE(data_b->mapping_response_[0].size() == 0);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
        }

        SECTION("Remove module D")
        {
            REQUIRE(core.removeModule(4, false) == Core::RemoveResult::SUCCESS);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id > last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);

            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 3);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d == nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 3);
            REQUIRE(data_e->mapping_publish_[0].size() == 0);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 0);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 0);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        }

        SECTION("Remove module C")
        {
            REQUIRE(core.removeModule(3, false) == Core::RemoveResult::HAS_DEPENDENCIES);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);

            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
            
            REQUIRE(core.getLoadedModulesCount() == 5);

            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
            REQUIRE(data_e->mapping_publish_[0].size() == 1);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 1);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 1);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_d->mapping_subscribe_.size() == 1);
            REQUIRE(data_d->mapping_request_.size() == 1);
            REQUIRE(data_d->mapping_publish_.size() == 2);
            REQUIRE(data_d->mapping_response_.size() == 0);
            
            REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_d->mapping_request_[0].size() == 2);
            REQUIRE(data_d->mapping_publish_[0].size() == 0);
            REQUIRE(data_d->mapping_publish_[1].size() == 1);

            REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});


            REQUIRE(core.removeModule(3, true) == Core::RemoveResult::SUCCESS);

            
            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id > last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 2);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c == nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d == nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 2);
            REQUIRE(data_e->mapping_publish_[0].size() == 0);
            REQUIRE(data_e->mapping_response_[0].size() == 0);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 1);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 0);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
        }

        SECTION("Remove module B")
        {
            REQUIRE(core.removeModule(2, false) == Core::RemoveResult::HAS_DEPENDENCIES);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
            
            REQUIRE(core.getLoadedModulesCount() == 5);

            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
            REQUIRE(data_e->mapping_publish_[0].size() == 1);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 1);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 1);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_d->mapping_subscribe_.size() == 1);
            REQUIRE(data_d->mapping_request_.size() == 1);
            REQUIRE(data_d->mapping_publish_.size() == 2);
            REQUIRE(data_d->mapping_response_.size() == 0);
            
            REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_d->mapping_request_[0].size() == 2);
            REQUIRE(data_d->mapping_publish_[0].size() == 0);
            REQUIRE(data_d->mapping_publish_[1].size() == 1);

            REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});


            REQUIRE(core.removeModule(2, true) == Core::RemoveResult::SUCCESS);
            
            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id > last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 2);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b == nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d == nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 2);
            REQUIRE(data_e->mapping_publish_[0].size() == 0);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 1);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 0);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});

            

            SECTION("Re-add module B and D")
            {
                channel_id_b = {1, 1};
                single_channel_info_b = {&channel_id_b, 1};
                channel_map_info_b = {&single_channel_info_b, 1, nullptr, 0};

                new_state_id = core.getModulesMappingStateId();
                REQUIRE(new_state_id == last_state_id);
                last_state_id = new_state_id;
                
                REQUIRE(core.addModule(1, channel_map_info_b) == true);

                new_state_id = core.getModulesMappingStateId();
                REQUIRE(new_state_id > last_state_id);
                last_state_id = new_state_id;

                REQUIRE(core.getCreatedModulesCount() == 6);

                REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(2) == nullptr);
                REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
                REQUIRE(core.getCreatedModulesInfo(5) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(6) == nullptr);
                REQUIRE(core.getCreatedModulesInfo(7) == nullptr);

                REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
                REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
                REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
                REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
                REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
                REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 3);
                REQUIRE(core.collectDependentModules(0).size() == 2);
                REQUIRE(core.collectDependentModules(1).size() == 3);
                REQUIRE(core.collectDependentModules(2).size() == 0);
                REQUIRE(core.collectDependentModules(3).size() == 1);
                REQUIRE(core.collectDependentModules(4).size() == 0);
                REQUIRE(core.collectDependentModules(5).size() == 1);
                REQUIRE(core.collectDependentModules(6).size() == 0);


                REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
                REQUIRE(data_e != nullptr);

                REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
                REQUIRE(data_a != nullptr);
                
                REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(5));
                REQUIRE(data_b != nullptr);
                
                REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
                REQUIRE(data_c != nullptr);

                REQUIRE(data_e->mapping_subscribe_.size() == 1);
                REQUIRE(data_e->mapping_request_.size() == 0);
                REQUIRE(data_e->mapping_publish_.size() == 1);
                REQUIRE(data_e->mapping_response_.size() == 1);
                
                REQUIRE(data_e->mapping_subscribe_[0].size() == 3);
                REQUIRE(data_e->mapping_publish_[0].size() == 0);
                REQUIRE(data_e->mapping_response_[0].size() == 1);

                REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
                REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{5, 0});
                REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});

                
                REQUIRE(data_a->mapping_subscribe_.size() == 0);
                REQUIRE(data_a->mapping_request_.size() == 0);
                REQUIRE(data_a->mapping_publish_.size() == 2);
                REQUIRE(data_a->mapping_response_.size() == 0);
                
                REQUIRE(data_a->mapping_publish_[0].size() == 1);
                REQUIRE(data_a->mapping_publish_[1].size() == 2);

                REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{5, 0});

                
                REQUIRE(data_b->mapping_subscribe_.size() == 1);
                REQUIRE(data_b->mapping_request_.size() == 0);
                REQUIRE(data_b->mapping_publish_.size() == 1);
                REQUIRE(data_b->mapping_response_.size() == 1);
                
                REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
                REQUIRE(data_b->mapping_publish_[0].size() == 1);
                REQUIRE(data_b->mapping_response_[0].size() == 0);

                REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
                REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});

                
                REQUIRE(data_c->mapping_subscribe_.size() == 1);
                REQUIRE(data_c->mapping_request_.size() == 1);
                REQUIRE(data_c->mapping_publish_.size() == 1);
                REQUIRE(data_c->mapping_response_.size() == 1);
                
                REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
                REQUIRE(data_c->mapping_request_[0].size() == 1);
                REQUIRE(data_c->mapping_publish_[0].size() == 1);
                REQUIRE(data_c->mapping_response_[0].size() == 0);

                REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
                REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});

        

                channel_sub_id_d = {0, 0};
                channel_req_ids_d[0] = {3, 0};
                channel_req_ids_d[1] = {5, 0};
                single_channel_sub_info_d = {&channel_sub_id_d, 1};
                single_channel_req_info_d = {channel_req_ids_d, 2};
                channel_map_info_d = {&single_channel_sub_info_d, 1, &single_channel_req_info_d, 1};

                REQUIRE(core.addModule(3, channel_map_info_d) == true);

                new_state_id = core.getModulesMappingStateId();
                REQUIRE(new_state_id > last_state_id);
                last_state_id = new_state_id;

                REQUIRE(core.getCreatedModulesCount() == 7);

                REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(2) == nullptr);
                REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
                REQUIRE(core.getCreatedModulesInfo(5) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(6) != nullptr);
                REQUIRE(core.getCreatedModulesInfo(7) == nullptr);

                REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
                REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
                REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
                REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
                REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
                REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
                REQUIRE(core.collectDependentModules(0).size() == 3);
                REQUIRE(core.collectDependentModules(1).size() == 4);
                REQUIRE(core.collectDependentModules(2).size() == 0);
                REQUIRE(core.collectDependentModules(3).size() == 2);
                REQUIRE(core.collectDependentModules(4).size() == 0);
                REQUIRE(core.collectDependentModules(5).size() == 2);
                REQUIRE(core.collectDependentModules(6).size() == 1);
                REQUIRE(core.collectDependentModules(7).size() == 0);

                REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
                REQUIRE(data_e != nullptr);

                REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
                REQUIRE(data_a != nullptr);
                
                REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(5));
                REQUIRE(data_b != nullptr);
                
                REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
                REQUIRE(data_c != nullptr);
                
                REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(6));
                REQUIRE(data_d != nullptr);

                REQUIRE(data_e->mapping_subscribe_.size() == 1);
                REQUIRE(data_e->mapping_request_.size() == 0);
                REQUIRE(data_e->mapping_publish_.size() == 1);
                REQUIRE(data_e->mapping_response_.size() == 1);
                
                REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
                REQUIRE(data_e->mapping_publish_[0].size() == 1);
                REQUIRE(data_e->mapping_response_[0].size() == 1);

                REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
                REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{5, 0});
                REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{6, 1});
                REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{6, 0});

                
                REQUIRE(data_a->mapping_subscribe_.size() == 0);
                REQUIRE(data_a->mapping_request_.size() == 0);
                REQUIRE(data_a->mapping_publish_.size() == 2);
                REQUIRE(data_a->mapping_response_.size() == 0);
                
                REQUIRE(data_a->mapping_publish_[0].size() == 1);
                REQUIRE(data_a->mapping_publish_[1].size() == 2);

                REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{5, 0});

                
                REQUIRE(data_b->mapping_subscribe_.size() == 1);
                REQUIRE(data_b->mapping_request_.size() == 0);
                REQUIRE(data_b->mapping_publish_.size() == 1);
                REQUIRE(data_b->mapping_response_.size() == 1);
                
                REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
                REQUIRE(data_b->mapping_publish_[0].size() == 1);
                REQUIRE(data_b->mapping_response_[0].size() == 1);

                REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
                REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{6, 0});

                
                REQUIRE(data_c->mapping_subscribe_.size() == 1);
                REQUIRE(data_c->mapping_request_.size() == 1);
                REQUIRE(data_c->mapping_publish_.size() == 1);
                REQUIRE(data_c->mapping_response_.size() == 1);
                
                REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
                REQUIRE(data_c->mapping_request_[0].size() == 1);
                REQUIRE(data_c->mapping_publish_[0].size() == 1);
                REQUIRE(data_c->mapping_response_[0].size() == 1);

                REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
                REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{6, 0});

                
                REQUIRE(data_d->mapping_subscribe_.size() == 1);
                REQUIRE(data_d->mapping_request_.size() == 1);
                REQUIRE(data_d->mapping_publish_.size() == 2);
                REQUIRE(data_d->mapping_response_.size() == 0);
                
                REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
                REQUIRE(data_d->mapping_request_[0].size() == 2);
                REQUIRE(data_d->mapping_publish_[0].size() == 0);
                REQUIRE(data_d->mapping_publish_[1].size() == 1);

                REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
                REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{3, 0});
                REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{5, 0});
                REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});
            }
        }

        

        SECTION("Remove module A")
        {
            REQUIRE(core.removeModule(1, false) == Core::RemoveResult::HAS_DEPENDENCIES);

            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id == last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 2);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 4);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 4);
            REQUIRE(data_e->mapping_publish_[0].size() == 1);
            REQUIRE(data_e->mapping_response_[0].size() == 1);

            REQUIRE(data_e->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 0});
            REQUIRE(data_e->mapping_subscribe_[0][1] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_e->mapping_subscribe_[0][2] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_subscribe_[0][3] == aergo::module::ChannelIdentifier{4, 1});
            REQUIRE(data_e->mapping_response_[0][0] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_e->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_a->mapping_subscribe_.size() == 0);
            REQUIRE(data_a->mapping_request_.size() == 0);
            REQUIRE(data_a->mapping_publish_.size() == 2);
            REQUIRE(data_a->mapping_response_.size() == 0);
            
            REQUIRE(data_a->mapping_publish_[0].size() == 1);
            REQUIRE(data_a->mapping_publish_[1].size() == 2);

            REQUIRE(data_a->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_a->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_a->mapping_publish_[1][1] == aergo::module::ChannelIdentifier{3, 0});

            
            REQUIRE(data_b->mapping_subscribe_.size() == 1);
            REQUIRE(data_b->mapping_request_.size() == 0);
            REQUIRE(data_b->mapping_publish_.size() == 1);
            REQUIRE(data_b->mapping_response_.size() == 1);
            
            REQUIRE(data_b->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_b->mapping_publish_[0].size() == 1);
            REQUIRE(data_b->mapping_response_[0].size() == 1);

            REQUIRE(data_b->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_b->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_b->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_c->mapping_subscribe_.size() == 1);
            REQUIRE(data_c->mapping_request_.size() == 1);
            REQUIRE(data_c->mapping_publish_.size() == 1);
            REQUIRE(data_c->mapping_response_.size() == 1);
            
            REQUIRE(data_c->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_c->mapping_request_[0].size() == 1);
            REQUIRE(data_c->mapping_publish_[0].size() == 1);
            REQUIRE(data_c->mapping_response_[0].size() == 1);

            REQUIRE(data_c->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{1, 1});
            REQUIRE(data_c->mapping_request_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_publish_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_c->mapping_response_[0][0] == aergo::module::ChannelIdentifier{4, 0});

            
            REQUIRE(data_d->mapping_subscribe_.size() == 1);
            REQUIRE(data_d->mapping_request_.size() == 1);
            REQUIRE(data_d->mapping_publish_.size() == 2);
            REQUIRE(data_d->mapping_response_.size() == 0);
            
            REQUIRE(data_d->mapping_subscribe_[0].size() == 1);
            REQUIRE(data_d->mapping_request_[0].size() == 2);
            REQUIRE(data_d->mapping_publish_[0].size() == 0);
            REQUIRE(data_d->mapping_publish_[1].size() == 1);

            REQUIRE(data_d->mapping_subscribe_[0][0] == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(data_d->mapping_request_[0][0] == aergo::module::ChannelIdentifier{2, 0});
            REQUIRE(data_d->mapping_request_[0][1] == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(data_d->mapping_publish_[1][0] == aergo::module::ChannelIdentifier{0, 0});



            REQUIRE(core.removeModule(1, true) == Core::RemoveResult::SUCCESS);
            
            new_state_id = core.getModulesMappingStateId();
            REQUIRE(new_state_id > last_state_id);
            last_state_id = new_state_id;

            REQUIRE(core.getCreatedModulesCount() == 5);
            REQUIRE(core.getCreatedModulesInfo(0) != nullptr);
            REQUIRE(core.getCreatedModulesInfo(1) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(2) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(3) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(4) == nullptr);
            REQUIRE(core.getCreatedModulesInfo(5) == nullptr);
            
            REQUIRE(core.getExistingPublishChannels("message_1/v1:int").size() == 0);
            REQUIRE(core.getExistingResponseChannels("message_2/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_3/v1:int").size() == 1);
            REQUIRE(core.getExistingResponseChannels("message_4/v1:int").size() == 1);
            REQUIRE(core.getExistingPublishChannels("message_5/v1:int").size() == 0);
            REQUIRE(core.getExistingPublishChannels("message_6/v1:int").size() == 0);
            
            REQUIRE(core.getLoadedModulesCount() == 5);


            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a == nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b == nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c == nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d == nullptr);

            REQUIRE(data_e->mapping_subscribe_.size() == 1);
            REQUIRE(data_e->mapping_request_.size() == 0);
            REQUIRE(data_e->mapping_publish_.size() == 1);
            REQUIRE(data_e->mapping_response_.size() == 1);
            
            REQUIRE(data_e->mapping_subscribe_[0].size() == 0);
            REQUIRE(data_e->mapping_publish_[0].size() == 0);
            REQUIRE(data_e->mapping_response_[0].size() == 0);
        }

        SECTION("Test communication between modules")
        {
            REQUIRE_NOTHROW(data_e = core.getCreatedModulesInfo(0));
            REQUIRE(data_e != nullptr);

            REQUIRE_NOTHROW(data_a = core.getCreatedModulesInfo(1));
            REQUIRE(data_a != nullptr);
            
            REQUIRE_NOTHROW(data_b = core.getCreatedModulesInfo(2));
            REQUIRE(data_b != nullptr);
            
            REQUIRE_NOTHROW(data_c = core.getCreatedModulesInfo(3));
            REQUIRE(data_c != nullptr);
            
            REQUIRE_NOTHROW(data_d = core.getCreatedModulesInfo(4));
            REQUIRE(data_d != nullptr);

            ModuleCommon* module_a = (ModuleCommon*) data_a->module_.get();
            ModuleCommon* module_b = (ModuleCommon*) data_b->module_.get();
            ModuleCommon* module_c = (ModuleCommon*) data_c->module_.get();
            ModuleCommon* module_d = (ModuleCommon*) data_d->module_.get();
            ModuleCommon* module_e = (ModuleCommon*) data_e->module_.get();

            REQUIRE(module_a != nullptr);
            REQUIRE(module_b != nullptr);
            REQUIRE(module_c != nullptr);
            REQUIRE(module_d != nullptr);
            REQUIRE(module_e != nullptr);

            REQUIRE(module_a->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_b->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::INVALID);


            REQUIRE_NOTHROW(module_d->publish(0, 0));
            REQUIRE(module_a->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_b->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::INVALID);

            uint32_t sleep_ms = 20;

            REQUIRE_NOTHROW(module_a->publish(0, 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_e->last_msg_data_ == 1);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{1, 0});

            REQUIRE(module_a->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_b->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            

            REQUIRE_NOTHROW(module_a->publish(1, 2));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            
            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_e->last_msg_data_ == 1);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{1, 0});
            
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::INVALID);
            
            REQUIRE(module_b->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_b->last_msg_data_ == 2);
            REQUIRE(module_b->last_channel_id_ == 0);
            REQUIRE(module_b->last_source_channel_ == aergo::module::ChannelIdentifier{1, 1});
            
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_c->last_msg_data_ == 2);
            REQUIRE(module_c->last_channel_id_ == 0);
            REQUIRE(module_c->last_source_channel_ == aergo::module::ChannelIdentifier{1, 1});


            REQUIRE_NOTHROW(module_e->publish(0, 3));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_d->last_msg_data_ == 3);
            REQUIRE(module_d->last_channel_id_ == 0);
            REQUIRE(module_d->last_source_channel_ == aergo::module::ChannelIdentifier{0, 0});


            REQUIRE_NOTHROW(module_b->publish(0, 4));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_e->last_msg_data_ == 4);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{2, 0});


            REQUIRE_NOTHROW(module_c->publish(0, 5));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_e->last_msg_data_ == 5);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{3, 0});


            REQUIRE_NOTHROW(module_d->publish(1, 6));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::MESSAGE);
            REQUIRE(module_e->last_msg_data_ == 6);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{4, 1});


            uint64_t req_id, req_id_2;
            REQUIRE_NOTHROW(req_id = module_c->request(0, {0, 0}, 7));
            std::this_thread::sleep_for(std::chrono::milliseconds(2 * sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::REQUEST);
            REQUIRE(module_e->last_msg_data_ == 7);
            REQUIRE(module_e->last_msg_id_ == req_id);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::RESPONSE);
            REQUIRE(module_c->last_msg_data_ == 7);
            REQUIRE(module_c->last_msg_id_ == req_id);
            REQUIRE(module_c->last_channel_id_ == 0);
            REQUIRE(module_c->last_source_channel_ == aergo::module::ChannelIdentifier{0, 0});
            

            REQUIRE_NOTHROW(req_id_2 = module_c->request(0, {0, 0}, 8));
            std::this_thread::sleep_for(std::chrono::milliseconds(2 * sleep_ms));

            REQUIRE(module_e->last_msg_type_ == ModuleCommon::msg_type::REQUEST);
            REQUIRE(module_e->last_msg_data_ == 8);
            REQUIRE(module_e->last_msg_id_ == req_id_2);
            REQUIRE(module_e->last_channel_id_ == 0);
            REQUIRE(module_e->last_source_channel_ == aergo::module::ChannelIdentifier{3, 0});
            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::RESPONSE);
            REQUIRE(module_c->last_msg_data_ == 8);
            REQUIRE(module_c->last_msg_id_ == req_id_2);
            REQUIRE(module_c->last_channel_id_ == 0);
            REQUIRE(module_c->last_source_channel_ == aergo::module::ChannelIdentifier{0, 0});
            REQUIRE(req_id_2 != req_id);


            REQUIRE_NOTHROW(req_id = module_d->request(0, {2, 0}, 9));
            std::this_thread::sleep_for(std::chrono::milliseconds(2 * sleep_ms));
            
            REQUIRE(module_b->last_msg_type_ == ModuleCommon::msg_type::REQUEST);
            REQUIRE(module_b->last_msg_data_ == 9);
            REQUIRE(module_b->last_msg_id_ == req_id);
            REQUIRE(module_b->last_channel_id_ == 0);
            REQUIRE(module_b->last_source_channel_ == aergo::module::ChannelIdentifier{4, 0});
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::RESPONSE);
            REQUIRE(module_d->last_msg_data_ == 9);
            REQUIRE(module_d->last_msg_id_ == req_id);
            REQUIRE(module_d->last_channel_id_ == 0);
            REQUIRE(module_d->last_source_channel_ == aergo::module::ChannelIdentifier{2, 0});


            REQUIRE_NOTHROW(req_id = module_d->request(0, {3, 0}, 10));
            std::this_thread::sleep_for(std::chrono::milliseconds(2 * sleep_ms));

            REQUIRE(module_c->last_msg_type_ == ModuleCommon::msg_type::REQUEST);
            REQUIRE(module_c->last_msg_data_ == 10);
            REQUIRE(module_c->last_msg_id_ == req_id);
            REQUIRE(module_c->last_channel_id_ == 0);
            REQUIRE(module_c->last_source_channel_ == aergo::module::ChannelIdentifier{4, 0});
            REQUIRE(module_d->last_msg_type_ == ModuleCommon::msg_type::RESPONSE);
            REQUIRE(module_d->last_msg_data_ == 10);
            REQUIRE(module_d->last_msg_id_ == req_id);
            REQUIRE(module_d->last_channel_id_ == 0);
            REQUIRE(module_d->last_source_channel_ == aergo::module::ChannelIdentifier{3, 0});
        }
    }
}