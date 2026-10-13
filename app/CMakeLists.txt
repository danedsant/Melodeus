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

# --- Shaders HLSL (waveform) compilados con fxc.exe del Windows SDK ----
set(MELODEUS_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/waveform.hlsl)
set(MELODEUS_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(MELODEUS_FXC_FOUND OFF)
find_program(MELODEUS_FXC fxc
    HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}x64"
          "$ENV{WindowsSdkDir}/bin/x64")
if(NOT MELODEUS_FXC)
    file(GLOB _fxc_versions "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/fxc.exe")
    list(SORT _fxc_versions)
    list(REVERSE _fxc_versions)
    if(_fxc_versions)
        list(GET _fxc_versions 0 MELODEUS_FXC)
    endif()
endif()

add_executable(melodeus WIN32
    main.cpp
    app_state.cpp
    deck_panel.cpp
    help_ui.cpp
    i18n.cpp
    library_panel.cpp
    mixer_panel.cpp
    render/device_d3d12.cpp
    render/imgui_layer.cpp
    render/logo_texture.cpp
    render/waveform_pass.cpp
    settings.cpp
    settings_ui.cpp
    spike_winsnd.cpp
    res/melodeus.rc
)

set_target_properties(melodeus PROPERTIES OUTPUT_NAME "Melodeus")

if(MELODEUS_FXC)
    set(MELODEUS_FXC_FOUND ON)
    add_custom_command(
        OUTPUT ${MELODEUS_SHADER_OUT}/waveform.vso ${MELODEUS_SHADER_OUT}/waveform.pso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MELODEUS_SHADER_OUT}
        COMMAND "${MELODEUS_FXC}" /nologo /T vs_5_1 /E vs_main
                /Fo ${MELODEUS_SHADER_OUT}/waveform.vso ${MELODEUS_SHADER_SRC}
        COMMAND "${MELODEUS_FXC}" /nologo /T ps_5_1 /E ps_main
                /Fo ${MELODEUS_SHADER_OUT}/waveform.pso ${MELODEUS_SHADER_SRC}
        DEPENDS ${MELODEUS_SHADER_SRC}
        COMMENT "fxc: waveform.hlsl -> .vso/.pso"
        VERBATIM)
    add_custom_target(melodeus_shaders DEPENDS
        ${MELODEUS_SHADER_OUT}/waveform.vso ${MELODEUS_SHADER_OUT}/waveform.pso)
    add_dependencies(melodeus melodeus_shaders)
    add_custom_command(TARGET melodeus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MELODEUS_SHADER_OUT}/waveform.vso
            ${MELODEUS_SHADER_OUT}/waveform.pso
            $<TARGET_FILE_DIR:melodeus>
        COMMENT "copiando .cso junto al exe")
    message(STATUS "melodeus: fxc -> ${MELODEUS_FXC}")
else()
    message(WARNING "melodeus: fxc.exe no encontrado; el pase de waveform se omite.")
endif()

# --- Fuentes UI (Clash Display + JetBrains Mono + Phosphor, F3.6) ----------
add_custom_command(TARGET melodeus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_FILE_DIR:melodeus>/fonts
    COMMENT "copiando fuentes UI junto al exe")

target_include_directories(melodeus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(melodeus PRIVATE melodeus_core melodeus_audio melodeus_waveform
    melodeus_analysis melodeus_library melodeus_streaming melodeus_imgui)
if(MELODEUS_FXC_FOUND)
    target_compile_definitions(melodeus PRIVATE MELODEUS_HAVE_WAVEFORM_PASS=1)
endif()
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

# fxc se localiza al principio del archivo (bloque "Shaders HLSL").
