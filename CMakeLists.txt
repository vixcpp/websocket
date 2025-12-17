# ====================================================================
# Vix.cpp — WebSocket Module
# ====================================================================
# Purpose:
#   Build configuration for the Vix WebSocket module:
#   - Upgrades HTTP connections from vix::core to WebSocket
#   - Provides real-time streaming primitives (channels, broadcasts,
#     chat-style messaging, notifications, dashboards, IoT streams, ...)
#
# Public Targets:
#   - vix_websocket   : The actual library target (STATIC or INTERFACE)
#   - vix::websocket  : Namespaced alias for consumers
#
# Public Dependencies:
#   - vix::core   (HTTP server, routing, config)
#   - vix::utils  (logging, small helpers)
#   - optional JSON backend (vix::json / vix_json)
#
# Options:
#   - VIX_WEBSOCKET_WITH_JSON  : AUTO|ON|OFF (default: AUTO)
#   - VIX_WEBSOCKET_FETCH_CORE : Auto-fetch vix::core if missing (default ON)
#   - VIX_WEBSOCKET_FETCH_UTILS: Auto-fetch vix::utils if missing (default ON)
#
# Installation/Export:
#   - Contributes to the umbrella export-set `VixTargets`
#   - Headers installed under <prefix>/include/vix/websocket/...
# ====================================================================

