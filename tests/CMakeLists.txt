# Tests (Qt Test / CTest). Ausfuehren nach dem Build:
#   ctest --test-dir build --output-on-failure
find_package(Qt6 REQUIRED COMPONENTS Test)

function(kers_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kers_hud kers_server kers_common Qt6::Test)
    target_link_options(${name} PRIVATE -static)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

kers_test(tst_quali_status)
kers_test(tst_derive)
kers_test(tst_packets)
kers_test(tst_settings)
kers_test(tst_tracks)
kers_test(tst_server)
kers_test(tst_updater)
kers_test(tst_local)
kers_test(tst_access)
kers_test(tst_champ)
kers_test(tst_obs)
kers_test(tst_design)
# tst_design braucht eine GUI-Anwendung (Schriftdatenbank im Theme). Im statischen Qt
# muss das offscreen-Plugin ausdruecklich hinein - sonst zeigt Windows eine Fehlermeldung
# als Fenster, und der Test wartet ewig darauf.
qt_import_plugins(tst_design INCLUDE Qt::QOffscreenIntegrationPlugin)
target_link_libraries(tst_obs PRIVATE Qt6::WebSockets)
# LocalServer, LocalFeed, LocalDataSource gehoeren zur App - Quellen direkt mitbauen
target_sources(tst_local PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/server_control.cpp ${CMAKE_SOURCE_DIR}/src/app/server_control.h
    ${CMAKE_SOURCE_DIR}/src/app/local_link.cpp ${CMAKE_SOURCE_DIR}/src/app/local_link.h)
target_include_directories(tst_local PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
# Spielt in tst_local den Server der Python-Fassung (data\main.exe) - siehe schlaefer.cpp
add_executable(kers_schlaefer schlaefer.cpp)
target_link_libraries(kers_schlaefer PRIVATE ws2_32)
target_link_options(kers_schlaefer PRIVATE -static)
add_dependencies(tst_local kers_schlaefer)
target_compile_definitions(tst_local PRIVATE KERS_SCHLAEFER="$<TARGET_FILE:kers_schlaefer>")
# Der Updater gehoert zur App (keine eigene Bibliothek) - die Quelle direkt mitbauen
target_sources(tst_updater PRIVATE ${CMAKE_SOURCE_DIR}/src/app/updater.cpp ${CMAKE_SOURCE_DIR}/src/app/updater.h)
target_include_directories(tst_updater PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
# tst_server braucht ein paar echte Seiten und Dateien aus der EXE
qt_add_resources(tst_server "testassets" PREFIX "/" BASE ${CMAKE_SOURCE_DIR}
    FILES ${CMAKE_SOURCE_DIR}/static/css/core.css ${CMAKE_SOURCE_DIR}/templates/parts_index.html)

# Golden-Vergleich mit main.py: kers_replay (C++) + golden\golden_dump.py (Python)
# + golden\vergleich.py. Kein CTest - braucht eine .f1rec und Flask.
qt_add_executable(kers_replay golden/kers_replay.cpp)
target_link_libraries(kers_replay PRIVATE kers_server kers_common Qt6::ZlibPrivate)
target_link_options(kers_replay PRIVATE -static)
