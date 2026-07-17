cmake_minimum_required(VERSION 3.15)
project(AetherInjector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX)

include(FetchContent)

FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.8.zip
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

set(AETHER_SOURCES
    src/main.cpp
    src/injector/injector.cpp
    src/injector/utils.cpp
    src/ui/ui.cpp
)

add_executable(AetherInjector ${AETHER_SOURCES} ${IMGUI_SOURCES})

target_include_directories(AetherInjector PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(AetherInjector PRIVATE
    d3d11
    d3dcompiler
    dwmapi
)

set_target_properties(AetherInjector PROPERTIES
    WIN32_EXECUTABLE TRUE
)

add_library(AetherTestDLL SHARED src/test_dll.cpp)
