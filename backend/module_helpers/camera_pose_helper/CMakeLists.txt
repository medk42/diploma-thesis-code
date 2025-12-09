set(LIB_NAME helpers___camera_pose_helper)
add_library(${LIB_NAME} INTERFACE)

target_include_directories(${LIB_NAME} INTERFACE include)

target_link_libraries(${LIB_NAME} INTERFACE
    module_common
    helpers___camera_messages
    helpers___robot_interface
)
