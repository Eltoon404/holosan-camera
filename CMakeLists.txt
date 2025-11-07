cmake_minimum_required(VERSION 3.26)
project(webcam_holoviz CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Holoscan SDK
find_package(holoscan REQUIRED)

# Sources
set(SRC
  main.cpp
)

add_executable(webcam_holoviz ${SRC})

# Project include path
target_include_directories(webcam_holoviz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link core + operators used
target_link_libraries(webcam_holoviz
  PRIVATE
    holoscan::core
    holoscan::ops::v4l2
    holoscan::ops::holoviz
    holoscan::ops::format_converter
)

# Link GXF multimedia (VideoBuffer symbols) explicitly
find_library(GXF_MULTIMEDIA_LIB NAMES gxf_multimedia
  HINTS $ENV{HOLOSCAN_LIB_DIR}
  PATHS /opt/nvidia/holoscan/lib /opt/nvidia/holoscan/lib64
)
if (GXF_MULTIMEDIA_LIB)
  target_link_libraries(webcam_holoviz PRIVATE ${GXF_MULTIMEDIA_LIB})
else()
  message(WARNING "libgxf_multimedia not found; linking may fail if VideoBuffer is used")
endif()
