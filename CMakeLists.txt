cmake_minimum_required(VERSION 3.20)
project(foxy-jumpscare VERSION 0.1.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
set(SHARED_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
pkg_check_modules(CORE REQUIRED IMPORTED_TARGET
    wayland-client>=1.20 libpng>=1.6 libffi zlib miniaudio
)
foreach(LIBRARY IN LISTS CORE_LINK_LIBRARIES)
    if(NOT LIBRARY MATCHES "^(m|rt|pthread|dl)$" AND NOT LIBRARY MATCHES "\\.a$")
        message(FATAL_ERROR "Static library required, but unavailable: ${LIBRARY}")
    endif()
endforeach()
set(CMAKE_FIND_LIBRARY_SUFFIXES "${SHARED_LIBRARY_SUFFIXES}")
pkg_check_modules(AUDIO_BACKENDS REQUIRED IMPORTED_TARGET libpulse alsa sndio)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WLR_PROTOCOLS REQUIRED wlr-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WLR_PROTOCOLS_DIR wlr-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTOCOL_DIR}")
set(PROTOCOL_SOURCES)
foreach(PROTOCOL IN ITEMS xdg-shell wlr-layer-shell-unstable-v1 viewporter)
    if(PROTOCOL STREQUAL "xdg-shell")
        set(XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
    elseif(PROTOCOL STREQUAL "viewporter")
        set(XML "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
    else()
        set(XML "${WLR_PROTOCOLS_DIR}/unstable/wlr-layer-shell-unstable-v1.xml")
    endif()
    set(HEADER "${PROTOCOL_DIR}/${PROTOCOL}-client-protocol.h")
    set(CODE "${PROTOCOL_DIR}/${PROTOCOL}-protocol.c")
    add_custom_command(
        OUTPUT "${HEADER}" "${CODE}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${XML}" "${HEADER}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${XML}" "${CODE}"
        DEPENDS "${XML}"
        VERBATIM
    )
    list(APPEND PROTOCOL_SOURCES "${HEADER}" "${CODE}")
endforeach()

add_executable(foxy-jumpscare main.c ${PROTOCOL_SOURCES})
target_compile_features(foxy-jumpscare PRIVATE c_std_11)
target_include_directories(foxy-jumpscare PRIVATE "${PROTOCOL_DIR}")
target_link_libraries(foxy-jumpscare PRIVATE
    PkgConfig::CORE PkgConfig::AUDIO_BACKENDS Threads::Threads m ${CMAKE_DL_LIBS}
)

set_target_properties(foxy-jumpscare PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(foxy-jumpscare PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Oz;-ffunction-sections;-fdata-sections;-fvisibility=hidden;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-fomit-frame-pointer>"
    )
    target_link_options(foxy-jumpscare PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Oz>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:LINKER:--gc-sections,--as-needed,-s>"
    )
endif()

include(GNUInstallDirs)
install(TARGETS foxy-jumpscare RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
