/**
 *
 * @file router_error_test.cpp
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
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

  static void test_error_handler_type()
  {
    static_assert(
        std::is_same_v<
            Router::ErrorHandler,
            std::function<void(
                Session &,
                const std::string &)>>);
  }

  static void test_error_registration_return_type()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().on_error(
                std::declval<Router::ErrorHandler>())),
            void>);
  }

  static void test_error_dispatch_return_type()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().handle_error(
                std::declval<Session &>(),
                std::declval<const std::string &>())),
            void>);
  }

  static void test_error_without_handler_is_safe()
  {
    Fixture fixture;

    fixture.router->handle_error(
        *fixture.session,
        "connection reset");

    assert(true);
  }

  static void test_error_handler_is_called()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++calls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "connection reset");

    assert(calls == 1);
  }

  static void test_error_handler_receives_same_session()
  {
    Fixture fixture;

    Session *receivedSession = nullptr;

    fixture.router->on_error(
        [&](Session &session,
            const std::string &)
        {
          receivedSession = &session;
        });

    fixture.router->handle_error(
        *fixture.session,
        "error");

    assert(
        receivedSession ==
        fixture.session.get());
  }

  static void test_error_handler_receives_exact_message()
  {
    Fixture fixture;

    std::string receivedError;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          receivedError = error;
        });

    const std::string original =
        "websocket frame too short";

    fixture.router->handle_error(
        *fixture.session,
        original);

    assert(receivedError == original);
    assert(original == "websocket frame too short");
  }

  static void test_empty_error_message_is_dispatched()
  {
    Fixture fixture;

    int calls = 0;
    std::string receivedError = "not-empty";

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          ++calls;
          receivedError = error;
        });

    fixture.router->handle_error(
        *fixture.session,
        "");

    assert(calls == 1);
    assert(receivedError.empty());
  }

  static void test_long_error_message_is_preserved()
  {
    Fixture fixture;

    std::string receivedError;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          receivedError = error;
        });

    const std::string original(
        16384u,
        'x');

    fixture.router->handle_error(
        *fixture.session,
        original);

    assert(receivedError.size() == original.size());
    assert(receivedError == original);
  }

  static void test_error_message_with_embedded_null_bytes()
  {
    Fixture fixture;

    std::string receivedError;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          receivedError = error;
        });

    const std::string original{
        'r',
        'e',
        'a',
        'd',
        '\0',
        'e',
        'r',
        'r',
        'o',
        'r'};

    fixture.router->handle_error(
        *fixture.session,
        original);

    assert(receivedError.size() == 10u);
    assert(receivedError == original);
    assert(receivedError[4] == '\0');
  }

  static void test_multiple_errors_are_dispatched_in_order()
  {
    Fixture fixture;

    std::vector<std::string> receivedErrors;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          receivedErrors.push_back(error);
        });

    fixture.router->handle_error(
        *fixture.session,
        "first");

    fixture.router->handle_error(
        *fixture.session,
        "second");

    fixture.router->handle_error(
        *fixture.session,
        "third");

    assert(receivedErrors.size() == 3u);

    assert(receivedErrors[0] == "first");
    assert(receivedErrors[1] == "second");
    assert(receivedErrors[2] == "third");
  }

  static void test_repeated_error_dispatch()
  {
    Fixture fixture;

    constexpr std::size_t count = 100u;

    std::size_t calls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          assert(error == "repeated error");
          ++calls;
        });

    for (std::size_t i = 0; i < count; ++i)
    {
      fixture.router->handle_error(
          *fixture.session,
          "repeated error");
    }

    assert(calls == count);
  }

  static void test_error_handler_can_be_replaced()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "first");

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "second");

    assert(firstCalls == 1);
    assert(secondCalls == 1);
  }

  static void test_replacing_handler_before_dispatch()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "error");

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_error_handler_can_be_cleared()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++calls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "first");

    fixture.router->on_error({});

    fixture.router->handle_error(
        *fixture.session,
        "second");

    assert(calls == 1);
  }

  static void test_error_handler_can_be_registered_after_clear()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->on_error({});

    fixture.router->handle_error(
        *fixture.session,
        "ignored");

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "handled");

    assert(firstCalls == 0);
    assert(secondCalls == 1);
  }

  static void test_error_dispatch_is_synchronous()
  {
    Fixture fixture;

    bool completed = false;

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          completed = true;
        });

    fixture.router->handle_error(
        *fixture.session,
        "synchronous");

    assert(completed == true);
  }

  static void test_error_handler_can_modify_external_state()
  {
    Fixture fixture;

    std::size_t totalLength = 0u;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          totalLength += error.size();
        });

    fixture.router->handle_error(
        *fixture.session,
        "abc");

    fixture.router->handle_error(
        *fixture.session,
        "12345");

    assert(totalLength == 8u);
  }

  static void test_error_dispatch_does_not_invoke_other_handlers()
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
        [&](Session &,
            const std::string &)
        {
          ++messageCalls;
        });

    fixture.router->on_close(
        [&](Session &)
        {
          ++closeCalls;
        });

    fixture.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++errorCalls;
        });

    fixture.router->handle_error(
        *fixture.session,
        "only error");

    assert(openCalls == 0);
    assert(messageCalls == 0);
    assert(closeCalls == 0);
    assert(errorCalls == 1);
  }

  static void test_separate_routers_have_independent_error_handlers()
  {
    Fixture first;
    Fixture second;

    int firstCalls = 0;
    int secondCalls = 0;

    first.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    second.router->on_error(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    first.router->handle_error(
        *first.session,
        "first");

    first.router->handle_error(
        *first.session,
        "first again");

    second.router->handle_error(
        *second.session,
        "second");

    assert(firstCalls == 2);
    assert(secondCalls == 1);
  }

} // namespace

int main()
{
  test_error_handler_type();
  test_error_registration_return_type();
  test_error_dispatch_return_type();

  test_error_without_handler_is_safe();
  test_error_handler_is_called();

  test_error_handler_receives_same_session();
  test_error_handler_receives_exact_message();

  test_empty_error_message_is_dispatched();
  test_long_error_message_is_preserved();
  test_error_message_with_embedded_null_bytes();

  test_multiple_errors_are_dispatched_in_order();
  test_repeated_error_dispatch();

  test_error_handler_can_be_replaced();
  test_replacing_handler_before_dispatch();

  test_error_handler_can_be_cleared();
  test_error_handler_can_be_registered_after_clear();

  test_error_dispatch_is_synchronous();
  test_error_handler_can_modify_external_state();

  test_error_dispatch_does_not_invoke_other_handlers();
  test_separate_routers_have_independent_error_handlers();

  return 0;
}
