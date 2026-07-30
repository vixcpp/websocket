/**
 *
 * @file http_api_send_test.cpp
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
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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

  struct RequestStub
  {
    std::string targetValue{"/ws/send"};
    std::string bodyValue{};

    [[nodiscard]]
    std::string_view target() const noexcept
    {
      return std::string_view{
          targetValue.data(),
          targetValue.size()};
    }

    [[nodiscard]]
    const std::string &body() const noexcept
    {
      return bodyValue;
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
      bodyValue =
          std::move(value);

      return *this;
    }

    int statusCode{-1};

    std::optional<nlohmann::json>
        bodyValue{};
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
        "VIX_DOCS",
        "false");

    set_env_var(
        "VIX_ACCESS_LOGS",
        "false");

    set_env_var(
        "VIX_INTERNAL_LOGS",
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

  static std::shared_ptr<RuntimeExecutor>
  make_executor()
  {
    return std::make_shared<RuntimeExecutor>(
        RuntimeConfig{
            1u,
            vix::runtime::BudgetConfig{
                8u}});
  }

  struct ServerWithoutBridgeFixture
  {
    Config config{
        make_config()};

    std::shared_ptr<RuntimeExecutor>
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

    std::shared_ptr<RuntimeExecutor>
        executor{
            make_executor()};

    LongPollingManager manager{};

    std::size_t forwardCalls{0u};
    JsonMessage forwardedMessage{};

    std::shared_ptr<LongPollingBridge>
        bridge{};

    Server server{
        config,
        executor};

    ServerWithBridgeFixture()
    {
      bridge =
          std::make_shared<LongPollingBridge>(
              manager,
              LongPollingBridge::Resolver{},
              [this](
                  const JsonMessage &message)
              {
                ++forwardCalls;
                forwardedMessage = message;
              });

      server.attach_long_polling_bridge(
          bridge);
    }
  };

  static const nlohmann::json &
  response_json(
      const ResponseStub &response)
  {
    assert(response.bodyValue.has_value());

    return *response.bodyValue;
  }

  static void assert_client_error(
      const ResponseStub &response)
  {
    assert(response.statusCode >= 400);
    assert(response.statusCode < 500);

    const auto &body =
        response_json(response);

    assert(body.is_object());
    assert(body.contains("error"));
    assert(body["error"].is_string());
  }

  static void assert_success(
      const ResponseStub &response)
  {
    assert(response.statusCode >= 200);
    assert(response.statusCode < 300);

    const auto &body =
        response_json(response);

    assert(body.is_object());
  }

  static RequestStub make_request(
      std::string body,
      std::string target = "/ws/send")
  {
    RequestStub request;

    request.targetValue =
        std::move(target);

    request.bodyValue =
        std::move(body);

    return request;
  }

  static void test_handler_signature()
  {
    using Handler =
        void (*)(
            RequestStub &,
            ResponseStub &,
            Server &);

    static_assert(
        std::is_same_v<
            decltype(&http::handle_ws_send<
                     RequestStub,
                     ResponseStub>),
            Handler>);
  }

  static void test_missing_bridge_returns_service_unavailable()
  {
    ServerWithoutBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"chat.message","payload":{"text":"hello"}})");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert(response.statusCode == 503);

    const auto &body =
        response_json(response);

    assert(body.is_object());
    assert(body.contains("error"));
    assert(body["error"].is_string());
  }

  static void test_valid_message_is_forwarded()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "id":"message-1",
              "kind":"command",
              "ts":"2026-07-30T10:00:00Z",
              "room":"general",
              "type":"chat.message",
              "payload":{
                "text":"hello",
                "urgent":true,
                "count":42
              }
            })");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);

    assert(fixture.forwardCalls == 1u);

    assert(
        fixture.forwardedMessage.id ==
        "message-1");

    assert(
        fixture.forwardedMessage.kind ==
        "command");

    assert(
        fixture.forwardedMessage.ts ==
        "2026-07-30T10:00:00Z");

    assert(
        fixture.forwardedMessage.room ==
        "general");

    assert(
        fixture.forwardedMessage.type ==
        "chat.message");

    const nlohmann::json forwarded =
        fixture.forwardedMessage.to_nlohmann();

    assert(
        forwarded["payload"]["text"] ==
        "hello");

    assert(
        forwarded["payload"]["urgent"] ==
        true);

    assert(
        forwarded["payload"]["count"] ==
        42);
  }

  static void test_minimal_message_is_forwarded()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"system.ready"})");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);

    assert(fixture.forwardCalls == 1u);

    assert(
        fixture.forwardedMessage.type ==
        "system.ready");

    assert(
        fixture.forwardedMessage.kind ==
        "event");

    assert(
        fixture.forwardedMessage.id.empty());

    assert(
        fixture.forwardedMessage.room.empty());

    assert(
        fixture.forwardedMessage.ts.empty());
  }

  static void test_message_with_empty_payload_is_forwarded()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "type":"chat.message",
              "payload":{}
            })");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);

    assert(fixture.forwardCalls == 1u);

    const nlohmann::json forwarded =
        fixture.forwardedMessage.to_nlohmann();

    assert(forwarded.contains("payload"));
    assert(forwarded["payload"].is_object());
    assert(forwarded["payload"].empty());
  }

  static void test_session_query_does_not_modify_message()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "id":"message-1",
              "room":"original-room",
              "type":"chat.message"
            })",
            "/ws/send?session_id=session-42");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);

    assert(fixture.forwardCalls == 1u);

    assert(
        fixture.forwardedMessage.id ==
        "message-1");

    assert(
        fixture.forwardedMessage.room ==
        "original-room");

    assert(
        fixture.forwardedMessage.type ==
        "chat.message");
  }

  static void test_url_encoded_session_query_is_accepted()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"chat.message"})",
            "/ws/send?session_id=room%3Ageneral");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);
    assert(fixture.forwardCalls == 1u);
  }

  static void test_empty_body_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request("");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_whitespace_body_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            "   \n\t  ");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_malformed_json_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"chat.message")");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_json_array_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"([{"type":"chat.message"}])");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_json_string_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"("chat.message")");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_missing_type_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "id":"message-1",
              "payload":{"text":"hello"}
            })");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_empty_type_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "type":"",
              "payload":{"text":"hello"}
            })");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_null_type_is_rejected()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({
              "type":null,
              "payload":{"text":"hello"}
            })");

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_client_error(response);
    assert(fixture.forwardCalls == 0u);
  }

  static void test_multiple_messages_are_forwarded_independently()
  {
    ServerWithBridgeFixture fixture;

    for (std::size_t index = 1u;
         index <= 10u;
         ++index)
    {
      RequestStub request =
          make_request(
              nlohmann::json{
                  {"id",
                   "message-" +
                       std::to_string(index)},
                  {"type",
                   "event-" +
                       std::to_string(index)},
                  {"payload",
                   nlohmann::json::object()}}
                  .dump());

      ResponseStub response;

      http::handle_ws_send(
          request,
          response,
          fixture.server);

      assert_success(response);

      assert(
          fixture.forwardCalls ==
          index);

      assert(
          fixture.forwardedMessage.id ==
          "message-" +
              std::to_string(index));

      assert(
          fixture.forwardedMessage.type ==
          "event-" +
              std::to_string(index));
    }
  }

  static void test_forwarding_is_synchronous()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"chat.message"})");

    ResponseStub response;

    assert(fixture.forwardCalls == 0u);

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert(fixture.forwardCalls == 1u);
  }

  static void test_request_body_is_not_modified()
  {
    ServerWithBridgeFixture fixture;

    RequestStub request =
        make_request(
            R"({"type":"chat.message","payload":{"text":"hello"}})");

    const std::string original =
        request.bodyValue;

    ResponseStub response;

    http::handle_ws_send(
        request,
        response,
        fixture.server);

    assert_success(response);

    assert(
        request.bodyValue ==
        original);
  }

} // namespace

int main()
{
  test_handler_signature();

  test_missing_bridge_returns_service_unavailable();

  test_valid_message_is_forwarded();
  test_minimal_message_is_forwarded();
  test_message_with_empty_payload_is_forwarded();

  test_session_query_does_not_modify_message();
  test_url_encoded_session_query_is_accepted();

  test_empty_body_is_rejected();
  test_whitespace_body_is_rejected();
  test_malformed_json_is_rejected();

  test_json_array_is_rejected();
  test_json_string_is_rejected();

  test_missing_type_is_rejected();
  test_empty_type_is_rejected();
  test_null_type_is_rejected();

  test_multiple_messages_are_forwarded_independently();
  test_forwarding_is_synchronous();
  test_request_body_is_not_modified();

  return 0;
}
