cmake_minimum_required(VERSION 3.20)
project(lvgl_html5_canvas C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(LHC_BUILD_TESTS "Build unit tests"  ON)
option(LHC_BUILD_APP   "Build main app"    ON)

# -----------------------------------------------------------------------------
# Dependencies via FetchContent (no submodules).
# -----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- LVGL 9.5 ---
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/src/lv_conf.h" CACHE FILEPATH "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES   ON  CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS      OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS            ON  CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG        release/v9.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)

# Strip ARM Cortex-M Helium (MVE) assembly from the lvgl target — it's picked
# up by file(GLOB_RECURSE src/*.S) in LVGL's CMake but cannot be assembled on
# host aarch64/x86_64 clang. Safe because LV_USE_NATIVE_HELIUM_ASM=0 means
# no C code references its symbols.
if(TARGET lvgl)
  get_target_property(_lvgl_srcs lvgl SOURCES)
  if(_lvgl_srcs)
    list(FILTER _lvgl_srcs EXCLUDE REGEX "lv_blend_helium\\.S$")
    set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")
  endif()
endif()

# --- libwebsockets ---
set(LWS_WITH_SSL              OFF CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TESTAPPS      ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_SERVER   ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_PING     ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT   ON  CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED           OFF CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC           ON  CACHE BOOL "" FORCE)
set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "" FORCE)
# Avoid pulling system-level deps so we stay cross-compile-friendly
# (target: Allwinner T507 / Timesys Linux). Anything that needs an
# external .so is explicitly disabled here; if a feature is required
# later, vendor it via FetchContent rather than the system package.
set(LWS_WITH_LIBCAP           OFF CACHE BOOL "" FORCE)  # avoid libcap
# LWS unconditionally runs CHECK_LIBRARY_EXISTS(cap cap_set_flag ...) which
# spuriously succeeds on Termux/musl (symbol is in libc) and then fails at
# link time because there's no libcap.so. Force the detection result off so
# the find_library(LIBCAP_LIBRARIES) branch is skipped entirely.
set(LWS_HAVE_LIBCAP           OFF CACHE INTERNAL "" FORCE)
set(LWS_WITH_LIBUV            OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LIBEV            OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LIBEVENT         OFF CACHE BOOL "" FORCE)
set(LWS_WITH_GLIB             OFF CACHE BOOL "" FORCE)
set(LWS_WITH_ZLIB              OFF CACHE BOOL "" FORCE)
set(LWS_WITH_BUNDLED_ZLIB     OFF CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS    ON  CACHE BOOL "" FORCE)
set(LWS_WITH_PLUGINS          OFF CACHE BOOL "" FORCE)
set(LWS_WITH_HTTP2            OFF CACHE BOOL "" FORCE)
set(LWS_WITH_CGI              OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LWSWS            OFF CACHE BOOL "" FORCE)
set(LWS_ROLE_RAW_PROXY        OFF CACHE BOOL "" FORCE)
set(LWS_ROLE_DBUS             OFF CACHE BOOL "" FORCE)
set(LWS_ROLE_MQTT             OFF CACHE BOOL "" FORCE)
# libwebsockets 4.3.x predates CMake's 3.5-minimum requirement.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_Declare(
  libwebsockets
  GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
  GIT_TAG        v4.3.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libwebsockets)

# LWS turns on -Werror unconditionally; release/master tip has a -Wunused-label
# in client-http.c that breaks the build on Termux clang. Strip -Werror on the
# websockets target so we don't have to vendor a patch.
if(TARGET websockets)
  target_compile_options(websockets PRIVATE -Wno-error -Wno-unused-label)
endif()

# -----------------------------------------------------------------------------
# Protocol JS generation (single source of truth: src/proto/protocol.h)
# -----------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PROTOCOL_H ${CMAKE_SOURCE_DIR}/src/proto/protocol.h)
set(PROTOCOL_JS ${CMAKE_SOURCE_DIR}/web/protocol.js)
set(GEN_JS_SCRIPT ${CMAKE_SOURCE_DIR}/src/proto/gen_js_protocol.py)

add_custom_command(
  OUTPUT  ${PROTOCOL_JS}
  COMMAND ${Python3_EXECUTABLE} ${GEN_JS_SCRIPT}
          --header ${PROTOCOL_H} --out ${PROTOCOL_JS}
  DEPENDS ${PROTOCOL_H} ${GEN_JS_SCRIPT}
  COMMENT "Generating web/protocol.js from protocol.h"
  VERBATIM
)
add_custom_target(gen_protocol_js ALL DEPENDS ${PROTOCOL_JS})

# -----------------------------------------------------------------------------
# Encoder library (no LVGL dep — used by tests too)
# -----------------------------------------------------------------------------
add_library(lhc_proto STATIC
  src/proto/encoder.c
)
target_include_directories(lhc_proto PUBLIC src/proto)

# -----------------------------------------------------------------------------
# Main app
# -----------------------------------------------------------------------------
if(LHC_BUILD_APP)
  add_executable(lvgl_html5_canvas
    src/main.c
    src/draw/html5_draw_unit.c
    src/transport/ws_server.c
    src/assets/demo_sprite.c
  )
  target_include_directories(lvgl_html5_canvas PRIVATE
    src
    ${lvgl_SOURCE_DIR}
  )
  target_link_libraries(lvgl_html5_canvas PRIVATE
    lhc_proto
    lvgl
    lvgl_demos
    websockets
  )
  add_dependencies(lvgl_html5_canvas gen_protocol_js)
  if(UNIX AND NOT APPLE)
    target_link_libraries(lvgl_html5_canvas PRIVATE pthread m)
  endif()
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(LHC_BUILD_TESTS)
  enable_testing()
  add_executable(test_encoder tests/test_encoder.c)
  target_link_libraries(test_encoder PRIVATE lhc_proto)
  add_test(NAME test_encoder COMMAND test_encoder)

  add_test(
    NAME protocol_js_up_to_date
    COMMAND ${Python3_EXECUTABLE} ${GEN_JS_SCRIPT}
            --header ${PROTOCOL_H} --out ${PROTOCOL_JS} --check
  )

  if(LHC_BUILD_APP)
    add_test(
      NAME protocol_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_protocol_e2e.py
    )
    set_tests_properties(protocol_e2e PROPERTIES
      ENVIRONMENT "LHC_BIN=$<TARGET_FILE:lvgl_html5_canvas>"
      TIMEOUT 30
    )
  endif()
endif()
