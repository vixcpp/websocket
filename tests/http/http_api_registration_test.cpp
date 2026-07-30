/**
 *
 * @file http_api_registration_test.cpp
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
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/app/App.hpp>
#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/http/Request.hpp>
#include <vix/http/ResponseWrapper.hpp>
#include <vix/router/Router.hpp>
#include <vix/runtime/Budget.hpp>
#include <vix/runtime/Runtime.hpp>
#include <vix/websocket/HttpApi.hpp>
#include <vix/websocket/server.hpp>

namespace
{
  namespace websocket_http =
      vix::websocket::http;

  using App =
      vix::App;

  using Config =
      vix::config::Config;

  using Request =
      vix::http::Request;

  using ResponseWrapper =
      vix::http::ResponseWrapper;

  using RouteRecord =
      vix::router::Router::RouteRecord;

  using RuntimeConfig =
      vix::runtime::RuntimeConfig;

  using RuntimeExecutor =
      vix::executor::RuntimeExecutor;

  using Server =
      vix::websocket::Server;

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

  static void unset_env_var(
      const char *name)
  {
#if defined(_WIN32)
    const std::string assignment =
        std::string{name} +
        "=";

    const int result =
        _putenv(
            assignment.c_str());
#else
    const int result =
        unsetenv(name);
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

    unset_env_var(
        "SERVER_PORT");

    unset_env_var(
        "SERVER_TLS_ENABLED");

    unset_env_var(
        "SERVER_TLS_CERT_FILE");

    unset_env_var(
        "SERVER_TLS_KEY_FILE");
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

  struct ServerFixture
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

  static void register_http_routes(
      App &app,
      Server &server,
      std::string pollPath = "/ws/poll",
      std::string sendPath = "/ws/send")
  {
    app.get(
        std::move(pollPath),
        [&server](
            Request &request,
            ResponseWrapper &response)
        {
          websocket_http::handle_ws_poll(
              request,
              response,
              server);
        });

    app.post(
        std::move(sendPath),
        [&server](
            Request &request,
            ResponseWrapper &response)
        {
          websocket_http::handle_ws_send(
              request,
              response,
              server);
        });
  }

  static bool has_record(
      const std::vector<RouteRecord> &records,
      const std::string &method,
      const std::string &path,
      bool heavy)
  {
    for (const RouteRecord &record :
         records)
    {
      if (record.method == method &&
          record.path == path &&
          record.heavy == heavy)
      {
        return true;
      }
    }

    return false;
  }

  static std::size_t count_records(
      const std::vector<RouteRecord> &records,
      const std::string &method,
      const std::string &path)
  {
    std::size_t count = 0u;

    for (const RouteRecord &record :
         records)
    {
      if (record.method == method &&
          record.path == path)
      {
        ++count;
      }
    }

    return count;
  }

  static void assert_route_registered(
      App &app,
      const std::string &method,
      const std::string &path)
  {
    assert(app.router() != nullptr);

    assert(
        app.router()->has_route(
            method,
            path) == true);
  }

  static void assert_route_not_registered(
      App &app,
      const std::string &method,
      const std::string &path)
  {
    assert(app.router() != nullptr);

    assert(
        app.router()->has_route(
            method,
            path) == false);
  }

  static void test_handler_signatures()
  {
    using Handler =
        void (*)(
            Request &,
            ResponseWrapper &,
            Server &);

    static_assert(
        std::is_same_v<
            decltype(&websocket_http::
                         handle_ws_poll<
                             Request,
                             ResponseWrapper>),
            Handler>);

    static_assert(
        std::is_same_v<
            decltype(&websocket_http::
                         handle_ws_send<
                             Request,
                             ResponseWrapper>),
            Handler>);
  }

  static void test_default_routes_are_registered()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    assert_route_registered(
        app,
        "GET",
        "/ws/poll");

    assert_route_registered(
        app,
        "POST",
        "/ws/send");

    app.close();
  }

  static void test_options_routes_are_registered()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    assert_route_registered(
        app,
        "OPTIONS",
        "/ws/poll");

    assert_route_registered(
        app,
        "OPTIONS",
        "/ws/send");

    app.close();
  }

  static void test_wrong_methods_are_not_registered()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    assert_route_not_registered(
        app,
        "POST",
        "/ws/poll");

    assert_route_not_registered(
        app,
        "GET",
        "/ws/send");

    assert_route_not_registered(
        app,
        "PUT",
        "/ws/poll");

    assert_route_not_registered(
        app,
        "DELETE",
        "/ws/send");

    app.close();
  }

  static void test_registration_adds_four_route_records()
  {
    ServerFixture fixture;

    App app;

    assert(app.router() != nullptr);

    const std::size_t before =
        app.router()->routes().size();

    register_http_routes(
        app,
        fixture.server);

    const std::size_t after =
        app.router()->routes().size();

    assert(after == before + 4u);

    app.close();
  }

  static void test_registered_routes_are_not_heavy()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    const auto &records =
        app.router()->routes();

    assert(
        has_record(
            records,
            "GET",
            "/ws/poll",
            false) == true);

    assert(
        has_record(
            records,
            "POST",
            "/ws/send",
            false) == true);

    assert(
        has_record(
            records,
            "OPTIONS",
            "/ws/poll",
            false) == true);

    assert(
        has_record(
            records,
            "OPTIONS",
            "/ws/send",
            false) == true);

    app.close();
  }

  static void test_each_route_is_registered_once()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    const auto &records =
        app.router()->routes();

    assert(
        count_records(
            records,
            "GET",
            "/ws/poll") ==
        1u);

    assert(
        count_records(
            records,
            "POST",
            "/ws/send") ==
        1u);

    assert(
        count_records(
            records,
            "OPTIONS",
            "/ws/poll") ==
        1u);

    assert(
        count_records(
            records,
            "OPTIONS",
            "/ws/send") ==
        1u);

    app.close();
  }

  static void test_custom_paths_are_registered()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server,
        "/api/realtime/poll",
        "/api/realtime/send");

    assert_route_registered(
        app,
        "GET",
        "/api/realtime/poll");

    assert_route_registered(
        app,
        "POST",
        "/api/realtime/send");

    assert_route_registered(
        app,
        "OPTIONS",
        "/api/realtime/poll");

    assert_route_registered(
        app,
        "OPTIONS",
        "/api/realtime/send");

    assert_route_not_registered(
        app,
        "GET",
        "/ws/poll");

    assert_route_not_registered(
        app,
        "POST",
        "/ws/send");

    app.close();
  }

  static void test_paths_without_leading_slash_are_normalized()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server,
        "api/ws/poll",
        "api/ws/send");

    assert_route_registered(
        app,
        "GET",
        "/api/ws/poll");

    assert_route_registered(
        app,
        "POST",
        "/api/ws/send");

    assert_route_registered(
        app,
        "OPTIONS",
        "/api/ws/poll");

    assert_route_registered(
        app,
        "OPTIONS",
        "/api/ws/send");

    app.close();
  }

  static void test_registration_does_not_start_http_server()
  {
    ServerFixture fixture;

    App app;

    assert(app.is_running() == false);
    assert(app.has_server_ready_info() == false);

    register_http_routes(
        app,
        fixture.server);

    assert(app.is_running() == false);
    assert(app.has_server_ready_info() == false);

    app.close();
  }

  static void test_poll_route_is_classified_as_light()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    Request request{
        std::string{"GET"},
        std::string{"/ws/poll"}};

    assert(
        app.router()->is_heavy(
            request) == false);

    app.close();
  }

  static void test_send_route_is_classified_as_light()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    Request request{
        std::string{"POST"},
        std::string{"/ws/send"}};

    assert(
        app.router()->is_heavy(
            request) == false);

    app.close();
  }

  static void test_query_string_does_not_change_route_matching()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    Request pollRequest{
        std::string{"GET"},
        std::string{
            "/ws/poll?session_id=session-1&max=10"}};

    Request sendRequest{
        std::string{"POST"},
        std::string{
            "/ws/send?session_id=session-1"}};

    assert(
        app.router()->is_heavy(
            pollRequest) == false);

    assert(
        app.router()->is_heavy(
            sendRequest) == false);

    app.close();
  }

  static void test_existing_routes_are_preserved()
  {
    ServerFixture fixture;

    App app;

    app.get(
        "/health",
        [](
            Request &,
            ResponseWrapper &response)
        {
          response.ok().text("ok");
        });

    assert_route_registered(
        app,
        "GET",
        "/health");

    assert_route_registered(
        app,
        "OPTIONS",
        "/health");

    register_http_routes(
        app,
        fixture.server);

    assert_route_registered(
        app,
        "GET",
        "/health");

    assert_route_registered(
        app,
        "OPTIONS",
        "/health");

    assert_route_registered(
        app,
        "GET",
        "/ws/poll");

    assert_route_registered(
        app,
        "POST",
        "/ws/send");

    app.close();
  }

  static void test_separate_apps_register_independently()
  {
    ServerFixture firstFixture;
    ServerFixture secondFixture;

    App first;
    App second;

    register_http_routes(
        first,
        firstFixture.server);

    assert_route_registered(
        first,
        "GET",
        "/ws/poll");

    assert_route_registered(
        first,
        "POST",
        "/ws/send");

    assert_route_not_registered(
        second,
        "GET",
        "/ws/poll");

    assert_route_not_registered(
        second,
        "POST",
        "/ws/send");

    register_http_routes(
        second,
        secondFixture.server,
        "/events/poll",
        "/events/send");

    assert_route_registered(
        second,
        "GET",
        "/events/poll");

    assert_route_registered(
        second,
        "POST",
        "/events/send");

    assert_route_not_registered(
        first,
        "GET",
        "/events/poll");

    assert_route_not_registered(
        first,
        "POST",
        "/events/send");

    first.close();
    second.close();
  }

  static void test_registration_survives_close_before_listen()
  {
    ServerFixture fixture;

    App app;

    register_http_routes(
        app,
        fixture.server);

    assert_route_registered(
        app,
        "GET",
        "/ws/poll");

    assert_route_registered(
        app,
        "POST",
        "/ws/send");

    app.close();

    assert(app.is_running() == false);
  }

} // namespace

int main()
{
  test_handler_signatures();

  test_default_routes_are_registered();
  test_options_routes_are_registered();
  test_wrong_methods_are_not_registered();

  test_registration_adds_four_route_records();
  test_registered_routes_are_not_heavy();
  test_each_route_is_registered_once();

  test_custom_paths_are_registered();
  test_paths_without_leading_slash_are_normalized();

  test_registration_does_not_start_http_server();

  test_poll_route_is_classified_as_light();
  test_send_route_is_classified_as_light();
  test_query_string_does_not_change_route_matching();

  test_existing_routes_are_preserved();
  test_separate_apps_register_independently();

  test_registration_survives_close_before_listen();

  return 0;
}