cmake_minimum_required(VERSION 3.20)
project(vix_websocket VERSION 1.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

# ------------------------ Global settings ----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ------------------------ Sources discovery --------------------------
# If any .cpp exists under src/, we switch to STATIC build mode.
file(GLOB_RECURSE WEBSOCKET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# ------------------------ Core dependency ----------------------------
# We need vix::core (which itself may bring vix::utils, Boost, OpenSSL, ...).
option(VIX_WEBSOCKET_FETCH_CORE "Auto-fetch vix::core if missing" ON)
option(VIX_WEBSOCKET_FETCH_UTILS "Auto-fetch vix::utils if missing" ON)

if (NOT TARGET vix::core)
  if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../core/CMakeLists.txt")
    message(STATUS "[websocket] Adding core from umbrella: ../core")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../core" "core")
  elseif (VIX_WEBSOCKET_FETCH_CORE)
    include(FetchContent)
    message(STATUS "[websocket] Fetching vix::core via FetchContent")
    # ⚠️ Align this tag with your umbrella release
    FetchContent_Declare(vix_core
      GIT_REPOSITORY https://github.com/vixcpp/core.git
      GIT_TAG        v1.5.2
    )
    FetchContent_MakeAvailable(vix_core)
  else()
    message(FATAL_ERROR
      "vix::core not found. Provide vix::core before websocket "
      "or enable VIX_WEBSOCKET_FETCH_CORE=ON.")
  endif()
endif()

set(VIX_CORE_TARGET vix::core)
set(VIX_UTILS_TARGET vix::utils)

# ------------------------ JSON linkage policy ------------------------
# AUTO: link if a known JSON target exists, else define VIX_WEBSOCKET_NO_JSON
# ON  : require JSON target (error if absent)
# OFF : never link JSON, define VIX_WEBSOCKET_NO_JSON
set(VIX_WEBSOCKET_WITH_JSON "AUTO" CACHE STRING "Link JSON backend (AUTO|ON|OFF)")
set_property(CACHE VIX_WEBSOCKET_WITH_JSON PROPERTY STRINGS AUTO ON OFF)

function(vix_websocket_try_link_json onto link_scope)
  if (VIX_WEBSOCKET_WITH_JSON STREQUAL "OFF")
    message(STATUS "[websocket] JSON disabled (OFF)")
    target_compile_definitions(${onto} ${link_scope} VIX_WEBSOCKET_NO_JSON=1)
    return()
  endif()

  set(_json_candidates vix::json vix_json)
  set(_json_found "")
  foreach(t IN LISTS _json_candidates)
    if (TARGET ${t})
      set(_json_found ${t})
      break()
    endif()
  endforeach()

  if (_json_found)
    message(STATUS "[websocket] JSON backend: ${_json_found}")
    target_link_libraries(${onto} ${link_scope} ${_json_found})
  else()
    if (VIX_WEBSOCKET_WITH_JSON STREQUAL "ON")
      message(FATAL_ERROR
        "JSON required but not found (expected: vix::json or vix_json).")
    else()
      message(STATUS "[websocket] No JSON backend detected; building without JSON.")
      target_compile_definitions(${onto} ${link_scope} VIX_WEBSOCKET_NO_JSON=1)
    endif()
  endif()
endfunction()

# ============================== STATIC ===============================
if (WEBSOCKET_SOURCES)
  message(STATUS "[websocket] Building STATIC library with detected sources.")

  # Beast/WebSocket repose sur Boost.System pour les codes d'erreur
  find_package(Boost 1.78 REQUIRED COMPONENTS system)

  # ➕ NEW: SQLite
  find_package(SQLite3 REQUIRED)

  add_library(vix_websocket STATIC ${WEBSOCKET_SOURCES})
  add_library(vix::websocket ALIAS vix_websocket)
  add_library(vix::ws ALIAS vix_websocket)
  target_compile_features(vix_websocket PUBLIC cxx_std_20)

  # Inherit umbrella warnings/sanitizers if present
  if (TARGET vix_warnings)
    target_link_libraries(vix_websocket PUBLIC vix_warnings)
  endif()

  if (TARGET vix_sanitizers)
    target_link_libraries(vix_websocket PUBLIC vix_sanitizers)
  endif()

  target_include_directories(vix_websocket
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(vix_websocket
    PUBLIC
      ${VIX_CORE_TARGET}
      Boost::system
      SQLite::SQLite3
  )

  # Optional JSON linkage
  vix_websocket_try_link_json(vix_websocket PUBLIC)

  set_target_properties(vix_websocket PROPERTIES
    OUTPUT_NAME vix_websocket
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME websocket
  )

  install(TARGETS vix_websocket
    EXPORT VixTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# ============================ HEADER-ONLY ============================
else()
  message(STATUS "[websocket] Building HEADER-ONLY library (no sources).")

  add_library(vix_websocket INTERFACE)
  # Canonical target
  add_library(vix::websocket ALIAS vix_websocket)
  # Short alias (ergonomic)
  add_library(vix::ws ALIAS vix_websocket)
  target_compile_features(vix_websocket INTERFACE cxx_std_20)

  # Inherit umbrella warnings/sanitizers if present
  if (TARGET vix_warnings)
    target_link_libraries(vix_websocket INTERFACE vix_warnings)
  endif()

  if (TARGET vix_sanitizers)
    target_link_libraries(vix_websocket INTERFACE vix_sanitizers)
  endif()

  target_include_directories(vix_websocket INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(vix_websocket INTERFACE
    ${VIX_CORE_TARGET}
    ${VIX_UTILS_TARGET}
  )

  vix_websocket_try_link_json(vix_websocket INTERFACE)

  install(TARGETS vix_websocket
    EXPORT VixTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
endif()

# ------------------------ Examples (optional) ------------------------
option(VIX_WEBSOCKET_BUILD_EXAMPLES
       "Build WebSocket examples (server + HTML chat demo)" ON)

if (VIX_WEBSOCKET_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ----------------------------- Summary -------------------------------
message(STATUS "------------------------------------------------------")
message(STATUS "vix::websocket configured (${PROJECT_VERSION})")
if (WEBSOCKET_SOURCES)
  message(STATUS "Mode: STATIC / sources found")
else()
  message(STATUS "Mode: HEADER-ONLY / no sources")
endif()
message(STATUS "Core target:          ${VIX_CORE_TARGET}")
message(STATUS "Utils target:         ${VIX_UTILS_TARGET}")
message(STATUS "JSON policy:          ${VIX_WEBSOCKET_WITH_JSON}")
message(STATUS "Include dir:          ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "Fetch core enabled:   ${VIX_WEBSOCKET_FETCH_CORE}")
message(STATUS "Fetch utils enabled:  ${VIX_WEBSOCKET_FETCH_UTILS}")
message(STATUS "------------------------------------------------------")
