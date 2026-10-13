cmake_minimum_required(VERSION 3.24)

project(melodeus
    VERSION 1.1.0
    DESCRIPTION "Melodeus - DJ software de baja latencia para Windows"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

option(MELODEUS_BUILD_TESTS "Construir los tests unitarios" ON)
option(MELODEUS_BUILD_APP   "Construir la aplicacion (spike F0)" ON)
option(MELODEUS_BUILD_TOOLS "Construir herramientas de desarrollo" ON)
option(MELODEUS_STRICT_RT    "Abortar si el hilo RT aloja memoria" ON)
option(MELODEUS_ENABLE_ASIO  "Habilitar ASIO en JUCE (requiere ASIO SDK)" OFF)

include(cmake/Compiler.cmake)

# -------------------------------------------------------------------- JUCE
if(EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/JUCE)
else()
    message(STATUS
        "melodeus: external/JUCE no presente -> app sin audio. "
        "Ejecuta scripts/fetch_deps.ps1.")
endif()

# ------------------------------------------------------------------ core
# OBJECT library: garantiza que los overrides globales de new/delete y el
# resto de simbolos se incluyan en el enlazado (no viven en un archivo).
add_library(melodeus_core OBJECT
    src/core/rt_guard.cpp
    src/core/log_rt.cpp
    src/core/sha1.cpp
)
target_include_directories(melodeus_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(melodeus_core PUBLIC
    MELODEUS_STRICT_RT=$<BOOL:${MELODEUS_STRICT_RT}>
)
melodeus_set_warnings(melodeus_core)

# ------------------------------------------------------------------ audio
add_subdirectory(src/audio)

# ---------------------------------------------------------------- waveform (F2)
add_subdirectory(src/waveform)
add_subdirectory(src/analysis)
add_subdirectory(src/library)

# ---------------------------------------------------------------- streaming (F4)
add_subdirectory(src/streaming)

# ------------------------------------------------------------------ tests
if(MELODEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ tools
if(MELODEUS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ------------------------------------------------------------------- app
if(MELODEUS_BUILD_APP)
    add_subdirectory(app)
endif()
