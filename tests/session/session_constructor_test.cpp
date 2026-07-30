/**
 *
 * @file session_constructor_test.cpp
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
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <vix/websocket/session.hpp>

namespace
{
  using Config =
      vix::websocket::Config;

  using Router =
      vix::websocket::Router;

  using Session =
      vix::websocket::Session;

  using RuntimeExecutor =
      vix::executor::RuntimeExecutor;

  using io_context =
      vix::async::core::io_context;

  using tcp_stream =
      vix::async::net::tcp_stream;

  static std::unique_ptr<tcp_stream>
  null_stream()
  {
    return {};
  }

  static std::shared_ptr<Router>
  null_router()
  {
    return {};
  }

  static std::shared_ptr<RuntimeExecutor>
  null_executor()
  {
    return {};
  }

  static std::shared_ptr<io_context>
  make_io_context()
  {
    return std::make_shared<
        io_context>();
  }

  static void test_constructor_accepts_valid_io_context()
  {
    Config config;

    const auto context =
        make_io_context();

    Session session{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    assert(context != nullptr);
    assert(session.is_open() == false);
  }

  static void test_constructor_accepts_null_stream()
  {
    Config config;

    const auto context =
        make_io_context();

    bool constructed = false;

    try
    {
      Session session{
          std::unique_ptr<
              tcp_stream>{},
          config,
          null_router(),
          null_executor(),
          context};

      constructed = true;

      assert(
          session.is_open() ==
          false);
    }
    catch (...)
    {
      assert(false);
    }

    assert(constructed);
  }

  static void test_constructor_accepts_null_router()
  {
    Config config;

    const auto context =
        make_io_context();

    bool constructed = false;

    try
    {
      Session session{
          null_stream(),
          config,
          std::shared_ptr<
              Router>{},
          null_executor(),
          context};

      constructed = true;

      assert(
          session.is_open() ==
          false);
    }
    catch (...)
    {
      assert(false);
    }

    assert(constructed);
  }

  static void test_constructor_accepts_null_executor()
  {
    Config config;

    const auto context =
        make_io_context();

    bool constructed = false;

    try
    {
      Session session{
          null_stream(),
          config,
          null_router(),
          std::shared_ptr<
              RuntimeExecutor>{},
          context};

      constructed = true;

      assert(
          session.is_open() ==
          false);
    }
    catch (...)
    {
      assert(false);
    }

    assert(constructed);
  }

  static void test_constructor_rejects_null_io_context()
  {
    Config config;

    bool thrown = false;

    try
    {
      Session session{
          null_stream(),
          config,
          null_router(),
          null_executor(),
          std::shared_ptr<
              io_context>{}};

      (void)session;
    }
    catch (
        const std::invalid_argument &)
    {
      thrown = true;
    }
    catch (...)
    {
      assert(false);
    }

    assert(thrown);
  }

  static void test_null_io_context_error_message()
  {
    Config config;

    try
    {
      Session session{
          null_stream(),
          config,
          null_router(),
          null_executor(),
          nullptr};

      (void)session;

      assert(false);
    }
    catch (
        const std::invalid_argument &error)
    {
      assert(
          std::string{
              error.what()} ==
          "websocket session requires a valid io_context");
    }
  }

  static void test_constructor_checks_io_context_with_other_null_dependencies()
  {
    Config config;

    bool thrown = false;

    try
    {
      Session session{
          std::unique_ptr<
              tcp_stream>{},
          config,
          std::shared_ptr<
              Router>{},
          std::shared_ptr<
              RuntimeExecutor>{},
          std::shared_ptr<
              io_context>{}};

      (void)session;
    }
    catch (
        const std::invalid_argument &)
    {
      thrown = true;
    }

    assert(thrown);
  }

  static void test_session_starts_closed()
  {
    Config config;

    const auto context =
        make_io_context();

    Session session{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    assert(!session.is_open());
  }

  static void test_multiple_sessions_can_share_io_context()
  {
    Config config;

    const auto context =
        make_io_context();

    Session first{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    Session second{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    Session third{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    assert(context.use_count() == 4);

    assert(!first.is_open());
    assert(!second.is_open());
    assert(!third.is_open());
  }

  static void test_shared_session_construction()
  {
    Config config;

    const auto context =
        make_io_context();

    auto session =
        std::make_shared<Session>(
            null_stream(),
            config,
            null_router(),
            null_executor(),
            context);

    assert(session != nullptr);
    assert(session.use_count() == 1);
    assert(!session->is_open());
  }

  static void test_shared_from_this_after_shared_construction()
  {
    Config config;

    const auto context =
        make_io_context();

    auto session =
        std::make_shared<Session>(
            null_stream(),
            config,
            null_router(),
            null_executor(),
            context);

    const std::shared_ptr<Session> self =
        session->shared_from_this();

    assert(self != nullptr);

    assert(
        self.get() ==
        session.get());

    assert(session.use_count() == 2);
    assert(self.use_count() == 2);
  }

  static void test_weak_from_this_after_shared_construction()
  {
    Config config;

    const auto context =
        make_io_context();

    auto session =
        std::make_shared<Session>(
            null_stream(),
            config,
            null_router(),
            null_executor(),
            context);

    const std::weak_ptr<Session> weak =
        session->weak_from_this();

    assert(!weak.expired());

    const auto locked =
        weak.lock();

    assert(locked != nullptr);

    assert(
        locked.get() ==
        session.get());
  }

  static void test_stack_session_has_no_shared_owner()
  {
    Config config;

    const auto context =
        make_io_context();

    Session session{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    const std::weak_ptr<Session> weak =
        session.weak_from_this();

    assert(weak.expired());
    assert(weak.lock() == nullptr);
  }

  static void test_shared_from_this_on_stack_session_throws()
  {
    Config config;

    const auto context =
        make_io_context();

    Session session{
        null_stream(),
        config,
        null_router(),
        null_executor(),
        context};

    bool thrown = false;

    try
    {
      const auto self =
          session.shared_from_this();

      (void)self;
    }
    catch (
        const std::bad_weak_ptr &)
    {
      thrown = true;
    }

    assert(thrown);
  }

  static void test_moving_context_argument_transfers_caller_ownership()
  {
    Config config;

    auto context =
        make_io_context();

    std::weak_ptr<io_context> weak =
        context;

    {
      Session session{
          null_stream(),
          config,
          null_router(),
          null_executor(),
          std::move(context)};

      assert(context == nullptr);
      assert(!weak.expired());

      assert(
          session.is_open() ==
          false);
    }

    assert(weak.expired());
  }

  static void test_copying_context_argument_preserves_caller_ownership()
  {
    Config config;

    auto context =
        make_io_context();

    assert(context.use_count() == 1);

    {
      Session session{
          null_stream(),
          config,
          null_router(),
          null_executor(),
          context};

      assert(context != nullptr);
      assert(context.use_count() == 2);
    }

    assert(context.use_count() == 1);
  }

  static void test_repeated_construction_and_destruction()
  {
    Config config;

    const auto context =
        make_io_context();

    constexpr std::size_t count = 100u;

    for (std::size_t index = 0u;
         index < count;
         ++index)
    {
      Session session{
          null_stream(),
          config,
          null_router(),
          null_executor(),
          context};

      assert(!session.is_open());
    }

    assert(context.use_count() == 1);
  }

  static void test_shared_sessions_are_destroyed_normally()
  {
    Config config;

    const auto context =
        make_io_context();

    std::weak_ptr<Session> weak;

    {
      auto session =
          std::make_shared<Session>(
              null_stream(),
              config,
              null_router(),
              null_executor(),
              context);

      weak = session;

      assert(!weak.expired());
      assert(!session->is_open());
    }

    assert(weak.expired());
  }

} // namespace

int main()
{
  test_constructor_accepts_valid_io_context();

  test_constructor_accepts_null_stream();
  test_constructor_accepts_null_router();
  test_constructor_accepts_null_executor();

  test_constructor_rejects_null_io_context();
  test_null_io_context_error_message();

  test_constructor_checks_io_context_with_other_null_dependencies();

  test_session_starts_closed();
  test_multiple_sessions_can_share_io_context();

  test_shared_session_construction();
  test_shared_from_this_after_shared_construction();
  test_weak_from_this_after_shared_construction();

  test_stack_session_has_no_shared_owner();
  test_shared_from_this_on_stack_session_throws();

  test_moving_context_argument_transfers_caller_ownership();
  test_copying_context_argument_preserves_caller_ownership();

  test_repeated_construction_and_destruction();
  test_shared_sessions_are_destroyed_normally();

  return 0;
}
