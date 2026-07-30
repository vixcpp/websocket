/**
 *
 * @file long_polling_manager_limits_test.cpp
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
#include <vector>

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

  static void push_range(
      LongPollingManager &manager,
      const std::string &sessionId,
      std::size_t first,
      std::size_t last)
  {
    for (std::size_t index = first;
         index <= last;
         ++index)
    {
      manager.push_to(
          sessionId,
          make_message(index));
    }
  }

  static void test_custom_limit_can_be_reached_exactly()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        3u};

    push_range(
        manager,
        "session-1",
        1u,
        3u);

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 3u);

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

  static void test_overflow_drops_oldest_messages()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        3u};

    push_range(
        manager,
        "session-1",
        1u,
        5u);

    assert(manager.buffer_size("session-1") == 3u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 3u);

    assert(messages[0].id == "message-3");
    assert(messages[1].id == "message-4");
    assert(messages[2].id == "message-5");
  }

  static void test_buffer_never_exceeds_custom_limit()
  {
    constexpr std::size_t limit = 8u;

    LongPollingManager manager{
        std::chrono::seconds{60},
        limit};

    for (std::size_t index = 1u;
         index <= 1000u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_message(index));

      assert(
          manager.buffer_size("session-1") <=
          limit);
    }

    assert(manager.buffer_size("session-1") == limit);
    assert(manager.session_count() == 1u);
  }

  static void test_zero_limit_keeps_buffer_empty()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        0u};

    push_range(
        manager,
        "session-1",
        1u,
        10u);

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 0u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.empty());
    assert(manager.session_count() == 1u);
  }

  static void test_limit_one_keeps_latest_message()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        1u};

    push_range(
        manager,
        "session-1",
        1u,
        10u);

    assert(manager.buffer_size("session-1") == 1u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-10");
    assert(messages[0].type == "event-10");
  }

  static void test_default_limit_is_256()
  {
    LongPollingManager manager;

    push_range(
        manager,
        "session-1",
        1u,
        300u);

    assert(manager.buffer_size("session-1") == 256u);

    const auto messages =
        manager.poll(
            "session-1",
            300u,
            false);

    assert(messages.size() == 256u);

    assert(messages.front().id == "message-45");
    assert(messages.back().id == "message-300");
  }

  static void test_limits_are_independent_per_session()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        2u};

    push_range(
        manager,
        "session-a",
        1u,
        5u);

    push_range(
        manager,
        "session-b",
        10u,
        12u);

    assert(manager.session_count() == 2u);

    assert(manager.buffer_size("session-a") == 2u);
    assert(manager.buffer_size("session-b") == 2u);

    const auto first =
        manager.poll(
            "session-a",
            10u,
            false);

    const auto second =
        manager.poll(
            "session-b",
            10u,
            false);

    assert(first.size() == 2u);
    assert(first[0].id == "message-4");
    assert(first[1].id == "message-5");

    assert(second.size() == 2u);
    assert(second[0].id == "message-11");
    assert(second[1].id == "message-12");
  }

  static void test_poll_limit_is_independent_from_buffer_limit()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        10u};

    push_range(
        manager,
        "session-1",
        1u,
        6u);

    const auto first =
        manager.poll(
            "session-1",
            2u,
            false);

    assert(first.size() == 2u);
    assert(first[0].id == "message-1");
    assert(first[1].id == "message-2");

    assert(manager.buffer_size("session-1") == 4u);

    const auto second =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(second.size() == 4u);

    assert(second[0].id == "message-3");
    assert(second[1].id == "message-4");
    assert(second[2].id == "message-5");
    assert(second[3].id == "message-6");
  }

  static void test_zero_poll_limit_does_not_drain_buffer()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        4u};

    push_range(
        manager,
        "session-1",
        1u,
        4u);

    const auto messages =
        manager.poll(
            "session-1",
            0u,
            false);

    assert(messages.empty());
    assert(manager.buffer_size("session-1") == 4u);
  }

  static void test_poll_larger_than_buffer_drains_everything()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        4u};

    push_range(
        manager,
        "session-1",
        1u,
        3u);

    const auto messages =
        manager.poll(
            "session-1",
            100u,
            false);

    assert(messages.size() == 3u);
    assert(manager.buffer_size("session-1") == 0u);

    assert(messages[0].id == "message-1");
    assert(messages[1].id == "message-2");
    assert(messages[2].id == "message-3");
  }

  static void test_partial_poll_releases_buffer_capacity()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        3u};

    push_range(
        manager,
        "session-1",
        1u,
        3u);

    const auto first =
        manager.poll(
            "session-1",
            2u,
            false);

    assert(first.size() == 2u);
    assert(manager.buffer_size("session-1") == 1u);

    manager.push_to(
        "session-1",
        make_message(4u));

    manager.push_to(
        "session-1",
        make_message(5u));

    assert(manager.buffer_size("session-1") == 3u);

    const auto second =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(second.size() == 3u);

    assert(second[0].id == "message-3");
    assert(second[1].id == "message-4");
    assert(second[2].id == "message-5");
  }

  static void test_overflow_does_not_create_extra_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        2u};

    push_range(
        manager,
        "session-1",
        1u,
        100u);

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 2u);
  }

  static void test_move_constructor_preserves_limit()
  {
    LongPollingManager source{
        std::chrono::seconds{60},
        2u};

    source.push_to(
        "session-1",
        make_message(1u));

    source.push_to(
        "session-1",
        make_message(2u));

    LongPollingManager destination{
        std::move(source)};

    destination.push_to(
        "session-1",
        make_message(3u));

    assert(destination.buffer_size("session-1") == 2u);

    const auto messages =
        destination.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 2u);
    assert(messages[0].id == "message-2");
    assert(messages[1].id == "message-3");
  }

  static void test_move_assignment_preserves_limit()
  {
    LongPollingManager source{
        std::chrono::seconds{60},
        2u};

    source.push_to(
        "session-1",
        make_message(1u));

    LongPollingManager destination{
        std::chrono::seconds{60},
        10u};

    destination =
        std::move(source);

    destination.push_to(
        "session-1",
        make_message(2u));

    destination.push_to(
        "session-1",
        make_message(3u));

    assert(destination.buffer_size("session-1") == 2u);

    const auto messages =
        destination.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 2u);
    assert(messages[0].id == "message-2");
    assert(messages[1].id == "message-3");
  }

} // namespace

int main()
{
  test_custom_limit_can_be_reached_exactly();
  test_overflow_drops_oldest_messages();
  test_buffer_never_exceeds_custom_limit();

  test_zero_limit_keeps_buffer_empty();
  test_limit_one_keeps_latest_message();
  test_default_limit_is_256();

  test_limits_are_independent_per_session();

  test_poll_limit_is_independent_from_buffer_limit();
  test_zero_poll_limit_does_not_drain_buffer();
  test_poll_larger_than_buffer_drains_everything();

  test_partial_poll_releases_buffer_capacity();

  test_overflow_does_not_create_extra_sessions();

  test_move_constructor_preserves_limit();
  test_move_assignment_preserves_limit();

  return 0;
}
