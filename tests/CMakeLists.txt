add_executable(melodeus_tests
    main.cpp
    test_spsc_ring.cpp
    test_prealloc_pool.cpp
    test_rt_guard.cpp
    test_log_rt.cpp
    test_decoder.cpp
    test_deck.cpp
    test_dsp.cpp
    test_engine.cpp
    test_waveform.cpp
)

target_link_libraries(melodeus_tests PRIVATE melodeus_core melodeus_audio melodeus_waveform)
target_include_directories(melodeus_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external/doctest/doctest
)
melodeus_set_warnings(melodeus_tests)
if(MSVC)
    # doctest usa #pragma warning(4865), desconocido en MSVC 19.x, y eso
    # dispara C4619 en cada TU que lo incluye.
    target_compile_options(melodeus_tests PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/msvc_doctest_shut.hpp)
endif()

# El guard RT y el logger son parte del contrato del producto: se fuerzan
# ON en los tests aunque la app se compile sin ellos.
target_compile_definitions(melodeus_tests PRIVATE MELODEUS_STRICT_RT=1)

add_test(NAME unit COMMAND melodeus_tests)
set_tests_properties(unit PROPERTIES TIMEOUT 120)
