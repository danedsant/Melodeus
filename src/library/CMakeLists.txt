# Third-party SQLite (amalgamation): fuera de nuestras reglas de avisos.
add_library(melodeus_sqlite STATIC ${CMAKE_SOURCE_DIR}/external/sqlite/sqlite3.c)
target_include_directories(melodeus_sqlite PUBLIC ${CMAKE_SOURCE_DIR}/external/sqlite)
target_compile_definitions(melodeus_sqlite PUBLIC SQLITE_ENABLE_FTS5 SQLITE_ENABLE_FTS4
    SQLITE_ENABLE_JSON1 SQLITE_THREADSAFE=1)

add_library(melodeus_library STATIC
    blob.cpp
    database.cpp
    importer.cpp
)
target_include_directories(melodeus_library PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(melodeus_library PUBLIC melodeus_sqlite melodeus_audio melodeus_analysis)
melodeus_set_warnings(melodeus_library)
