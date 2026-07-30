cmake_minimum_required(VERSION 3.20)

include(CTest)

if (BUILD_TESTING)
  enable_testing()
endif()

set(VIX_WEBSOCKET_TEST_TARGET vix::websocket)

if (NOT TARGET ${VIX_WEBSOCKET_TEST_TARGET} AND TARGET vix_websocket)
  set(VIX_WEBSOCKET_TEST_TARGET vix_websocket)
endif()

if (NOT TARGET ${VIX_WEBSOCKET_TEST_TARGET} AND TARGET websocket)
  set(VIX_WEBSOCKET_TEST_TARGET websocket)
endif()

if (NOT TARGET ${VIX_WEBSOCKET_TEST_TARGET})
  message(
    FATAL_ERROR
    "[websocket/tests] Missing WebSocket target "
    "(expected vix::websocket, vix_websocket or websocket)."
  )
endif()

function(vix_websocket_add_test test_name)
  set(test_sources)

  foreach(test_source IN LISTS ARGN)
    if (IS_ABSOLUTE "${test_source}")
      set(test_source_path "${test_source}")
    else()
      set(test_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
    endif()

    if (EXISTS "${test_source_path}")
      file(SIZE "${test_source_path}" test_source_size)
      if (test_source_size EQUAL 0)
        message(STATUS "[websocket/tests] Skipping placeholder test ${test_name}: ${test_source}")
        return()
      endif()
    endif()

    list(APPEND test_sources "${test_source}")
  endforeach()

  add_executable(${test_name}
    ${test_sources}
  )

  target_link_libraries(${test_name} PRIVATE
    ${VIX_WEBSOCKET_TEST_TARGET}
  )

  target_compile_features(${test_name} PRIVATE cxx_std_20)

  if (BUILD_TESTING)
    add_test(
      NAME ${test_name}
      COMMAND ${test_name}
    )
  endif()
endfunction()

function(vix_websocket_add_network_test test_name server_port)
  vix_websocket_add_test(${test_name}
    ${ARGN}
  )

  if (BUILD_TESTING AND TARGET ${test_name})
    set_tests_properties(${test_name} PROPERTIES
      TIMEOUT 20
      RUN_SERIAL TRUE
      ENVIRONMENT
        "SERVER_PORT=${server_port};VIX_WEBSOCKET_TEST_PORT=${server_port}"
    )
  endif()
endfunction()

# -----------------------------------------------------------------------------
# Smoke and common behavior
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_smoke_test
  websocket_smoke_test.cpp
)

vix_websocket_add_test(websocket_disconnect_tests
  websocket_disconnect_tests.cpp
)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_config_defaults_test
  config/config_defaults_test.cpp
)

vix_websocket_add_test(websocket_config_copy_move_test
  config/config_copy_move_test.cpp
)

vix_websocket_add_test(websocket_config_from_core_test
  config/config_from_core_test.cpp
)

# -----------------------------------------------------------------------------
# Protocol
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_protocol_opcode_test
  protocol/protocol_opcode_test.cpp
)

vix_websocket_add_test(websocket_protocol_base64_test
  protocol/protocol_base64_test.cpp
)

vix_websocket_add_test(websocket_protocol_sha1_test
  protocol/protocol_sha1_test.cpp
)

vix_websocket_add_test(websocket_protocol_accept_key_test
  protocol/protocol_accept_key_test.cpp
)

vix_websocket_add_test(websocket_protocol_client_key_test
  protocol/protocol_client_key_test.cpp
)

vix_websocket_add_test(websocket_protocol_header_helpers_test
  protocol/protocol_header_helpers_test.cpp
)

vix_websocket_add_test(websocket_protocol_frame_build_test
  protocol/protocol_frame_build_test.cpp
)

vix_websocket_add_test(websocket_protocol_frame_header_test
  protocol/protocol_frame_header_test.cpp
)

vix_websocket_add_test(websocket_protocol_frame_decode_test
  protocol/protocol_frame_decode_test.cpp
)

vix_websocket_add_test(websocket_protocol_frame_roundtrip_test
  protocol/protocol_frame_roundtrip_test.cpp
)

vix_websocket_add_test(websocket_protocol_control_frame_test
  protocol/protocol_control_frame_test.cpp
)

vix_websocket_add_test(websocket_protocol_close_frame_test
  protocol/protocol_close_frame_test.cpp
)

vix_websocket_add_test(websocket_protocol_json_message_test
  protocol/protocol_json_message_test.cpp
)

