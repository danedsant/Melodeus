add_executable(latency_probe
    latency_probe.cpp
)
melodeus_set_warnings(latency_probe)
target_link_libraries(latency_probe PRIVATE melodeus_core)
if(MSVC)
    target_link_libraries(latency_probe PRIVATE ole32 uuid mmdevapi avrt)
endif()

# --- F0.4: ONNX Runtime (paquete DirectML) ---
set(MELODEUS_ORT_DIR "${CMAKE_SOURCE_DIR}/external/onnxruntime")
if(EXISTS "${MELODEUS_ORT_DIR}/runtimes/win-x64/native/onnxruntime.lib")
    add_executable(ort_probe ort_probe.cpp)
    melodeus_set_warnings(ort_probe)
    target_include_directories(ort_probe SYSTEM PRIVATE "${MELODEUS_ORT_DIR}/build/native/include")
    target_link_libraries(ort_probe PRIVATE "${MELODEUS_ORT_DIR}/runtimes/win-x64/native/onnxruntime.lib")
    add_custom_command(TARGET ort_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MELODEUS_ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll"
                "$<TARGET_FILE_DIR:ort_probe>"
        COMMENT "Copiando onnxruntime.dll junto a ort_probe")
else()
    message(STATUS "ort_probe omitido (ejecute scripts/fetch_deps.ps1; onnxruntime no presente)")
endif()

# --- F0.5: yt-dlp + ffmpeg por pipe (solo Windows API; binarios en external/) ---
add_executable(stream_probe stream_probe.cpp)
melodeus_set_warnings(stream_probe)
