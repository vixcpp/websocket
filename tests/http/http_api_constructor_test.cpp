/**
 *
 * @file http_api_constructor_test.cpp
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
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <vix/websocket/HttpApi.hpp>

namespace
{
  namespace http =
      vix::websocket::http;

  namespace detail =
      vix::websocket::http::detail;

  using Server =
      vix::websocket::Server;

  struct RequestStub
  {
    explicit RequestStub(
        std::string target = "/",
        std::string body = {})
        : targetValue{
              std::move(target)},
          bodyValue{
              std::move(body)}
    {
    }

    [[nodiscard]]
    std::string_view target() const noexcept
    {
      return targetValue;
    }

    [[nodiscard]]
    const std::string &body() const noexcept
    {
      return bodyValue;
    }

    std::string targetValue;
    std::string bodyValue;
  };

  struct QueryRequestStub
  {
    explicit QueryRequestStub(
        std::string value = {})
        : queryValue{
              std::move(value)}
    {
    }

    [[nodiscard]]
    std::string query_value(
        std::string_view) const
    {
      return queryValue;
    }

    std::string queryValue;
  };

  struct JsonRequestStub
  {
    explicit JsonRequestStub(
        std::optional<nlohmann::json> value =
            std::nullopt)
        : jsonValue{
              std::move(value)}
    {
    }

    [[nodiscard]]
    std::optional<nlohmann::json> json() const
    {
      return jsonValue;
    }

    std::optional<nlohmann::json> jsonValue;
  };

  struct ResponseStub
  {
    explicit ResponseStub(
        int statusCode = 0)
        : statusCode{
              statusCode}
    {
    }

    ResponseStub &status(
        int value) noexcept
    {
      statusCode = value;
      return *this;
    }

    ResponseStub &json(
        nlohmann::json value)
    {
      jsonValue =
          std::move(value);

      return *this;
    }

    int statusCode{0};

    std::optional<nlohmann::json>
        jsonValue{};
  };

  static void test_stub_type_contracts()
  {
    static_assert(
        std::is_default_constructible_v<
            RequestStub>);

    static_assert(
        std::is_constructible_v<
            RequestStub,
            std::string,
            std::string>);

    static_assert(
        std::is_default_constructible_v<
            QueryRequestStub>);

    static_assert(
        std::is_default_constructible_v<
            JsonRequestStub>);

    static_assert(
        std::is_default_constructible_v<
            ResponseStub>);

    static_assert(
        std::is_copy_constructible_v<
            RequestStub>);

    static_assert(
        std::is_move_constructible_v<
            RequestStub>);

    static_assert(
        std::is_copy_constructible_v<
            ResponseStub>);

    static_assert(
        std::is_move_constructible_v<
            ResponseStub>);
  }

  static void test_http_helper_return_types()
  {
    static_assert(
        std::is_same_v<
            decltype(detail::hex_val('0')),
            int>);

    static_assert(
        std::is_same_v<
            decltype(detail::url_decode(
                std::declval<
                    std::string_view>())),
            std::string>);

    static_assert(
        std::is_same_v<
            decltype(detail::query_param_from_target(
                std::declval<
                    std::string_view>(),
                std::declval<
                    std::string_view>())),
            std::optional<std::string>>);

    static_assert(
        std::is_same_v<
            decltype(detail::request_target_string(
                std::declval<
                    const RequestStub &>())),
            std::string>);

    static_assert(
        std::is_same_v<
            decltype(detail::get_query_param(
                std::declval<
                    const RequestStub &>(),
                std::declval<
                    std::string_view>())),
            std::optional<std::string>>);

    static_assert(
        std::is_same_v<
            decltype(detail::has_query_param(
                std::declval<
                    const RequestStub &>(),
                std::declval<
                    std::string_view>())),
            bool>);

    static_assert(
        std::is_same_v<
            decltype(detail::get_json_body(
                std::declval<
                    const RequestStub &>())),
            std::optional<nlohmann::json>>);
  }

  static void test_default_request_construction()
  {
    const RequestStub request;

    assert(request.targetValue == "/");
    assert(request.bodyValue.empty());

    assert(request.target() == "/");
    assert(request.body().empty());
  }

  static void test_request_construction_with_values()
  {
    const RequestStub request{
        "/ws/poll?session_id=session-42",
        R"({"type":"chat.message"})"};

    assert(
        request.target() ==
        "/ws/poll?session_id=session-42");

    assert(
        request.body() ==
        R"({"type":"chat.message"})");
  }

  static void test_request_constructor_owns_values()
  {
    std::string target =
        "/ws/send";

    std::string body =
        R"({"type":"system.ready"})";

    const RequestStub request{
        target,
        body};

    target = "/modified";
    body = "{}";

    assert(request.target() == "/ws/send");

    assert(
        request.body() ==
        R"({"type":"system.ready"})");
  }

  static void test_query_request_construction()
  {
    const QueryRequestStub request{
        "session-42"};

    assert(
        request.query_value(
            "session_id") ==
        "session-42");

    assert(
        request.query_value(
            "another-key") ==
        "session-42");
  }

  static void test_json_request_construction()
  {
    const nlohmann::json body{
        {"type", "chat.message"},
        {"room", "general"}};

    const JsonRequestStub request{
        body};

    const auto value =
        request.json();

    assert(value.has_value());

    assert(
        (*value)["type"] ==
        "chat.message");

    assert(
        (*value)["room"] ==
        "general");
  }

  static void test_empty_json_request_construction()
  {
    const JsonRequestStub request;

    assert(
        request.json().has_value() ==
        false);
  }

  static void test_default_response_construction()
  {
    const ResponseStub response;

    assert(response.statusCode == 0);
    assert(response.jsonValue.has_value() == false);
  }

  static void test_response_status_construction()
  {
    const ResponseStub response{
        200};

    assert(response.statusCode == 200);
    assert(response.jsonValue.has_value() == false);
  }

  static void test_response_status_is_chainable()
  {
    ResponseStub response;

    ResponseStub &returned =
        response.status(202);

    assert(&returned == &response);
    assert(response.statusCode == 202);
  }

  static void test_response_json_is_chainable()
  {
    ResponseStub response;

    ResponseStub &returned =
        response
            .status(200)
            .json(
                nlohmann::json{
                    {"status", "ok"}});

    assert(&returned == &response);
    assert(response.statusCode == 200);
    assert(response.jsonValue.has_value());

    assert(
        (*response.jsonValue)["status"] ==
        "ok");
  }

  static void test_poll_handler_can_be_materialized()
  {
    using Handler =
        void (*)(
            RequestStub &,
            ResponseStub &,
            Server &);

    Handler handler =
        &http::handle_ws_poll<
            RequestStub,
            ResponseStub>;

    assert(handler != nullptr);
  }

  static void test_send_handler_can_be_materialized()
  {
    using Handler =
        void (*)(
            RequestStub &,
            ResponseStub &,
            Server &);

    Handler handler =
        &http::handle_ws_send<
            RequestStub,
            ResponseStub>;

    assert(handler != nullptr);
  }

  static void test_handlers_have_expected_signatures()
  {
    using Handler =
        void (*)(
            RequestStub &,
            ResponseStub &,
            Server &);

    static_assert(
        std::is_same_v<
            decltype(&http::handle_ws_poll<
                     RequestStub,
                     ResponseStub>),
            Handler>);

    static_assert(
        std::is_same_v<
            decltype(&http::handle_ws_send<
                     RequestStub,
                     ResponseStub>),
            Handler>);
  }

} // namespace

int main()
{
  test_stub_type_contracts();
  test_http_helper_return_types();

  test_default_request_construction();
  test_request_construction_with_values();
  test_request_constructor_owns_values();

  test_query_request_construction();

  test_json_request_construction();
  test_empty_json_request_construction();

  test_default_response_construction();
  test_response_status_construction();
  test_response_status_is_chainable();
  test_response_json_is_chainable();

  test_poll_handler_can_be_materialized();
  test_send_handler_can_be_materialized();
  test_handlers_have_expected_signatures();

  return 0;
}
