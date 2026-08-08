# SPDX-License-Identifier: MIT
#
# helix-xml test suite.
#
# Standalone: this project builds helix-xml against a *pinned upstream* LVGL,
# not against HelixScreen's patched submodule. That is deliberate - helix-xml is
# its own library and its tests must not depend on the consuming application.
#
#   cmake -S tests -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# Offline / bring-your-own LVGL:
#
#   cmake -S tests -B build -DLVGL_DIR=/path/to/lvgl
#
# LVGL_DIR must be a PRISTINE upstream checkout. In particular it must NOT be
# HelixScreen's lib/lvgl submodule: that one has patches/*.patch applied, which
# inject calls to app-side symbols that a standalone test build cannot resolve.
# A configure-time probe below rejects it with an explanation.
#
# Adding a test: drop a new file in cases/. It is picked up by the glob below
# and becomes its own executable and its own ctest entry. No edits here.

cmake_minimum_required(VERSION 3.20)

project(helix_xml_tests LANGUAGES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# tests/  -> the directory this file lives in
set(HELIX_TEST_DIR   ${CMAKE_CURRENT_SOURCE_DIR})
# helix-xml repo root (holds helix_xml.h and src/)
get_filename_component(HELIX_XML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

set(HELIX_LVGL_TAG "v9.5.0" CACHE STRING "LVGL git tag to test against")

# ---------------------------------------------------------------------------
# LVGL
# ---------------------------------------------------------------------------
# LVGL's own CMake (env_support/cmake/os_desktop.cmake) is driven by cache
# variables, so they must be set *before* the subdirectory is added:
#   LV_BUILD_CONF_DIR             - dir containing our lv_conf.h; also makes
#                                   LVGL define LV_CONF_INCLUDE_SIMPLE PUBLIC
#   CONFIG_LV_BUILD_EXAMPLES/DEMOS- default ON; a large pile of code we never
#                                   link, so turn them off to keep builds fast
#   CONFIG_LV_USE_THORVG_INTERNAL - default ON and drags in the C++ ThorVG
#                                   sources; LV_USE_THORVG_* is 0 in our conf,
#                                   so this is pure build time
#
# LV_BUILD_CONF_DIR is deliberately NOT forced: the conf-guards CI job passes
# -DLV_BUILD_CONF_DIR pointing at a generated variant of lv_conf.h (LV_USE_XML 0,
# LV_USE_TRANSLATION 0, ...) to prove the library's #if guards hold. Forcing it
# would silently ignore that and re-test the default config three times.
if(NOT LV_BUILD_CONF_DIR)
    set(LV_BUILD_CONF_DIR "${HELIX_TEST_DIR}" CACHE PATH "Directory holding lv_conf.h" FORCE)
endif()
get_filename_component(LV_BUILD_CONF_DIR "${LV_BUILD_CONF_DIR}" ABSOLUTE)
message(STATUS "helix-xml tests: lv_conf.h from ${LV_BUILD_CONF_DIR}")

set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(LVGL_DIR)
    # Escape hatch: use a checkout that is already on disk (offline builds, or
    # bisecting an LVGL change). No version check is done - you asked for it.
    get_filename_component(LVGL_DIR "${LVGL_DIR}" ABSOLUTE)
    if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
        message(FATAL_ERROR "LVGL_DIR=${LVGL_DIR} does not look like an LVGL checkout (no lvgl.h)")
    endif()

    # Reject a checkout that a consuming application has patched.
    #
    # The obvious thing for a HelixScreen developer to point LVGL_DIR at is
    # HelixScreen's own lib/lvgl - but that submodule has patches/*.patch
    # applied to it, and some of those inject calls to app-side symbols
    # (helix_crash_note_*) that only exist when linking the application. A
    # standalone test build has no such symbols, so the configure succeeds, the
    # compile succeeds, and every one of the twelve executables dies at link
    # time with a wall of "Undefined symbols ... referenced from liblvgl.a"
    # that says nothing about the real cause.
    #
    # Probing for the marker here turns that into one readable message.
    set(HELIX_PATCH_PROBE_FILES
        "${LVGL_DIR}/src/core/lv_obj_event.c"
        "${LVGL_DIR}/src/core/lv_obj_tree.c"
    )
    set(HELIX_PATCHED_FILES "")
    foreach(probe_file ${HELIX_PATCH_PROBE_FILES})
        if(EXISTS "${probe_file}")
            file(STRINGS "${probe_file}" probe_hits REGEX "helix_crash_note")
            if(probe_hits)
                list(APPEND HELIX_PATCHED_FILES "${probe_file}")
            endif()
        endif()
    endforeach()
    if(HELIX_PATCHED_FILES)
        # One argument, and every line kept short: message() treats each
        # argument as a paragraph and reflows anything long, which shreds a
        # multi-argument message into unreadable fragments.
        string(REPLACE ";" "\n      " HELIX_PATCHED_FILES_TEXT "${HELIX_PATCHED_FILES}")
        message(FATAL_ERROR
"LVGL_DIR is a PATCHED LVGL checkout, not a pristine upstream one.

  LVGL_DIR = ${LVGL_DIR}

  HelixScreen's crash-instrumentation marker (helix_crash_note) is in:
      ${HELIX_PATCHED_FILES_TEXT}

  That is HelixScreen's lib/lvgl submodule with patches/*.patch applied.
  Those patches call app-side symbols (helix_crash_note_event,
  helix_crash_note_sync_del, ...) that do not exist in a standalone test
  build. The compile would succeed and every test executable would then
  fail to link:
      Undefined symbols for architecture <arch>:
        \"_helix_crash_note_event\", referenced from:
            _lv_obj_send_event in liblvgl.a
  LVGL_DIR needs a PRISTINE upstream LVGL checkout. Either:
    - drop -DLVGL_DIR and let FetchContent pull LVGL ${HELIX_LVGL_TAG}
      (the default, and what CI uses), or
    - clone upstream yourself:
        git clone --depth 1 --branch ${HELIX_LVGL_TAG} \\
            https://github.com/lvgl/lvgl.git /tmp/lvgl-pristine
        cmake -S tests -B build -DLVGL_DIR=/tmp/lvgl-pristine
  Do NOT unpatch HelixScreen's submodule to work around this - the
  application needs those patches.")
    endif()

    message(STATUS "helix-xml tests: using LVGL from LVGL_DIR=${LVGL_DIR}")
    set(HELIX_LVGL_SOURCE_DIR "${LVGL_DIR}")
    add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl-build" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG ${HELIX_LVGL_TAG}
        GIT_SHALLOW TRUE
    )
    message(STATUS "helix-xml tests: fetching LVGL ${HELIX_LVGL_TAG}")
    FetchContent_MakeAvailable(lvgl)
    set(HELIX_LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# helix-xml
# ---------------------------------------------------------------------------
# These globs are the library. LVGL 9.5 removed src/xml and src/libs/expat from
# core, so nothing here collides with the lvgl target.
file(GLOB HELIX_XML_SOURCES CONFIGURE_DEPENDS
    ${HELIX_XML_ROOT}/src/xml/*.c
    ${HELIX_XML_ROOT}/src/xml/parsers/*.c
    ${HELIX_XML_ROOT}/src/libs/expat/*.c
)
if(NOT HELIX_XML_SOURCES)
    message(FATAL_ERROR "No helix-xml sources found under ${HELIX_XML_ROOT}/src")
endif()

add_library(helix_xml STATIC ${HELIX_XML_SOURCES})

target_include_directories(helix_xml PUBLIC
    ${HELIX_LVGL_SOURCE_DIR}          # <lvgl.h>, <lvgl_private.h>
    ${HELIX_LVGL_SOURCE_DIR}/src      # <lv_conf_internal.h>, <core/...>, <misc/...>
    ${HELIX_XML_ROOT}                 # "helix_xml.h"
    ${HELIX_XML_ROOT}/src             # "xml/lv_xml.h"
    ${LV_BUILD_CONF_DIR}              # lv_conf.h (reached via LV_CONF_INCLUDE_SIMPLE)
)
target_link_libraries(helix_xml PUBLIC lvgl)

# Vendored/inherited sources: warnings here are noise, and the parent project
# compiles them the same way (-w). Test code below is held to a higher bar.
target_compile_options(helix_xml PRIVATE -w)
target_compile_definitions(helix_xml PRIVATE _GNU_SOURCE)

# ---------------------------------------------------------------------------
# Unity
# ---------------------------------------------------------------------------
add_library(helix_xml_test_unity STATIC ${HELIX_TEST_DIR}/unity/unity.c)
target_include_directories(helix_xml_test_unity PUBLIC ${HELIX_TEST_DIR}/unity)
target_compile_definitions(helix_xml_test_unity PUBLIC
    UNITY_INCLUDE_DOUBLE
    UNITY_OUTPUT_COLOR
)
target_compile_options(helix_xml_test_unity PRIVATE -w)

# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------
add_library(helix_xml_test_helpers STATIC ${HELIX_TEST_DIR}/helpers/helix_test_env.c)
target_include_directories(helix_xml_test_helpers PUBLIC ${HELIX_TEST_DIR})
target_link_libraries(helix_xml_test_helpers PUBLIC helix_xml helix_xml_test_unity)
target_compile_options(helix_xml_test_helpers PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# One executable per cases/*.c
# ---------------------------------------------------------------------------
file(GLOB HELIX_TEST_CASES CONFIGURE_DEPENDS ${HELIX_TEST_DIR}/cases/*.c)
if(NOT HELIX_TEST_CASES)
    message(WARNING "No test cases found under ${HELIX_TEST_DIR}/cases")
endif()

foreach(case_file ${HELIX_TEST_CASES})
    get_filename_component(case_name ${case_file} NAME_WE)
    add_executable(${case_name} ${case_file})
    target_link_libraries(${case_name} PRIVATE helix_xml_test_helpers)
    target_compile_options(${case_name} PRIVATE -Wall -Wextra)
    target_compile_definitions(${case_name} PRIVATE
        HELIX_TEST_ASSET_DIR="${HELIX_TEST_DIR}/assets"
    )
    add_test(NAME ${case_name} COMMAND ${case_name})
    # Tests must not depend on the shell's cwd; anything on disk is reached
    # through HELIX_TEST_ASSET_DIR.
    set_tests_properties(${case_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT 120
    )
endforeach()

list(LENGTH HELIX_TEST_CASES helix_case_count)
message(STATUS "helix-xml tests: ${helix_case_count} test executable(s) from cases/")
