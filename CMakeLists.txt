#  @file CMakeLists.cpp
#  @author Gaspard Kirira
#
#  Copyright 2025, Gaspard Kirira.  All rights reserved.
#  https://github.com/vixcpp/vix
#  Use of this source code is governed by a MIT license
#  that can be found in the License file.
#
# ====================================================================
# Vix.cpp — Middleware Module (Core HTTP only)
# ====================================================================
# Required:
#   - vix::core
#   - vix::cache
#
# Optional:
#   - vix::utils (AUTO|ON|OFF)
#   - JSON backend (AUTO|ON|OFF): vix::json or vix_json
# ====================================================================

cmake_minimum_required(VERSION 3.20)
project(vix_middleware VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_DISABLE_FIND_PACKAGE_VixOrm ON)

include(GNUInstallDirs)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sources discovery
file(GLOB_RECURSE MIDDLEWARE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

option(VIX_MW_FETCH_CORE "Auto-fetch vix::core if missing" ON)

if (NOT TARGET vix::core)
  if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../core/CMakeLists.txt")
    message(STATUS "[middleware] Adding core from umbrella: ../core")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../core" "core")
  elseif (VIX_MW_FETCH_CORE)
    include(FetchContent)
    message(STATUS "[middleware] Fetching vix::core via FetchContent")
    FetchContent_Declare(vix_core
      GIT_REPOSITORY https://github.com/vixcpp/core.git
      GIT_TAG        dev
    )
    FetchContent_MakeAvailable(vix_core)
  else()
    message(FATAL_ERROR "vix::core not found. Provide it or enable VIX_MW_FETCH_CORE=ON.")
  endif()
endif()

set(VIX_CORE_TARGET vix::core)

# Required: Cache
option(VIX_MW_FETCH_CACHE "Auto-fetch vix::cache if missing" ON)

if (NOT TARGET vix::cache)
  if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../cache/CMakeLists.txt")
    message(STATUS "[middleware] Adding cache from umbrella: ../cache")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../cache" "cache")
  elseif (VIX_MW_FETCH_CACHE)
    include(FetchContent)
    message(STATUS "[middleware] Fetching vix::cache via FetchContent")
    FetchContent_Declare(vix_cache
      GIT_REPOSITORY https://github.com/vixcpp/cache.git
      GIT_TAG        v0.1.0
    )
    FetchContent_MakeAvailable(vix_cache)
  else()
    message(FATAL_ERROR "vix::cache not found. Provide it or enable VIX_MW_FETCH_CACHE=ON.")
  endif()
endif()

set(VIX_CACHE_TARGET vix::cache)

# Optional: Utils
set(VIX_MW_WITH_UTILS "AUTO" CACHE STRING "Link utils module (AUTO|ON|OFF)")
set_property(CACHE VIX_MW_WITH_UTILS PROPERTY STRINGS AUTO ON OFF)
option(VIX_MW_FETCH_UTILS "Auto-fetch vix::utils if missing (when needed)" ON)

function(vix_mw_try_link_utils onto link_scope)
  if (VIX_MW_WITH_UTILS STREQUAL "OFF")
    message(STATUS "[middleware] Utils disabled (OFF)")
    target_compile_definitions(${onto} ${link_scope} VIX_MW_NO_UTILS=1)
    return()
  endif()

  if (NOT TARGET vix::utils)
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../utils/CMakeLists.txt")
      message(STATUS "[middleware] Adding utils from umbrella: ../utils")
      add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../utils" "utils")
    elseif (VIX_MW_FETCH_UTILS)
      include(FetchContent)
      message(STATUS "[middleware] Fetching vix::utils via FetchContent")
      FetchContent_Declare(vix_utils
        GIT_REPOSITORY https://github.com/vixcpp/utils.git
        GIT_TAG        dev
      )
      FetchContent_MakeAvailable(vix_utils)
    endif()
  endif()

  if (TARGET vix::utils)
    message(STATUS "[middleware] Utils backend: vix::utils")
    target_link_libraries(${onto} ${link_scope} vix::utils)
    target_compile_definitions(${onto} ${link_scope} VIX_MW_WITH_UTILS=1)
  else()
    if (VIX_MW_WITH_UTILS STREQUAL "ON")
      message(FATAL_ERROR "Utils required but vix::utils not found.")
    else()
      message(STATUS "[middleware] Utils not detected; building without utils.")
      target_compile_definitions(${onto} ${link_scope} VIX_MW_NO_UTILS=1)
    endif()
  endif()
endfunction()

# Optional: JSON backend
set(VIX_MW_WITH_JSON "AUTO" CACHE STRING "Link JSON backend (AUTO|ON|OFF)")
set_property(CACHE VIX_MW_WITH_JSON PROPERTY STRINGS AUTO ON OFF)

function(vix_mw_try_link_json onto link_scope)
  if (VIX_MW_WITH_JSON STREQUAL "OFF")
    message(STATUS "[middleware] JSON disabled (OFF)")
    target_compile_definitions(${onto} ${link_scope} VIX_MW_NO_JSON=1)
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
    message(STATUS "[middleware] JSON backend: ${_json_found}")
    target_link_libraries(${onto} ${link_scope} ${_json_found})
  else()
    if (VIX_MW_WITH_JSON STREQUAL "ON")
      message(FATAL_ERROR "JSON required but not found (expected: vix::json or vix_json).")
    else()
      message(STATUS "[middleware] No JSON backend detected; building without JSON.")
      target_compile_definitions(${onto} ${link_scope} VIX_MW_NO_JSON=1)
    endif()
  endif()
endfunction()

# STATIC
if (MIDDLEWARE_SOURCES)
  message(STATUS "[middleware] Building STATIC library with detected sources.")

  add_library(vix_middleware STATIC ${MIDDLEWARE_SOURCES})

  if (TARGET vix_warnings)
    target_link_libraries(vix_middleware PUBLIC vix_warnings)
  endif()

  add_library(vix::middleware ALIAS vix_middleware)
  add_library(vix::mw         ALIAS vix_middleware)
  target_compile_features(vix_middleware PUBLIC cxx_std_20)

  target_include_directories(vix_middleware
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(vix_middleware
    PUBLIC
      ${VIX_CORE_TARGET}
      ${VIX_CACHE_TARGET}
  )

  vix_mw_try_link_utils(vix_middleware PUBLIC)
  vix_mw_try_link_json(vix_middleware PUBLIC)

  if (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
    target_link_libraries(vix_middleware PUBLIC vix_sanitizers)
  endif()

  set_target_properties(vix_middleware PROPERTIES
    OUTPUT_NAME vix_middleware
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME middleware
  )

  install(TARGETS vix_middleware
    EXPORT VixTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# HEADER-ONLY
else()
  message(STATUS "[middleware] Building HEADER-ONLY library (no sources).")

  add_library(vix_middleware INTERFACE)

  if (TARGET vix_warnings)
    target_link_libraries(vix_middleware INTERFACE vix_warnings)
  endif()

  add_library(vix::middleware ALIAS vix_middleware)
  add_library(vix::mw         ALIAS vix_middleware)
  target_compile_features(vix_middleware INTERFACE cxx_std_20)

  target_include_directories(vix_middleware INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(vix_middleware INTERFACE
    ${VIX_CORE_TARGET}
    ${VIX_CACHE_TARGET}
  )

  vix_mw_try_link_utils(vix_middleware INTERFACE)
  vix_mw_try_link_json(vix_middleware INTERFACE)

  if (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
    target_link_libraries(vix_middleware INTERFACE vix_sanitizers)
  endif()

  install(TARGETS vix_middleware
    EXPORT VixTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
endif()

# Tests
option(VIX_MW_BUILD_TESTS "Build middleware tests" OFF)

if (VIX_MW_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

# Summary
message(STATUS "------------------------------------------------------")
message(STATUS "vix::middleware configured (${PROJECT_VERSION})")
if (MIDDLEWARE_SOURCES)
  message(STATUS "Mode: STATIC / sources found")
else()
  message(STATUS "Mode: HEADER-ONLY / no sources")
endif()
message(STATUS "Core target:        ${VIX_CORE_TARGET}")
message(STATUS "Cache target:       ${VIX_CACHE_TARGET}")
message(STATUS "Utils policy:       ${VIX_MW_WITH_UTILS}")
message(STATUS "JSON policy:        ${VIX_MW_WITH_JSON}")
message(STATUS "Include dir:        ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "Sanitizers enabled: ${VIX_ENABLE_SANITIZERS}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "------------------------------------------------------")
