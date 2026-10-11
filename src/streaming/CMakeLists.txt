# F4: streaming (yt-dlp/ffmpeg por pipes, jitter buffer, caché LRU).
# Sólo Windows (CreateProcessW + job objects); hermano de melodeus_audio.
add_library(melodeus_streaming STATIC
    disk_cache.cpp
    stream_loader.cpp
    ytdlp_source.cpp
)
target_include_directories(melodeus_streaming PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(melodeus_streaming PUBLIC melodeus_core melodeus_audio)
melodeus_set_warnings(melodeus_streaming)
