add_library(melodeus_analysis STATIC
    bpm.cpp
    key.cpp
    beatgrid.cpp
)
target_include_directories(melodeus_analysis PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(melodeus_analysis PUBLIC melodeus_waveform)
melodeus_set_warnings(melodeus_analysis)
