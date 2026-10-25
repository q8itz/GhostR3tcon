# graw - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(graw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# A cross-build's SDK only has a Windows rexglue.exe; codegen runs on the build
# machine, so -DREXGLUE_HOST_TOOL=<path> points it at that machine's rexglue.
if(REXGLUE_HOST_TOOL)
    set_target_properties(rex::rexglue PROPERTIES
        IMPORTED_LOCATION "${REXGLUE_HOST_TOOL}"
        IMPORTED_LOCATION_RELEASE "${REXGLUE_HOST_TOOL}")
endif()

# Sources
set(GRAW_SOURCES
    src/main.cpp
    src/graw_app.cpp
    src/graw_disc.cpp
    src/graw_launcher.cpp
    src/graw_menu.cpp
    src/graw_settings.cpp
    src/sha1.cpp
)

if(WIN32)
    add_executable(graw WIN32 ${GRAW_SOURCES})
else()
    add_executable(graw ${GRAW_SOURCES})
endif()

rexglue_setup_target(graw GPU_PLUGINS xenos)
