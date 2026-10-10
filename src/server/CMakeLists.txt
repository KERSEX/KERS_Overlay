qt_add_library(kers_server STATIC
    constants.cpp
    packets.cpp
    pyconv.h
    tracks.cpp
    settings_store.cpp
    race_state.cpp
    payload.cpp
    http_server.cpp
    access_guard.cpp
    obs_link.cpp
    sse_hub.cpp
    recorder.cpp
    cam_keys.cpp
    kers_server.cpp
    routes.cpp
)
target_include_directories(kers_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kers_server PUBLIC
    kers_common
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets         # OBS-Steuerung (obs-websocket v5), ab 0.3.1
    Qt6::ZlibPrivate        # gzip fuer die .f1rec-Aufnahmen (Qt-eigenes zlib)
)
if(WIN32)
    target_link_libraries(kers_server PUBLIC user32 crypt32)   # SendInput (Kamera-Tasten), DPAPI (OBS-Passwort)
endif()
