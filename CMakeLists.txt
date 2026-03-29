cmake_minimum_required(VERSION 3.20)
project(litware CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This build is for Linux only. Use Visual Studio for Windows builds.")
endif()

# Options
option(USE_LIBFUNCHOOK "Use libfunchook instead of builtin hook" OFF)
option(LITWARE_DEBUG "Enable debug logging" ON)
option(LITWARE_BLUR "Enable Vulkan blur effect (WIP)" OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DLITWARE_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(LITWARE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLITWARE_DEBUG")
endif()

# Main library
add_library(litware SHARED
    # Core entry
    litware-dll/src/linux_init.cpp
    litware-dll/src/entry.cpp

    # Bypass and core
    litware-dll/src/bypass.cpp
    litware-dll/src/core/interfaces.cpp
    litware-dll/src/core/globals.cpp

    # Debug
    litware-dll/src/debug.cpp

    # Fonts
    litware-dll/src/Fonts.cpp

    # Render hook (Vulkan implementation)
    litware-dll/src/hooks/render_hook_vk.cpp

    # ImGui core
    litware-dll/external/imgui/imgui.cpp
    litware-dll/external/imgui/imgui_demo.cpp
    litware-dll/external/imgui/imgui_draw.cpp
    litware-dll/external/imgui/imgui_tables.cpp
    litware-dll/external/imgui/imgui_widgets.cpp

    # ImGui backends
    litware-dll/external/imgui/backends/imgui_impl_vulkan.cpp
    litware-dll/external/imgui/backends/imgui_impl_sdl2.cpp
)

# Include directories
target_include_directories(litware PRIVATE
    litware-dll/src
    litware-dll/external/imgui
    litware-dll/external/imgui/backends
    vendor/omath/include
)

# Find dependencies
find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

# Link libraries
target_link_libraries(litware
    Vulkan::Vulkan
    SDL2::SDL2
    dl
    pthread
    m  # math library
)

# Compile definitions
target_compile_definitions(litware PRIVATE
    LITWARE_LINUX
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES  # Use dynamic loading for Vulkan
    IMGUI_API=
)

# Output configuration
set_target_properties(litware PROPERTIES
    OUTPUT_NAME "litware"
    PREFIX ""
    SUFFIX ".so"
    VERSION "1.0"
    SOVERSION "1"
)

# Installation (optional)
install(TARGETS litware LIBRARY DESTINATION lib)

# Print build info
message(STATUS "")
message(STATUS "=== LitWare Build Configuration ===")
message(STATUS "Platform: Linux")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Debug Logging: ${LITWARE_DEBUG}")
message(STATUS "Vulkan Blur: ${LITWARE_BLUR}")
message(STATUS "===================================")
message(STATUS "Output: litware.so")
message(STATUS "")