# -----------------------------------------------------------------------------
# Router
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_router_constructor_test
  router/router_constructor_test.cpp
)

vix_websocket_add_test(websocket_router_handlers_test
  router/router_handlers_test.cpp
)

vix_websocket_add_test(websocket_router_dispatch_test
  router/router_dispatch_test.cpp
)

vix_websocket_add_test(websocket_router_error_test
  router/router_error_test.cpp
)

# -----------------------------------------------------------------------------
# Long polling
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_long_polling_session_test
  long_polling/long_polling_session_test.cpp
)

vix_websocket_add_test(websocket_long_polling_session_limits_test
  long_polling/long_polling_session_limits_test.cpp
)

vix_websocket_add_test(websocket_long_polling_session_expiration_test
  long_polling/long_polling_session_expiration_test.cpp
)

vix_websocket_add_test(websocket_long_polling_manager_test
  long_polling/long_polling_manager_test.cpp
)

vix_websocket_add_test(websocket_long_polling_manager_limits_test
  long_polling/long_polling_manager_limits_test.cpp
)

vix_websocket_add_test(websocket_long_polling_manager_sweep_test
  long_polling/long_polling_manager_sweep_test.cpp
)

vix_websocket_add_test(websocket_long_polling_manager_metrics_test
  long_polling/long_polling_manager_metrics_test.cpp
)

vix_websocket_add_test(websocket_long_polling_bridge_test
  long_polling/long_polling_bridge_test.cpp
)

vix_websocket_add_test(websocket_long_polling_bridge_forwarding_test
  long_polling/long_polling_bridge_forwarding_test.cpp
)

# -----------------------------------------------------------------------------
# HTTP API
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_http_api_constructor_test
  http/http_api_constructor_test.cpp
)

vix_websocket_add_test(websocket_http_api_query_test
  http/http_api_query_test.cpp
)

vix_websocket_add_test(websocket_http_api_poll_test
  http/http_api_poll_test.cpp
)

vix_websocket_add_test(websocket_http_api_send_test
  http/http_api_send_test.cpp
)

vix_websocket_add_test(websocket_http_api_registration_test
  http/http_api_registration_test.cpp
)

# -----------------------------------------------------------------------------
# Metrics
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_metrics_defaults_test
  metrics/metrics_defaults_test.cpp
)

vix_websocket_add_test(websocket_metrics_counters_test
  metrics/metrics_counters_test.cpp
)

vix_websocket_add_test(websocket_metrics_prometheus_test
  metrics/metrics_prometheus_test.cpp
)

vix_websocket_add_network_test(
  websocket_metrics_server_lifecycle_test
  19200
  metrics/metrics_server_lifecycle_test.cpp
)

vix_websocket_add_network_test(
  websocket_metrics_server_request_test
  19201
  metrics/metrics_server_request_test.cpp
)

# -----------------------------------------------------------------------------
# Message storage
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_message_store_test
  storage/message_store_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_constructor_test
  storage/sqlite_message_store_constructor_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_append_test
  storage/sqlite_message_store_append_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_room_test
  storage/sqlite_message_store_room_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_replay_test
  storage/sqlite_message_store_replay_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_ordering_test
  storage/sqlite_message_store_ordering_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_persistence_test
  storage/sqlite_message_store_persistence_test.cpp
)

vix_websocket_add_test(websocket_sqlite_message_store_concurrency_test
  storage/sqlite_message_store_concurrency_test.cpp
)

# -----------------------------------------------------------------------------
# WebSocket session
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_session_type_traits_test
  session/session_type_traits_test.cpp
)

vix_websocket_add_test(websocket_session_constructor_test
  session/session_constructor_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_handshake_test
  19210
  session/session_handshake_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_handshake_validation_test
  19211
  session/session_handshake_validation_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_text_frame_test
  19212
  session/session_text_frame_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_binary_frame_test
  19213
  session/session_binary_frame_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_ping_pong_test
  19214
  session/session_ping_pong_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_close_test
  19215
  session/session_close_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_error_test
  19216
  session/session_error_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_send_queue_test
  19217
  session/session_send_queue_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_backpressure_test
  19218
  session/session_backpressure_test.cpp
)

vix_websocket_add_network_test(
  websocket_session_disconnect_test
  19219
  session/session_disconnect_test.cpp
)

# -----------------------------------------------------------------------------
# Low-level server
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_low_level_server_constructor_test
  server/low_level_server_constructor_test.cpp
)

vix_websocket_add_test(websocket_low_level_server_run_validation_test
  server/low_level_server_run_validation_test.cpp
)

