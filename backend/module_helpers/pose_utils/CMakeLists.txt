set(LIB_NAME helpers___pose_utils)

# Find OpenCV
find_package(OpenCV 4 REQUIRED core calib3d aruco imgproc imgcodecs highgui)
include_directories(${OpenCV_INCLUDE_DIRS})

add_library(${LIB_NAME} INTERFACE)
target_include_directories(${LIB_NAME} INTERFACE include)
target_link_libraries(${LIB_NAME} INTERFACE opencv_core opencv_calib3d)