set(LIB_NAME helpers___calibrated_stereo_robot_messages)
add_library(${LIB_NAME} INTERFACE)
target_include_directories(${LIB_NAME} INTERFACE include)
target_link_libraries(${LIB_NAME} INTERFACE module_common helpers___camera_pose_helper helpers___robot_interface)
