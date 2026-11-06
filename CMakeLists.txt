cmake_minimum_required(VERSION 3.25)
project(MoviePlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libmpv: pinned to one release and verified by hash. To upgrade, change these
# three values together (the hash is listed on the GitHub release page).
set(LIBMPV_RELEASE 20261106)
set(LIBMPV_BUILD 20261106-git-9028187538)
set(LIBMPV_SHA256 b5a3cc91b5dabf88ad5bb21d000834da4fd0bbda30f4584d39c082127bd62414)

set(LIBMPV_DIR "${CMAKE_SOURCE_DIR}/third_party/libmpv-${LIBMPV_BUILD}")
if(NOT EXISTS "${LIBMPV_DIR}/libmpv-2.dll")
    set(LIBMPV_ARCHIVE "${CMAKE_BINARY_DIR}/libmpv-${LIBMPV_BUILD}.7z")
    message(STATUS "Downloading libmpv ${LIBMPV_BUILD}")
    file(DOWNLOAD
        "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/${LIBMPV_RELEASE}/mpv-dev-x86_64-${LIBMPV_BUILD}.7z"
        "${LIBMPV_ARCHIVE}"
        EXPECTED_HASH SHA256=${LIBMPV_SHA256}
        TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${LIBMPV_ARCHIVE}" DESTINATION "${LIBMPV_DIR}")
endif()

add_library(libmpv SHARED IMPORTED)
set_target_properties(libmpv PROPERTIES
    IMPORTED_LOCATION "${LIBMPV_DIR}/libmpv-2.dll"
    IMPORTED_IMPLIB "${LIBMPV_DIR}/libmpv.dll.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBMPV_DIR}/include")

add_executable(MoviePlayer WIN32
    src/main.cpp
    src/ContextMenu.cpp
    src/ControlBar.cpp
    src/FileDialogs.cpp
    src/MainWindow.cpp
    src/MpvPlayer.cpp)

target_compile_definitions(MoviePlayer PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(MoviePlayer PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(MoviePlayer PRIVATE libmpv comdlg32 ole32 shell32 imm32 d3d11 dxgi d2d1 dwrite dcomp)

# The executable loads libmpv-2.dll from its own folder.
add_custom_command(TARGET MoviePlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:libmpv>" "$<TARGET_FILE_DIR:MoviePlayer>")
