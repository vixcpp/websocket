/**
 *
 * @file router_handlers_test.cpp
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
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <vix/async/core/io_context.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/websocket/session.hpp>

namespace
{
  using Config = vix::websocket::Config;
  using Router = vix::websocket::Router;
  using Session = vix::websocket::Session;

  using IoContext =
      vix::async::core::io_context;

  using TcpStream =
      vix::async::net::tcp_stream;

  struct Fixture
  {
    Config config{};
    std::shared_ptr<Router> router{
        std::make_shared<Router>()};

    std::shared_ptr<IoContext> ioContext{
        std::make_shared<IoContext>()};

    std::shared_ptr<Session> session{
        std::make_shared<Session>(
            std::unique_ptr<TcpStream>{},
            config,
            router,
            nullptr,
            ioContext)};
  };

  static void test_handler_type_contracts()
  {
    static_assert(
        std::is_same_v<
            Router::OpenHandler,
            std::function<void(Session &)>>);

    static_assert(
        std::is_same_v<
            Router::MessageHandler,
            std::function<void(
                Session &,
                const std::string &)>>);

    static_assert(
        std::is_same_v<
            Router::CloseHandler,
            std::function<void(Session &)>>);

    static_assert(
        std::is_same_v<
            Router::ErrorHandler,
            std::function<void(
                Session &,
                const std::string &)>>);
  }

  static void test_handler_registration_return_types()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().on_open(
                std::declval<Router::OpenHandler>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().on_message(
                std::declval<Router::MessageHandler>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().on_close(
                std::declval<Router::CloseHandler>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().on_error(
                std::declval<Router::ErrorHandler>())),
            void>);
  }

  static void test_open_handler_can_be_registered()
  {
    Fixture fixture;

    int calls = 0;
    Session *received = nullptr;

    fixture.router->on_open(
        [&](Session &session)
        {
          ++calls;
          received = &session;
        });

    fixture.router->handle_open(
        *fixture.session);

    assert(calls == 1);
    assert(received == fixture.session.get());
  }

  static void test_message_handler_can_be_registered()
  {
    Fixture fixture;

    int calls = 0;
    Session *received = nullptr;
    std::string receivedMessage;

    fixture.router->on_message(
        [&](Session &session,
            const std::string &message)
        {
          ++calls;
          received = &session;
          receivedMessage = message;
        });

    fixture.router->handle_message(
        *fixture.session,
        "hello websocket");

    assert(calls == 1);
    assert(received == fixture.session.get());
    assert(receivedMessage == "hello websocket");
  }

  static void test_close_handler_can_be_registered()
  {
    Fixture fixture;

    int calls = 0;
    Session *received = nullptr;

    fixture.router->on_close(
        [&](Session &session)
        {
          ++calls;
          received = &session;
        });

    fixture.router->handle_close(
        *fixture.session);

    assert(calls == 1);
    assert(received == fixture.session.get());
  }

  static void test_error_handler_can_be_registered()
  {
    Fixture fixture;

    int calls = 0;
    Session *received = nullptr;
    std::string receivedError;

    fixture.router->on_error(
        [&](Session &session,
            const std::string &error)
        {
          ++calls;
          received = &session;
          receivedError = error;
        });

    fixture.router->handle_error(
        *fixture.session,
        "connection reset");

    assert(calls == 1);
    assert(received == fixture.session.get());
    assert(receivedError == "connection reset");
  }

  static void test_handlers_are_independent()
  {
    Fixture fixture;

    int openCalls = 0;
    int messageCalls = 0;
    int closeCalls = 0;
    int errorCalls = 0;

    fixture.router->on_open(
        [&](Session &)
        {
          ++openCalls;
        });

    fixture.router->on_message(
        [&](Session &, const std::string &)
        {
          ++messageCalls;
        });

    fixture.router->on_close(
        [&](Session &)
        {
          ++closeCalls;
        });

    fixture.router->on_error(
        [&](Session &, const std::string &)
        {
          ++errorCalls;
        });

    fixture.router->handle_open(
        *fixture.session);

    assert(openCalls == 1);
    assert(messageCalls == 0);
    assert(closeCalls == 0);
    assert(errorCalls == 0);

    fixture.router->handle_message(
        *fixture.session,
        "message");

    assert(openCalls == 1);
    assert(messageCalls == 1);
    assert(closeCalls == 0);
    assert(errorCalls == 0);

    fixture.router->handle_error(
        *fixture.session,
        "error");

    assert(openCalls == 1);
    assert(messageCalls == 1);
    assert(closeCalls == 0);
    assert(errorCalls == 1);

    fixture.router->handle_close(
        *fixture.session);

    assert(openCalls == 1);
    assert(messageCalls == 1);
    assert(closeCalls == 1);
    assert(errorCalls == 1);
  }

  static void test_open_handler_can_be_replaced()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_open(
        [&](Session &)
        {
          ++firstCalls;
        });

    fixture.router->on_open(
        [&](Session &)
        {
          ++secondCalls;
        });

    fixture.router->handle_open(
        *fixture.session);

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_message_handler_can_be_replaced()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_message(
        [&](Session &, const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->on_message(
        [&](Session &, const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_message(
        *fixture.session,
        "replacement");

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_close_handler_can_be_replaced()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_close(
        [&](Session &)
        {
          ++firstCalls;
        });

    fixture.router->on_close(
        [&](Session &)
        {
          ++secondCalls;
        });

    fixture.router->handle_close(
        *fixture.session);

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_error_handler_can_be_replaced()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_error(
        [&](Session &, const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->on_error(
        [&](Session &, const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "replacement");

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_handlers_can_be_cleared()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_open(
        [&](Session &)
        {
          ++calls;
        });

    fixture.router->on_message(
        [&](Session &, const std::string &)
        {
          ++calls;
        });

    fixture.router->on_close(
        [&](Session &)
        {
          ++calls;
        });

    fixture.router->on_error(
        [&](Session &, const std::string &)
        {
          ++calls;
        });

    fixture.router->on_open({});
    fixture.router->on_message({});
    fixture.router->on_close({});
    fixture.router->on_error({});

    fixture.router->handle_open(
        *fixture.session);

    fixture.router->handle_message(
        *fixture.session,
        "ignored");

    fixture.router->handle_close(
        *fixture.session);

    fixture.router->handle_error(
        *fixture.session,
        "ignored");

    assert(calls == 0);
  }

  static void test_stateful_handlers_preserve_capture()
  {
    Fixture fixture;

    int total = 0;

    fixture.router->on_open(
        [&](Session &)
        {
          total += 1;
        });

    fixture.router->on_message(
        [&](Session &, const std::string &message)
        {
          total +=
              static_cast<int>(message.size());
        });

    fixture.router->on_error(
        [&](Session &, const std::string &error)
        {
          total +=
              static_cast<int>(error.size());
        });

    fixture.router->on_close(
        [&](Session &)
        {
          total += 10;
        });

    fixture.router->handle_open(
        *fixture.session);

    fixture.router->handle_message(
        *fixture.session,
        "abc");

    fixture.router->handle_error(
        *fixture.session,
        "error");

    fixture.router->handle_close(
        *fixture.session);

    assert(total == 19);
  }

} // namespace

int main()
{
  test_handler_type_contracts();
  test_handler_registration_return_types();

  test_open_handler_can_be_registered();
  test_message_handler_can_be_registered();
  test_close_handler_can_be_registered();
  test_error_handler_can_be_registered();

  test_handlers_are_independent();

  test_open_handler_can_be_replaced();
  test_message_handler_can_be_replaced();
  test_close_handler_can_be_replaced();
  test_error_handler_can_be_replaced();

  test_handlers_can_be_cleared();
  test_stateful_handlers_preserve_capture();

  return 0;
}
