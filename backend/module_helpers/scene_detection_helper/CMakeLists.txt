set(LIB_NAME helpers___scene_detection_helper)
add_library(${LIB_NAME} INTERFACE)
target_include_directories(${LIB_NAME} INTERFACE include)
target_link_libraries(${LIB_NAME} INTERFACE module_common helpers___serialization_helper)

