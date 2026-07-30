/**
 *
 * @file router_constructor_test.cpp
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
#include <string>
#include <type_traits>
#include <memory>

#include <vix/websocket/router.hpp>

namespace
{
  using Router = vix::websocket::Router;
  using Session = vix::websocket::Session;

  static void test_router_type_traits()
  {
    static_assert(std::is_default_constructible_v<Router>);
    static_assert(std::is_destructible_v<Router>);
  }

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

  static void test_default_construction()
  {
    Router router;

    (void)router;
  }

  static void test_multiple_routers_can_be_constructed()
  {
    Router first;
    Router second;
    Router third;

    assert(&first != &second);
    assert(&second != &third);
    assert(&first != &third);
  }

  static void test_constructor_does_not_invoke_open_handler()
  {
    bool called = false;

    Router router;

    router.on_open(
        [&called](Session &)
        {
          called = true;
        });

    assert(called == false);
  }

  static void test_constructor_does_not_invoke_message_handler()
  {
    bool called = false;

    Router router;

    router.on_message(
        [&called](
            Session &,
            const std::string &)
        {
          called = true;
        });

    assert(called == false);
  }

  static void test_constructor_does_not_invoke_close_handler()
  {
    bool called = false;

    Router router;

    router.on_close(
        [&called](Session &)
        {
          called = true;
        });

    assert(called == false);
  }

  static void test_constructor_does_not_invoke_error_handler()
  {
    bool called = false;

    Router router;

    router.on_error(
        [&called](
            Session &,
            const std::string &)
        {
          called = true;
        });

    assert(called == false);
  }

  static void test_all_handlers_can_be_registered()
  {
    bool open_called = false;
    bool message_called = false;
    bool close_called = false;
    bool error_called = false;

    Router router;

    router.on_open(
        [&open_called](Session &)
        {
          open_called = true;
        });

    router.on_message(
        [&message_called](
            Session &,
            const std::string &)
        {
          message_called = true;
        });

    router.on_close(
        [&close_called](Session &)
        {
          close_called = true;
        });

    router.on_error(
        [&error_called](
            Session &,
            const std::string &)
        {
          error_called = true;
        });

    assert(open_called == false);
    assert(message_called == false);
    assert(close_called == false);
    assert(error_called == false);
  }

  static void test_handlers_can_be_replaced()
  {
    Router router;

    router.on_open(
        [](Session &) {});

    router.on_open(
        [](Session &) {});

    router.on_message(
        [](Session &, const std::string &) {});

    router.on_message(
        [](Session &, const std::string &) {});

    router.on_close(
        [](Session &) {});

    router.on_close(
        [](Session &) {});

    router.on_error(
        [](Session &, const std::string &) {});

    router.on_error(
        [](Session &, const std::string &) {});
  }

  static void test_handlers_can_be_cleared()
  {
    Router router;

    router.on_open(
        [](Session &) {});

    router.on_message(
        [](Session &, const std::string &) {});

    router.on_close(
        [](Session &) {});

    router.on_error(
        [](Session &, const std::string &) {});

    router.on_open({});
    router.on_message({});
    router.on_close({});
    router.on_error({});
  }

  static void test_router_construction_has_no_external_dependencies()
  {
    Router router;

    /*
     * Router construction does not require:
     *
     * - a TCP stream;
     * - an io_context;
     * - a runtime executor;
     * - a WebSocket configuration;
     * - an active session.
     */
    (void)router;
  }

  static void test_router_can_be_stack_allocated()
  {
    {
      Router router;

      router.on_open(
          [](Session &) {});
    }

    assert(true);
  }

  static void test_router_can_be_dynamically_allocated()
  {
    auto router =
        std::make_unique<Router>();

    assert(router != nullptr);
  }

  static void test_router_can_be_shared()
  {
    auto router =
        std::make_shared<Router>();

    assert(router != nullptr);
    assert(router.use_count() == 1);

    auto second = router;

    assert(router.use_count() == 2);
    assert(second.get() == router.get());
  }

} // namespace

int main()
{
  test_router_type_traits();
  test_handler_type_contracts();

  test_default_construction();
  test_multiple_routers_can_be_constructed();

  test_constructor_does_not_invoke_open_handler();
  test_constructor_does_not_invoke_message_handler();
  test_constructor_does_not_invoke_close_handler();
  test_constructor_does_not_invoke_error_handler();

  test_all_handlers_can_be_registered();
  test_handlers_can_be_replaced();
  test_handlers_can_be_cleared();

  test_router_construction_has_no_external_dependencies();

  test_router_can_be_stack_allocated();
  test_router_can_be_dynamically_allocated();
  test_router_can_be_shared();

  return 0;
}
