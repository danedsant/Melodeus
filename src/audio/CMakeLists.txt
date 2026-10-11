# F1: motor de audio (decks, mixer, decode local).
add_library(melodeus_audio STATIC
    decoder.cpp
    engine.cpp
)
target_link_libraries(melodeus_audio PUBLIC melodeus_core)
target_include_directories(melodeus_audio SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external/dr_libs
    ${CMAKE_SOURCE_DIR}/external/minimp3
)
melodeus_set_warnings(melodeus_audio)
melodeus_set_audio_options(melodeus_audio)
