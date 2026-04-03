set(LITWARE_DLL_SOURCES
    src/bypass.cpp
    src/debug.cpp
    src/dllmain.cpp
    src/entry.cpp
    src/Fonts.cpp
    src/hooks/render_hook.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_dx11.cpp
    external/imgui/backends/imgui_impl_win32.cpp
    external/minhook/src/buffer.c
    external/minhook/src/hook.c
    external/minhook/src/trampoline.c
    external/minhook/src/hde/hde64.c
)

add_library(litware-dll SHARED ${LITWARE_DLL_SOURCES})

target_compile_features(litware-dll PRIVATE cxx_std_23)

target_compile_definitions(litware-dll
    PRIVATE
        _WINDOWS
        _USRDLL
        NOMINMAX
        $<$<CONFIG:Debug>:LITWARE_DEBUG>
)

target_include_directories(litware-dll
    PRIVATE
        ${CMAKE_SOURCE_DIR}/cmake/compat/windows-headers
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/external/minhook/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/minhook/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/minhook/src/hde
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/omath/include
)

set_target_properties(litware-dll PROPERTIES
    PREFIX ""
    OUTPUT_NAME "litware-dll"
)

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    set_target_properties(litware-dll PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_CURRENT_SOURCE_DIR}/bin/${cfg}"
        LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_CURRENT_SOURCE_DIR}/bin/${cfg}"
        ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_CURRENT_SOURCE_DIR}/obj/${cfg}"
    )
endforeach()

target_link_libraries(litware-dll
    PRIVATE
        d3d11
        dwmapi
        dxgi
        d3dcompiler
        psapi
        winmm
)

if(MSVC)
    target_compile_options(litware-dll PRIVATE
        /W4
        /EHsc
        /sdl
        /wd6011
        /wd6387
        /wd6388
        /wd6001
        /wd4244
        /wd4101
        /wd4204
        /wd4309
        /wd4456
        /wd4457
        /wd4458
        /wd4459
        /wd4460
        /wd4461
        /wd4462
        /wd4463
        /wd4464
        /wd4465
        /wd4466
        /wd4467
        /wd4468
        /wd4469
        /wd4470
        /wd6054
        $<$<CONFIG:Release>:/O2 /Ob2 /GL>
    )
    target_link_options(litware-dll PRIVATE /SUBSYSTEM:WINDOWS)
else()
    target_compile_options(litware-dll PRIVATE
        -fms-extensions
        -Winvalid-pch
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        $<$<CONFIG:Release>:-O3>
    )
endif()
