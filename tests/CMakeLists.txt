cmake_minimum_required(VERSION 3.20)

include(CTest)

if (BUILD_TESTING)
  enable_testing()
endif()

set(VIX_WEBSOCKET_TEST_TARGET vix::websocket)

if (NOT TARGET ${VIX_WEBSOCKET_TEST_TARGET} AND TARGET vix_websocket)
  set(VIX_WEBSOCKET_TEST_TARGET vix_websocket)
endif()

if (NOT TARGET ${VIX_WEBSOCKET_TEST_TARGET})
  message(FATAL_ERROR "[websocket/tests] Missing websocket target (expected vix::websocket or vix_websocket).")
endif()

add_executable(websocket_disconnect_tests
  websocket_disconnect_tests.cpp
)

target_compile_features(websocket_disconnect_tests
  PRIVATE
    cxx_std_20
)

target_link_libraries(websocket_disconnect_tests
  PRIVATE
    ${VIX_WEBSOCKET_TEST_TARGET}
)

if (VIX_ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(websocket_disconnect_tests
    PRIVATE
      -fno-omit-frame-pointer
      -fsanitize=address,undefined
  )

  target_link_options(websocket_disconnect_tests
    PRIVATE
      -fsanitize=address,undefined
  )
endif()

if (BUILD_TESTING)
  add_test(
    NAME websocket_disconnect_tests
    COMMAND websocket_disconnect_tests
  )
endif()
