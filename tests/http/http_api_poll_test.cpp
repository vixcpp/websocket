/**
 *
 * @file http_api_poll_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/runtime/Budget.hpp>
#include <vix/runtime/Runtime.hpp>
#include <vix/websocket/HttpApi.hpp>
#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/server.hpp>

namespace
{
  namespace http =
      vix::websocket::http;

  using Config =
      vix::config::Config;

  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingBridge =
      vix::websocket::LongPollingBridge;

  using LongPollingManager =
      vix::websocket::LongPollingManager;

  using RuntimeConfig =
      vix::runtime::RuntimeConfig;

  using RuntimeExecutor =
      vix::executor::RuntimeExecutor;

  using Server =
      vix::websocket::Server;

  struct TargetRequest
  {
    std::string targetValue{"/ws/poll"};

    [[nodiscard]]
    std::string_view target() const noexcept
    {
      return std::string_view{
          targetValue.data(),
          targetValue.size()};
    }
  };

  struct QueryValueRequest
  {
    std::unordered_map<
        std::string,
        std::string>
        values{};

    [[nodiscard]]
    std::string query_value(
        std::string_view key) const
    {
      const auto it =
          values.find(
              std::string{key});

      if (it == values.end())
      {
        return {};
      }

      return it->second;
    }
  };

  struct ResponseStub
  {
    ResponseStub &status(
        int value) noexcept
    {
      statusCode = value;
      return *this;
    }

    ResponseStub &json(
        nlohmann::json value)
    {
      body =
          std::move(value);

      return *this;
    }

    int statusCode{-1};

    std::optional<nlohmann::json>
        body{};
  };

  static void set_env_var(
      const char *name,
      const std::string &value)
  {
#if defined(_WIN32)
    const std::string assignment =
        std::string{name} +
        "=" +
        value;

    const int result =
        _putenv(
            assignment.c_str());
#else
    const int result =
        setenv(
            name,
            value.c_str(),
            1);
#endif

    assert(result == 0);
  }

  static void prepare_test_environment()
  {
    set_env_var(
        "VIX_ENV_SILENT",
        "true");

    set_env_var(
        "VIX_INTERNAL_LOGS",
        "false");

    set_env_var(
        "VIX_ACCESS_LOGS",
        "false");

    set_env_var(
        "VIX_LOG_ASYNC",
        "false");

    set_env_var(
        "VIX_LOG_LEVEL",
        "critical");
  }

  static Config make_config()
  {
    prepare_test_environment();

    return Config{};
  }

  static std::shared_ptr<
      RuntimeExecutor>
  make_executor()
  {
    return std::make_shared<
        RuntimeExecutor>(
        RuntimeConfig{
            1u,
            vix::runtime::BudgetConfig{
                8u}});
  }

  struct ServerWithoutBridgeFixture
  {
    Config config{
        make_config()};

    std::shared_ptr<
        RuntimeExecutor>
        executor{
            make_executor()};

    Server server{
        config,
        executor};
  };

  struct ServerWithBridgeFixture
  {
    Config config{
        make_config()};

    std::shared_ptr<
        RuntimeExecutor>
        executor{
            make_executor()};

    LongPollingManager manager{};

    std::shared_ptr<
        LongPollingBridge>
        bridge{
            std::make_shared<
                LongPollingBridge>(
                manager)};

    Server server{
        config,
        executor};

    ServerWithBridgeFixture()
    {
      server.attach_long_polling_bridge(
          bridge);
    }
  };

  static JsonMessage make_message(
      std::size_t index,
      std::string session = {})
  {
    JsonMessage message;

    message.id =
        "message-" +
        std::to_string(index);

    message.kind = "event";

    message.ts =
        "2026-07-30T10:00:00Z";

    message.room =
        std::move(session);

    message.type =
        "event-" +
        std::to_string(index);

    return message;
  }

  static void push_messages(
      LongPollingManager &manager,
      const std::string &sessionId,
      std::size_t first,
      std::size_t last)
  {
    for (std::size_t index = first;
         index <= last;
         ++index)
    {
      manager.push_to(
          sessionId,
          make_message(index));
    }
  }

  static const nlohmann::json &
  response_json(
      const ResponseStub &response)
  {
    assert(response.body.has_value());

    return *response.body;
  }

  static void test_missing_bridge_returns_service_unavailable()
  {
    ServerWithoutBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 503);

    const auto &body =
        response_json(response);

    assert(body.is_object());

    assert(
        body["error"] ==
        "long-polling bridge not attached");
  }

  static void test_default_poll_uses_broadcast_session()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    const auto &body =
        response_json(response);

    assert(body.is_array());
    assert(body.empty());

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "broadcast") ==
        0u);
  }

  static void test_poll_uses_requested_session()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll?session_id=session-42"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).empty());

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "session-42") ==
        0u);

    assert(
        fixture.manager.buffer_size(
            "broadcast") ==
        0u);
  }

  static void test_empty_session_id_uses_broadcast()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll?session_id="};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "broadcast") ==
        0u);
  }

  static void test_session_id_is_url_decoded()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll?session_id=room%3Ageneral"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "room:general") ==
        0u);

    assert(
        fixture.manager.buffer_size(
            "room%3Ageneral") ==
        0u);
  }

  static void test_plus_in_session_id_becomes_space()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll?session_id=user+one"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "user one") ==
        0u);
  }

  static void test_duplicate_session_id_uses_first_value()
  {
    ServerWithBridgeFixture fixture;

    TargetRequest request{
        "/ws/poll?session_id=first&session_id=second"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        fixture.manager.session_count() ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "first") ==
        0u);
  }

  static void test_poll_drains_requested_number_of_messages()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        5u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=2"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    const auto &body =
        response_json(response);

    assert(body.is_array());
    assert(body.size() == 2u);

    assert(body[0]["id"] == "message-1");
    assert(body[0]["type"] == "event-1");

    assert(body[1]["id"] == "message-2");
    assert(body[1]["type"] == "event-2");

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        3u);
  }

  static void test_poll_preserves_fifo_order()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        3u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=3"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    const auto &body =
        response_json(response);

    assert(body.size() == 3u);

    assert(body[0]["id"] == "message-1");
    assert(body[1]["id"] == "message-2");
    assert(body[2]["id"] == "message-3");

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        0u);
  }

  static void test_default_max_is_fifty()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        60u);

    TargetRequest request{
        "/ws/poll?session_id=session-1"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    const auto &body =
        response_json(response);

    assert(response.statusCode == 200);
    assert(body.size() == 50u);

    assert(
        body.front()["id"] ==
        "message-1");

    assert(
        body.back()["id"] ==
        "message-50");

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        10u);
  }

  static void test_invalid_max_uses_default_value()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        60u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=invalid"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).size() ==
        50u);

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        10u);
  }

  static void test_empty_max_uses_default_value()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        60u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max="};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).size() ==
        50u);

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        10u);
  }

  static void test_out_of_range_max_uses_default_value()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        60u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=999999999999999999999999999999999"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).size() ==
        50u);

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        10u);
  }

  static void test_zero_max_does_not_drain_messages()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        3u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=0"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).empty());

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        3u);
  }

  static void test_max_larger_than_buffer_drains_all()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-1",
        1u,
        3u);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=100"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).size() ==
        3u);

    assert(
        fixture.manager.buffer_size(
            "session-1") ==
        0u);
  }

  static void test_poll_only_drains_requested_session()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "session-a",
        1u,
        3u);

    push_messages(
        fixture.manager,
        "session-b",
        10u,
        12u);

    TargetRequest request{
        "/ws/poll?session_id=session-a&max=2"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    assert(
        response_json(response).size() ==
        2u);

    assert(
        fixture.manager.buffer_size(
            "session-a") ==
        1u);

    assert(
        fixture.manager.buffer_size(
            "session-b") ==
        3u);
  }

  static void test_poll_serializes_complete_message()
  {
    ServerWithBridgeFixture fixture;

    JsonMessage message;

    message.id = "message-42";
    message.kind = "event";
    message.ts = "2026-07-30T10:00:00Z";
    message.room = "general";
    message.type = "chat.message";

    fixture.manager.push_to(
        "session-1",
        message);

    TargetRequest request{
        "/ws/poll?session_id=session-1&max=1"};

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    const auto &body =
        response_json(response);

    assert(response.statusCode == 200);
    assert(body.size() == 1u);

    assert(body[0]["id"] == "message-42");
    assert(body[0]["kind"] == "event");

    assert(
        body[0]["ts"] ==
        "2026-07-30T10:00:00Z");

    assert(body[0]["room"] == "general");
    assert(body[0]["type"] == "chat.message");

    assert(body[0].contains("payload"));
    assert(body[0]["payload"].is_object());
  }

  static void test_repeated_poll_keeps_session_alive()
  {
    ServerWithBridgeFixture fixture;

    fixture.manager.push_to(
        "session-1",
        make_message(1u));

    TargetRequest firstRequest{
        "/ws/poll?session_id=session-1&max=1"};

    ResponseStub firstResponse;

    http::handle_ws_poll(
        firstRequest,
        firstResponse,
        fixture.server);

    assert(
        response_json(
            firstResponse)
            .size() ==
        1u);

    TargetRequest secondRequest{
        "/ws/poll?session_id=session-1&max=1"};

    ResponseStub secondResponse;

    http::handle_ws_poll(
        secondRequest,
        secondResponse,
        fixture.server);

    assert(
        response_json(
            secondResponse)
            .empty());

    assert(
        fixture.manager.session_count() ==
        1u);
  }

  static void test_query_value_request_is_supported()
  {
    ServerWithBridgeFixture fixture;

    push_messages(
        fixture.manager,
        "direct-session",
        1u,
        4u);

    QueryValueRequest request;

    request.values.emplace(
        "session_id",
        "direct-session");

    request.values.emplace(
        "max",
        "2");

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    const auto &body =
        response_json(response);

    assert(body.size() == 2u);

    assert(body[0]["id"] == "message-1");
    assert(body[1]["id"] == "message-2");

    assert(
        fixture.manager.buffer_size(
            "direct-session") ==
        2u);
  }

  static void test_empty_query_value_session_uses_broadcast()
  {
    ServerWithBridgeFixture fixture;

    fixture.manager.push_to(
        "broadcast",
        make_message(1u));

    QueryValueRequest request;

    request.values.emplace(
        "session_id",
        "");

    request.values.emplace(
        "max",
        "1");

    ResponseStub response;

    http::handle_ws_poll(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 200);

    const auto &body =
        response_json(response);

    assert(body.size() == 1u);
    assert(body[0]["id"] == "message-1");

    assert(
        fixture.manager.buffer_size(
            "broadcast") ==
        0u);
  }

} // namespace

int main()
{
  test_missing_bridge_returns_service_unavailable();

  test_default_poll_uses_broadcast_session();
  test_poll_uses_requested_session();
  test_empty_session_id_uses_broadcast();

  test_session_id_is_url_decoded();
  test_plus_in_session_id_becomes_space();
  test_duplicate_session_id_uses_first_value();

  test_poll_drains_requested_number_of_messages();
  test_poll_preserves_fifo_order();

  test_default_max_is_fifty();
  test_invalid_max_uses_default_value();
  test_empty_max_uses_default_value();
  test_out_of_range_max_uses_default_value();

  test_zero_max_does_not_drain_messages();
  test_max_larger_than_buffer_drains_all();

  test_poll_only_drains_requested_session();
  test_poll_serializes_complete_message();
  test_repeated_poll_keeps_session_alive();

  test_query_value_request_is_supported();
  test_empty_query_value_session_uses_broadcast();

  return 0;
}
