# EINE Quelle fuer die Versionsnummer: static/version.txt.
#
# Sie steht dort, weil der Selbst-Updater sie beim Start liest (eingebettet in
# der EXE) und mit dem GitHub-Tag KERS_SubsystemsV<x.y.z> vergleicht. Hier wird
# sie zusaetzlich zur CMake-Projektversion und zur Versionsressource der EXE.

file(READ "${CMAKE_CURRENT_LIST_DIR}/../static/version.txt" KERS_VERSION)
string(STRIP "${KERS_VERSION}" KERS_VERSION)
if(NOT KERS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "static/version.txt enthaelt keine Version x.y.z: '${KERS_VERSION}'")
endif()
# Fuer VERSIONINFO im .rc: Kommas statt Punkte
string(REPLACE "." "," KERS_VERSION_RC "${KERS_VERSION}")
