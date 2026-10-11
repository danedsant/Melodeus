# Spike F0: se construye sólo si las dependencias externas están presentes.
# (JUCE y Dear ImGui se poblan con scripts/fetch_deps.ps1; ver Docs.md §6/F0)

set(MELODEUS_IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui")

if(NOT EXISTS "${MELODEUS_IMGUI_DIR}/imgui.cpp")
    message(STATUS
        "melodeus: external/imgui no presente -> se omite el spike F0 (app). "
        "Ejecuta scripts/fetch_deps.ps1 para poblar las dependencias.")
    return()
endif()

# Dear ImGui: third-party; fuera de nuestras reglas de avisos.
add_library(melodeus_imgui STATIC
    ${MELODEUS_IMGUI_DIR}/imgui.cpp
    ${MELODEUS_IMGUI_DIR}/imgui_draw.cpp
    ${MELODEUS_IMGUI_DIR}/imgui_tables.cpp
    ${MELODEUS_IMGUI_DIR}/imgui_widgets.cpp
    ${MELODEUS_IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${MELODEUS_IMGUI_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(melodeus_imgui PUBLIC
    ${MELODEUS_IMGUI_DIR}
    ${MELODEUS_IMGUI_DIR}/backends
)
if(MSVC)
    target_compile_definitions(melodeus_imgui PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(melodeus WIN32
    main.cpp
    app_state.cpp
    render/device_d3d12.cpp
    render/imgui_layer.cpp
    spike_winsnd.cpp
)

target_include_directories(melodeus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(melodeus PRIVATE melodeus_core melodeus_imgui)
melodeus_set_warnings(melodeus)
melodeus_set_audio_options(melodeus)

if(TARGET juce::juce_audio_devices)
    target_link_libraries(melodeus PRIVATE juce::juce_audio_devices)
    target_compile_definitions(melodeus PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
endif()

if(MSVC)
    target_link_libraries(melodeus PRIVATE
        d3d12 dxgi dxguid ole32 user32 gdi32 shell32 avrt winmm)
    target_compile_definitions(melodeus PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# dxc/fxc: los shaders se compilan con el Windows SDK (fxc.exe) en F2; por
# ahora el spike usa shaders inline de ImGui.
