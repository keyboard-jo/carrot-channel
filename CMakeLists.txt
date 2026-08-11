cmake_minimum_required(VERSION 3.10)

project(carrot-spsc
    VERSION 0.1.0
    LANGUAGES CXX
)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_library(carrot-spsc INTERFACE
    examples/example.cpp)

target_include_directories(carrot-spsc
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(carrot-spsc INTERFACE cxx_std_20)

include(CTest)
enable_testing()

add_executable(example examples/example.cpp)

target_link_libraries(example PRIVATE carrot-spsc)