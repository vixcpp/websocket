/**
 *
 * @file router_dispatch_test.cpp
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

  static void test_dispatch_return_types()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().handle_open(
                std::declval<Session &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().handle_message(
                std::declval<Session &>(),
                std::declval<const std::string &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().handle_close(
                std::declval<Session &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<Router &>().handle_error(
                std::declval<Session &>(),
                std::declval<const std::string &>())),
            void>);
  }

  static void test_dispatch_without_handlers_is_safe()
  {
    Fixture fixture;

    fixture.router->handle_open(
        *fixture.session);

    fixture.router->handle_message(
        *fixture.session,
        "message");

    fixture.router->handle_error(
        *fixture.session,
        "error");

    fixture.router->handle_close(
        *fixture.session);

    assert(true);
  }

  static void test_open_dispatch_passes_same_session()
  {
    Fixture fixture;

    Session *received = nullptr;

    fixture.router->on_open(
        [&](Session &session)
        {
          received = &session;
        });

    fixture.router->handle_open(
        *fixture.session);

    assert(received == fixture.session.get());
  }

  static void test_message_dispatch_passes_same_session()
  {
    Fixture fixture;

    Session *received = nullptr;

    fixture.router->on_message(
        [&](Session &session,
            const std::string &)
        {
          received = &session;
        });

    fixture.router->handle_message(
        *fixture.session,
        "message");

    assert(received == fixture.session.get());
  }

  static void test_close_dispatch_passes_same_session()
  {
    Fixture fixture;

    Session *received = nullptr;

    fixture.router->on_close(
        [&](Session &session)
        {
          received = &session;
        });

    fixture.router->handle_close(
        *fixture.session);

    assert(received == fixture.session.get());
  }

  static void test_error_dispatch_passes_same_session()
  {
    Fixture fixture;

    Session *received = nullptr;

    fixture.router->on_error(
        [&](Session &session,
            const std::string &)
        {
          received = &session;
        });

    fixture.router->handle_error(
        *fixture.session,
        "error");

    assert(received == fixture.session.get());
  }

  static void test_message_dispatch_preserves_payload()
  {
    Fixture fixture;

    std::string received;

    fixture.router->on_message(
        [&](Session &,
            const std::string &message)
        {
          received = message;
        });

    const std::string original =
        "hello from Vix.cpp";

    fixture.router->handle_message(
        *fixture.session,
        original);

    assert(received == original);
    assert(original == "hello from Vix.cpp");
  }

  static void test_empty_message_dispatch()
  {
    Fixture fixture;

    int calls = 0;
    std::string received = "not-empty";

    fixture.router->on_message(
        [&](Session &,
            const std::string &message)
        {
          ++calls;
          received = message;
        });

    fixture.router->handle_message(
        *fixture.session,
        "");

    assert(calls == 1);
    assert(received.empty());
  }

  static void test_message_with_embedded_null_bytes()
  {
    Fixture fixture;

    std::string received;

    fixture.router->on_message(
        [&](Session &,
            const std::string &message)
        {
          received = message;
        });

    const std::string payload{
        'a',
        '\0',
        'b',
        '\0',
        'c'};

    fixture.router->handle_message(
        *fixture.session,
        payload);

    assert(received.size() == 5u);
    assert(received == payload);

    assert(received[0] == 'a');
    assert(received[1] == '\0');
    assert(received[2] == 'b');
    assert(received[3] == '\0');
    assert(received[4] == 'c');
  }

  static void test_error_dispatch_preserves_message()
  {
    Fixture fixture;

    std::string received;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          received = error;
        });

    const std::string original =
        "websocket frame too short";

    fixture.router->handle_error(
        *fixture.session,
        original);

    assert(received == original);
    assert(original == "websocket frame too short");
  }

  static void test_empty_error_dispatch()
  {
    Fixture fixture;

    int calls = 0;
    std::string received = "not-empty";

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          ++calls;
          received = error;
        });

    fixture.router->handle_error(
        *fixture.session,
        "");

    assert(calls == 1);
    assert(received.empty());
  }

  static void test_repeated_open_dispatch()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_open(
        [&](Session &)
        {
          ++calls;
        });

    for (std::size_t i = 0; i < 100u; ++i)
    {
      fixture.router->handle_open(
          *fixture.session);
    }

    assert(calls == 100);
  }

  static void test_repeated_message_dispatch()
  {
    Fixture fixture;

    int calls = 0;
    std::vector<std::string> messages;

    fixture.router->on_message(
        [&](Session &,
            const std::string &message)
        {
          ++calls;
          messages.push_back(message);
        });

    for (std::size_t i = 0; i < 100u; ++i)
    {
      fixture.router->handle_message(
          *fixture.session,
          "message-" + std::to_string(i));
    }

    assert(calls == 100);
    assert(messages.size() == 100u);

    for (std::size_t i = 0; i < messages.size(); ++i)
    {
      assert(
          messages[i] ==
          "message-" + std::to_string(i));
    }
  }

  static void test_repeated_error_dispatch()
  {
    Fixture fixture;

    int calls = 0;
    std::vector<std::string> errors;

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          ++calls;
          errors.push_back(error);
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

    assert(calls == 3);
    assert(errors.size() == 3u);

    assert(errors[0] == "first");
    assert(errors[1] == "second");
    assert(errors[2] == "third");
  }

  static void test_repeated_close_dispatch()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_close(
        [&](Session &)
        {
          ++calls;
        });

    fixture.router->handle_close(
        *fixture.session);

    fixture.router->handle_close(
        *fixture.session);

    fixture.router->handle_close(
        *fixture.session);

    assert(calls == 3);
  }

  static void test_dispatch_order_is_preserved()
  {
    Fixture fixture;

    std::vector<std::string> events;

    fixture.router->on_open(
        [&](Session &)
        {
          events.push_back("open");
        });

    fixture.router->on_message(
        [&](Session &,
            const std::string &message)
        {
          events.push_back(
              "message:" + message);
        });

    fixture.router->on_error(
        [&](Session &,
            const std::string &error)
        {
          events.push_back(
              "error:" + error);
        });

    fixture.router->on_close(
        [&](Session &)
        {
          events.push_back("close");
        });

    fixture.router->handle_open(
        *fixture.session);

    fixture.router->handle_message(
        *fixture.session,
        "hello");

    fixture.router->handle_error(
        *fixture.session,
        "warning");

    fixture.router->handle_close(
        *fixture.session);

    assert(events.size() == 4u);

    assert(events[0] == "open");
    assert(events[1] == "message:hello");
    assert(events[2] == "error:warning");
    assert(events[3] == "close");
  }

  static void test_dispatch_is_synchronous()
  {
    Fixture fixture;

    bool completed = false;

    fixture.router->on_message(
        [&](Session &,
            const std::string &)
        {
          completed = true;
        });

    fixture.router->handle_message(
        *fixture.session,
        "synchronous");

    assert(completed == true);
  }

  static void test_handler_replacement_affects_next_dispatch()
  {
    Fixture fixture;

    int firstCalls = 0;
    int secondCalls = 0;

    fixture.router->on_message(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    fixture.router->handle_message(
        *fixture.session,
        "first");

    fixture.router->on_message(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    fixture.router->handle_message(
        *fixture.session,
        "second");

    assert(firstCalls == 1);
    assert(secondCalls == 1);
  }

  static void test_cleared_handler_ignores_next_dispatch()
  {
    Fixture fixture;

    int calls = 0;

    fixture.router->on_message(
        [&](Session &,
            const std::string &)
        {
          ++calls;
        });

    fixture.router->handle_message(
        *fixture.session,
        "first");

    fixture.router->on_message({});

    fixture.router->handle_message(
        *fixture.session,
        "second");

    assert(calls == 1);
  }

  static void test_separate_routers_dispatch_independently()
  {
    Fixture first;
    Fixture second;

    int firstCalls = 0;
    int secondCalls = 0;

    first.router->on_message(
        [&](Session &,
            const std::string &)
        {
          ++firstCalls;
        });

    second.router->on_message(
        [&](Session &,
            const std::string &)
        {
          ++secondCalls;
        });

    first.router->handle_message(
        *first.session,
        "first");

    first.router->handle_message(
        *first.session,
        "first-again");

    second.router->handle_message(
        *second.session,
        "second");

    assert(firstCalls == 2);
    assert(secondCalls == 1);
  }

} // namespace

int main()
{
  test_dispatch_return_types();
  test_dispatch_without_handlers_is_safe();

  test_open_dispatch_passes_same_session();
  test_message_dispatch_passes_same_session();
  test_close_dispatch_passes_same_session();
  test_error_dispatch_passes_same_session();

  test_message_dispatch_preserves_payload();
  test_empty_message_dispatch();
  test_message_with_embedded_null_bytes();

  test_error_dispatch_preserves_message();
  test_empty_error_dispatch();

  test_repeated_open_dispatch();
  test_repeated_message_dispatch();
  test_repeated_error_dispatch();
  test_repeated_close_dispatch();

  test_dispatch_order_is_preserved();
  test_dispatch_is_synchronous();

  test_handler_replacement_affects_next_dispatch();
  test_cleared_handler_ignores_next_dispatch();

  test_separate_routers_dispatch_independently();

  return 0;
}
