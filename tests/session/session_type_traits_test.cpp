/**
 *
 * @file session_type_traits_test.cpp
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
#include <string_view>
#include <type_traits>
#include <utility>

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

  using task_void =
      vix::async::core::task<void>;

  using SessionBase =
      std::enable_shared_from_this<
          Session>;

  static void test_session_is_a_class()
  {
    static_assert(
        std::is_class_v<
            Session>);

    static_assert(
        std::is_object_v<
            Session>);

    static_assert(
        !std::is_union_v<
            Session>);

    static_assert(
        !std::is_enum_v<
            Session>);
  }

  static void test_shared_ownership_base()
  {
    static_assert(
        std::is_base_of_v<
            SessionBase,
            Session>);

    static_assert(
        std::is_convertible_v<
            Session *,
            SessionBase *>);

    static_assert(
        std::is_convertible_v<
            const Session *,
            const SessionBase *>);
  }

  static void test_constructor_contract()
  {
    static_assert(
        std::is_constructible_v<
            Session,
            std::unique_ptr<tcp_stream>,
            const Config &,
            std::shared_ptr<Router>,
            std::shared_ptr<RuntimeExecutor>,
            std::shared_ptr<io_context>>);

    static_assert(
        std::is_constructible_v<
            Session,
            std::unique_ptr<tcp_stream>,
            Config &,
            std::shared_ptr<Router>,
            std::shared_ptr<RuntimeExecutor>,
            std::shared_ptr<io_context>>);

    static_assert(
        !std::is_default_constructible_v<
            Session>);

    static_assert(
        !std::is_constructible_v<
            Session,
            std::unique_ptr<tcp_stream>,
            const Config &,
            std::shared_ptr<Router>,
            std::shared_ptr<RuntimeExecutor>>);
  }

  static void test_copy_and_move_contract()
  {
    static_assert(
        !std::is_copy_constructible_v<
            Session>);

    static_assert(
        !std::is_copy_assignable_v<
            Session>);

    static_assert(
        !std::is_move_constructible_v<
            Session>);

    static_assert(
        !std::is_move_assignable_v<
            Session>);
  }

  static void test_destruction_contract()
  {
    static_assert(
        std::is_destructible_v<
            Session>);

    static_assert(
        !std::has_virtual_destructor_v<
            Session>);

    static_assert(
        !std::is_polymorphic_v<
            Session>);

    static_assert(
        !std::is_abstract_v<
            Session>);

    static_assert(
        !std::is_final_v<
            Session>);
  }

  static void test_public_method_signatures()
  {
    static_assert(
        std::is_same_v<
            decltype(&Session::run),
            task_void (Session::*)()>);

    static_assert(
        std::is_same_v<
            decltype(&Session::send_text),
            void (Session::*)(
                std::string_view)>);

    static_assert(
        std::is_same_v<
            decltype(&Session::send_binary),
            void (Session::*)(
                const void *,
                std::size_t)>);

    static_assert(
        std::is_same_v<
            decltype(&Session::close),
            void (Session::*)(
                std::string)>);

    static_assert(
        std::is_same_v<
            decltype(&Session::is_open),
            bool (Session::*)()
                const noexcept>);

    static_assert(
        std::is_same_v<
            decltype(&Session::emit_error),
            void (Session::*)(
                const std::string &)>);

    static_assert(
        std::is_same_v<
            decltype(&Session::shutdown_now),
            void (Session::*)() noexcept>);
  }

  static void test_shared_pointer_contracts()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         std::shared_ptr<
                             Session>>()
                         .get()),
            Session *>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         std::weak_ptr<
                             Session>>()
                         .lock()),
            std::shared_ptr<Session>>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         Session &>()
                         .shared_from_this()),
            std::shared_ptr<Session>>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         const Session &>()
                         .shared_from_this()),
            std::shared_ptr<
                const Session>>);
  }

  static void test_pointer_defaults()
  {
    std::shared_ptr<Session> shared;
    std::weak_ptr<Session> weak;

    Session *raw = nullptr;

    assert(shared == nullptr);
    assert(shared.use_count() == 0);

    assert(weak.expired());
    assert(weak.lock() == nullptr);

    assert(raw == nullptr);
  }

} // namespace

int main()
{
  test_session_is_a_class();
  test_shared_ownership_base();

  test_constructor_contract();
  test_copy_and_move_contract();
  test_destruction_contract();

  test_public_method_signatures();
  test_shared_pointer_contracts();

  test_pointer_defaults();

  return 0;
}