vix_websocket_add_network_test(
  websocket_low_level_server_lifecycle_test
  19220
  server/low_level_server_lifecycle_test.cpp
)

vix_websocket_add_network_test(
  websocket_low_level_server_accept_test
  19221
  server/low_level_server_accept_test.cpp
)

vix_websocket_add_network_test(
  websocket_low_level_server_shutdown_test
  19222
  server/low_level_server_shutdown_test.cpp
)

# -----------------------------------------------------------------------------
# Public WebSocket server
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_server_constructor_test
  server/server_constructor_test.cpp
)

vix_websocket_add_test(websocket_server_handlers_test
  server/server_handlers_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_lifecycle_test
  19230
  server/server_lifecycle_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_sessions_test
  19231
  server/server_sessions_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_broadcast_test
  19232
  server/server_broadcast_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_rooms_test
  19233
  server/server_rooms_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_typed_messages_test
  19234
  server/server_typed_messages_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_long_polling_test
  19235
  server/server_long_polling_test.cpp
)

vix_websocket_add_network_test(
  websocket_server_shutdown_test
  19236
  server/server_shutdown_test.cpp
)

# -----------------------------------------------------------------------------
# Client
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_client_type_traits_test
  client/client_type_traits_test.cpp
)

vix_websocket_add_test(websocket_client_constructor_test
  client/client_constructor_test.cpp
)

vix_websocket_add_test(websocket_client_connect_validation_test
  client/client_connect_validation_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_handshake_test
  19240
  client/client_handshake_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_text_message_test
  19241
  client/client_text_message_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_typed_message_test
  19242
  client/client_typed_message_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_ping_pong_test
  19243
  client/client_ping_pong_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_close_test
  19244
  client/client_close_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_reconnect_test
  19245
  client/client_reconnect_test.cpp
)

vix_websocket_add_network_test(
  websocket_client_error_test
  19246
  client/client_error_test.cpp
)

# -----------------------------------------------------------------------------
# WebSocket App
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_app_constructor_test
  app/app_constructor_test.cpp
)

vix_websocket_add_test(websocket_app_config_test
  app/app_config_test.cpp
)

vix_websocket_add_test(websocket_app_routes_test
  app/app_routes_test.cpp
)

vix_websocket_add_test(websocket_app_handlers_test
  app/app_handlers_test.cpp
)

vix_websocket_add_network_test(
  websocket_app_lifecycle_test
  19250
  app/app_lifecycle_test.cpp
)

vix_websocket_add_network_test(
  websocket_app_shutdown_test
  19251
  app/app_shutdown_test.cpp
)

# -----------------------------------------------------------------------------
# Attached runtime
# -----------------------------------------------------------------------------

vix_websocket_add_test(websocket_attached_runtime_constructor_test
  runtime/attached_runtime_constructor_test.cpp
)

vix_websocket_add_network_test(
  websocket_attached_runtime_lifecycle_test
  19260
  runtime/attached_runtime_lifecycle_test.cpp
)

vix_websocket_add_network_test(
  websocket_attached_runtime_stop_test
  19261
  runtime/attached_runtime_stop_test.cpp
)

vix_websocket_add_test(websocket_attached_runtime_executor_test
  runtime/attached_runtime_executor_test.cpp
)

vix_websocket_add_network_test(
  websocket_attached_runtime_http_websocket_test
  19262
  runtime/attached_runtime_http_websocket_test.cpp
)

# -----------------------------------------------------------------------------
# Integration
# -----------------------------------------------------------------------------

vix_websocket_add_network_test(
  websocket_integration_client_server_text_test
  19300
  integration/client_server_text_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_client_server_json_test
  19301
  integration/client_server_json_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_client_server_ping_pong_test
  19302
  integration/client_server_ping_pong_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_client_server_close_test
  19303
  integration/client_server_close_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_broadcast_test
  19304
  integration/server_broadcast_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_room_broadcast_test
  19305
  integration/server_room_broadcast_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_long_polling_bridge_test
  19306
  integration/server_long_polling_bridge_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_message_store_test
  19307
  integration/server_message_store_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_reconnect_test
  19308
  integration/server_reconnect_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_concurrent_clients_test
  19309
  integration/server_concurrent_clients_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_rapid_disconnect_test
  19310
  integration/server_rapid_disconnect_test.cpp
)

vix_websocket_add_network_test(
  websocket_integration_server_shutdown_test
  19311
  integration/server_shutdown_test.cpp
)
