/**
 *
 * @file long_polling_manager_sweep_test.cpp
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
#include <cstddef>
#include <string>
#include <utility>

#include <vix/websocket/LongPolling.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingManager =
      vix::websocket::LongPollingManager;

  static JsonMessage make_message(
      std::size_t index)
  {
    JsonMessage message;

    message.id =
        "message-" + std::to_string(index);

    message.kind = "event";
    message.room = "general";

    message.type =
        "event-" + std::to_string(index);

    return message;
  }

  static void test_sweep_empty_manager_is_safe()
  {
    LongPollingManager manager;

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_default_ttl_keeps_fresh_session()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.sweep_expired();

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 1u);
  }

  static void test_large_ttl_keeps_active_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{86400},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-2",
        make_message(2u));

    manager.push_to(
        "session-3",
        make_message(3u));

    manager.sweep_expired();

    assert(manager.session_count() == 3u);

    assert(manager.buffer_size("session-1") == 1u);
    assert(manager.buffer_size("session-2") == 1u);
    assert(manager.buffer_size("session-3") == 1u);
  }

  static void test_negative_ttl_removes_fresh_session()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    assert(manager.session_count() == 1u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
    assert(manager.buffer_size("session-1") == 0u);
  }

  static void test_negative_ttl_removes_all_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-2",
        make_message(2u));

    manager.push_to(
        "session-3",
        make_message(3u));

    assert(manager.session_count() == 3u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    assert(manager.buffer_size("session-1") == 0u);
    assert(manager.buffer_size("session-2") == 0u);
    assert(manager.buffer_size("session-3") == 0u);
  }

  static void test_sweep_removes_buffered_messages_with_session()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    for (std::size_t index = 1u;
         index <= 10u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_message(index));
    }

    assert(manager.buffer_size("session-1") == 10u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
    assert(manager.buffer_size("session-1") == 0u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.empty());
    assert(manager.session_count() == 0u);
  }

  static void test_empty_created_session_can_be_swept()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    const auto messages =
        manager.poll(
            "empty-session",
            10u,
            true);

    assert(messages.empty());
    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("empty-session") == 0u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_missing_poll_does_not_create_session_to_sweep()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    const auto messages =
        manager.poll(
            "missing-session",
            10u,
            false);

    assert(messages.empty());
    assert(manager.session_count() == 0u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_sweep_is_idempotent()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-2",
        make_message(2u));

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    manager.sweep_expired();
    manager.sweep_expired();
    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_active_sweep_preserves_message_order()
  {
    LongPollingManager manager{
        std::chrono::seconds{86400},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-1",
        make_message(2u));

    manager.push_to(
        "session-1",
        make_message(3u));

    manager.sweep_expired();

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 3u);

    assert(messages[0].id == "message-1");
    assert(messages[1].id == "message-2");
    assert(messages[2].id == "message-3");
  }

  static void test_active_sweep_does_not_change_buffer_size()
  {
    LongPollingManager manager{
        std::chrono::seconds{86400},
        16u};

    for (std::size_t index = 1u;
         index <= 8u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_message(index));
    }

    const std::size_t before =
        manager.buffer_size("session-1");

    manager.sweep_expired();

    const std::size_t after =
        manager.buffer_size("session-1");

    assert(before == 8u);
    assert(after == before);
  }

  static void test_session_can_be_recreated_after_sweep()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    manager.push_to(
        "session-1",
        make_message(2u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 1u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-2");
  }

  static void test_drained_session_remains_until_swept()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(manager.buffer_size("session-1") == 0u);
    assert(manager.session_count() == 1u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_sweep_does_not_create_missing_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        16u};

    assert(manager.buffer_size("missing-1") == 0u);
    assert(manager.buffer_size("missing-2") == 0u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);
  }

  static void test_repeated_active_sweeps_preserve_session()
  {
    LongPollingManager manager{
        std::chrono::seconds{86400},
        16u};

    manager.push_to(
        "session-1",
        make_message(1u));

    for (std::size_t index = 0u;
         index < 100u;
         ++index)
    {
      manager.sweep_expired();

      assert(manager.session_count() == 1u);
      assert(manager.buffer_size("session-1") == 1u);
    }
  }

  static void test_move_constructor_preserves_ttl()
  {
    LongPollingManager source{
        std::chrono::seconds{-1},
        16u};

    source.push_to(
        "session-1",
        make_message(1u));

    LongPollingManager destination{
        std::move(source)};

    assert(destination.session_count() == 1u);

    destination.sweep_expired();

    assert(destination.session_count() == 0u);
  }

  static void test_move_assignment_preserves_ttl()
  {
    LongPollingManager source{
        std::chrono::seconds{-1},
        16u};

    source.push_to(
        "session-1",
        make_message(1u));

    LongPollingManager destination{
        std::chrono::seconds{86400},
        16u};

    destination =
        std::move(source);

    assert(destination.session_count() == 1u);

    destination.sweep_expired();

    assert(destination.session_count() == 0u);
  }

} // namespace

int main()
{
  test_sweep_empty_manager_is_safe();

  test_default_ttl_keeps_fresh_session();
  test_large_ttl_keeps_active_sessions();

  test_negative_ttl_removes_fresh_session();
  test_negative_ttl_removes_all_sessions();
  test_sweep_removes_buffered_messages_with_session();

  test_empty_created_session_can_be_swept();
  test_missing_poll_does_not_create_session_to_sweep();

  test_sweep_is_idempotent();

  test_active_sweep_preserves_message_order();
  test_active_sweep_does_not_change_buffer_size();

  test_session_can_be_recreated_after_sweep();
  test_drained_session_remains_until_swept();

  test_sweep_does_not_create_missing_sessions();
  test_repeated_active_sweeps_preserve_session();

  test_move_constructor_preserves_ttl();
  test_move_assignment_preserves_ttl();

  return 0;
}
