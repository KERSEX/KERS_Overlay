qt_add_library(kers_common STATIC
    paths.cpp
    jsonfile.cpp
    log.cpp
    hud_state.cpp
)
target_include_directories(kers_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kers_common PUBLIC Qt6::Core)
# Versionsnummer auch zur Compile-Zeit (Log, Fenstertitel, User-Agent des Updaters)
target_compile_definitions(kers_common PUBLIC KERS_VERSION_STR="${KERS_VERSION}")
