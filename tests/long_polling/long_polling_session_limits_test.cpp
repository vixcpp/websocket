/**
 *
 * @file long_polling_session_limits_test.cpp
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
#include <string>
#include <utility>
#include <vector>

#include <vix/websocket/LongPolling.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingSession =
      vix::websocket::LongPollingSession;

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

  static void enqueue_range(
      LongPollingSession &session,
      std::size_t first,
      std::size_t last,
      std::size_t limit)
  {
    for (
        std::size_t index = first;
        index <= last;
        ++index)
    {
      session.enqueue(
          make_message(index),
          limit);
    }
  }

  static void test_buffer_can_reach_exact_limit()
  {
    LongPollingSession session{
        "session-limit"};

    enqueue_range(
        session,
        1u,
        3u,
        3u);

    assert(session.buffer.size() == 3u);

    assert(session.buffer[0].id == "message-1");
    assert(session.buffer[1].id == "message-2");
    assert(session.buffer[2].id == "message-3");
  }

  static void test_buffer_never_exceeds_limit()
  {
    LongPollingSession session{
        "session-bounded"};

    constexpr std::size_t limit = 5u;

    for (std::size_t index = 1u;
         index <= 100u;
         ++index)
    {
      session.enqueue(
          make_message(index),
          limit);

      assert(session.buffer.size() <= limit);
    }

    assert(session.buffer.size() == limit);
  }

  static void test_overflow_removes_oldest_messages()
  {
    LongPollingSession session{
        "session-overflow"};

    enqueue_range(
        session,
        1u,
        5u,
        3u);

    assert(session.buffer.size() == 3u);

    assert(session.buffer[0].id == "message-3");
    assert(session.buffer[1].id == "message-4");
    assert(session.buffer[2].id == "message-5");
  }

  static void test_limit_one_keeps_latest_message()
  {
    LongPollingSession session{
        "session-limit-one"};

    enqueue_range(
        session,
        1u,
        10u,
        1u);

    assert(session.buffer.size() == 1u);
    assert(session.buffer.front().id == "message-10");
    assert(session.buffer.front().type == "event-10");
  }

  static void test_zero_limit_keeps_buffer_empty()
  {
    LongPollingSession session{
        "session-zero-limit"};

    for (std::size_t index = 1u;
         index <= 10u;
         ++index)
    {
      session.enqueue(
          make_message(index),
          0u);

      assert(session.buffer.empty());
    }
  }

  static void test_large_limit_preserves_all_messages()
  {
    LongPollingSession session{
        "session-large-limit"};

    constexpr std::size_t count = 1000u;
    constexpr std::size_t limit = 2000u;

    for (std::size_t index = 1u;
         index <= count;
         ++index)
    {
      session.enqueue(
          make_message(index),
          limit);
    }

    assert(session.buffer.size() == count);

    assert(session.buffer.front().id == "message-1");
    assert(session.buffer.back().id == "message-1000");
  }

  static void test_overflow_preserves_fifo_order()
  {
    LongPollingSession session{
        "session-overflow-order"};

    enqueue_range(
        session,
        1u,
        8u,
        4u);

    const auto drained =
        session.drain(10u);

    assert(drained.size() == 4u);

    assert(drained[0].id == "message-5");
    assert(drained[1].id == "message-6");
    assert(drained[2].id == "message-7");
    assert(drained[3].id == "message-8");
  }

  static void test_drain_zero_removes_nothing()
  {
    LongPollingSession session{
        "session-drain-zero"};

    enqueue_range(
        session,
        1u,
        3u,
        8u);

    const auto drained =
        session.drain(0u);

    assert(drained.empty());
    assert(session.buffer.size() == 3u);

    assert(session.buffer[0].id == "message-1");
    assert(session.buffer[1].id == "message-2");
    assert(session.buffer[2].id == "message-3");
  }

  static void test_partial_drain_respects_max_count()
  {
    LongPollingSession session{
        "session-partial-limit"};

    enqueue_range(
        session,
        1u,
        10u,
        10u);

    const auto drained =
        session.drain(4u);

    assert(drained.size() == 4u);

    assert(drained[0].id == "message-1");
    assert(drained[1].id == "message-2");
    assert(drained[2].id == "message-3");
    assert(drained[3].id == "message-4");

    assert(session.buffer.size() == 6u);
    assert(session.buffer.front().id == "message-5");
    assert(session.buffer.back().id == "message-10");
  }

  static void test_exact_drain_count()
  {
    LongPollingSession session{
        "session-exact-drain"};

    enqueue_range(
        session,
        1u,
        5u,
        5u);

    const auto drained =
        session.drain(5u);

    assert(drained.size() == 5u);
    assert(session.buffer.empty());

    for (std::size_t index = 0u;
         index < drained.size();
         ++index)
    {
      assert(
          drained[index].id ==
          "message-" +
              std::to_string(index + 1u));
    }
  }

  static void test_drain_count_larger_than_buffer()
  {
    LongPollingSession session{
        "session-large-drain"};

    enqueue_range(
        session,
        1u,
        3u,
        8u);

    const auto drained =
        session.drain(100u);

    assert(drained.size() == 3u);
    assert(session.buffer.empty());
  }

  static void test_partial_drain_then_enqueue()
  {
    LongPollingSession session{
        "session-drain-enqueue"};

    enqueue_range(
        session,
        1u,
        5u,
        5u);

    const auto first =
        session.drain(2u);

    assert(first.size() == 2u);
    assert(session.buffer.size() == 3u);

    session.enqueue(
        make_message(6u),
        5u);

    session.enqueue(
        make_message(7u),
        5u);

    assert(session.buffer.size() == 5u);

    const auto second =
        session.drain(10u);

    assert(second.size() == 5u);

    assert(second[0].id == "message-3");
    assert(second[1].id == "message-4");
    assert(second[2].id == "message-5");
    assert(second[3].id == "message-6");
    assert(second[4].id == "message-7");
  }

  static void test_capacity_is_reusable_after_full_drain()
  {
    LongPollingSession session{
        "session-reuse-capacity"};

    enqueue_range(
        session,
        1u,
        3u,
        3u);

    const auto first =
        session.drain(3u);

    assert(first.size() == 3u);
    assert(session.buffer.empty());

    enqueue_range(
        session,
        4u,
        6u,
        3u);

    assert(session.buffer.size() == 3u);

    assert(session.buffer[0].id == "message-4");
    assert(session.buffer[1].id == "message-5");
    assert(session.buffer[2].id == "message-6");
  }

  static void test_many_overflow_cycles_remain_bounded()
  {
    LongPollingSession session{
        "session-many-cycles"};

    constexpr std::size_t limit = 8u;

    for (std::size_t cycle = 0u;
         cycle < 100u;
         ++cycle)
    {
      for (std::size_t index = 0u;
           index < 16u;
           ++index)
      {
        session.enqueue(
            make_message(
                cycle * 16u + index),
            limit);

        assert(
            session.buffer.size() <=
            limit);
      }

      const auto drained =
          session.drain(3u);

      assert(drained.size() == 3u);
      assert(session.buffer.size() == 5u);
    }
  }

  static void test_limit_applies_independently_per_session()
  {
    LongPollingSession first{
        "first-session"};

    LongPollingSession second{
        "second-session"};

    enqueue_range(
        first,
        1u,
        10u,
        3u);

    enqueue_range(
        second,
        100u,
        110u,
        5u);

    assert(first.buffer.size() == 3u);
    assert(second.buffer.size() == 5u);

    assert(first.buffer.front().id == "message-8");
    assert(first.buffer.back().id == "message-10");

    assert(second.buffer.front().id == "message-106");
    assert(second.buffer.back().id == "message-110");
  }

} // namespace

int main()
{
  test_buffer_can_reach_exact_limit();
  test_buffer_never_exceeds_limit();

  test_overflow_removes_oldest_messages();
  test_limit_one_keeps_latest_message();
  test_zero_limit_keeps_buffer_empty();
  test_large_limit_preserves_all_messages();

  test_overflow_preserves_fifo_order();

  test_drain_zero_removes_nothing();
  test_partial_drain_respects_max_count();
  test_exact_drain_count();
  test_drain_count_larger_than_buffer();

  test_partial_drain_then_enqueue();
  test_capacity_is_reusable_after_full_drain();

  test_many_overflow_cycles_remain_bounded();
  test_limit_applies_independently_per_session();

  return 0;
}
