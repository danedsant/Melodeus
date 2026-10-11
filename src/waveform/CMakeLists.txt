# F2: pirámide de waveform + FFT de display.
add_library(melodeus_waveform STATIC
    waveform.cpp
)
target_link_libraries(melodeus_waveform PUBLIC melodeus_core melodeus_audio)
melodeus_set_warnings(melodeus_waveform)
melodeus_set_audio_options(melodeus_waveform)
