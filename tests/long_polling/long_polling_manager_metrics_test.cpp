/**
 *
 * @file long_polling_manager_metrics_test.cpp
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
#include <cstdint>
#include <string>

#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/Metrics.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingManager =
      vix::websocket::LongPollingManager;

  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

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

  static std::uint64_t load(
      const std::atomic<std::uint64_t> &value)
  {
    return value.load(
        std::memory_order_relaxed);
  }

  static void assert_initial_metrics(
      const WebSocketMetrics &metrics)
  {
    assert(load(metrics.lp_sessions_total) == 0u);
    assert(load(metrics.lp_sessions_active) == 0u);

    assert(load(metrics.lp_polls_total) == 0u);

    assert(load(metrics.lp_messages_buffered) == 0u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        0u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        0u);
  }

  static void test_metrics_start_at_zero()
  {
    WebSocketMetrics metrics;

    assert_initial_metrics(metrics);
  }

  static void test_manager_construction_does_not_create_session()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    assert(manager.session_count() == 0u);
    assert_initial_metrics(metrics);
  }

  static void test_first_push_creates_session_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 1u);

    assert(load(metrics.lp_sessions_total) == 1u);
    assert(load(metrics.lp_sessions_active) == 1u);

    assert(load(metrics.lp_polls_total) == 0u);

    assert(load(metrics.lp_messages_buffered) == 1u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        1u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        0u);
  }

  static void test_push_to_existing_session_does_not_recount_session()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-1",
        make_message(2u));

    manager.push_to(
        "session-1",
        make_message(3u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 3u);

    assert(load(metrics.lp_sessions_total) == 1u);
    assert(load(metrics.lp_sessions_active) == 1u);

    assert(load(metrics.lp_messages_buffered) == 3u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        3u);
  }

  static void test_separate_sessions_update_session_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

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

    assert(load(metrics.lp_sessions_total) == 3u);
    assert(load(metrics.lp_sessions_active) == 3u);

    assert(load(metrics.lp_messages_buffered) == 3u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        3u);
  }

  static void test_poll_updates_poll_and_drain_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-1",
        make_message(2u));

    manager.push_to(
        "session-1",
        make_message(3u));

    const auto messages =
        manager.poll(
            "session-1",
            2u,
            false);

    assert(messages.size() == 2u);

    assert(manager.buffer_size("session-1") == 1u);

    assert(load(metrics.lp_polls_total) == 1u);

    assert(load(metrics.lp_messages_buffered) == 1u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        3u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        2u);
  }

  static void test_multiple_polls_accumulate_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    for (std::size_t index = 1u;
         index <= 5u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_message(index));
    }

    const auto first =
        manager.poll(
            "session-1",
            2u,
            false);

    const auto second =
        manager.poll(
            "session-1",
            1u,
            false);

    const auto third =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(first.size() == 2u);
    assert(second.size() == 1u);
    assert(third.size() == 2u);

    assert(manager.buffer_size("session-1") == 0u);

    assert(load(metrics.lp_polls_total) == 3u);

    assert(load(metrics.lp_messages_buffered) == 0u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        5u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        5u);
  }

  static void test_empty_poll_still_counts_as_poll()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    const auto first =
        manager.poll(
            "session-1",
            10u,
            false);

    const auto second =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(first.size() == 1u);
    assert(second.empty());

    assert(load(metrics.lp_polls_total) == 2u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        1u);

    assert(load(metrics.lp_messages_buffered) == 0u);
  }

  static void test_poll_creation_updates_session_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        256u,
        &metrics};

    const auto messages =
        manager.poll(
            "created-by-poll",
            10u,
            true);

    assert(messages.empty());

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("created-by-poll") == 0u);

    assert(load(metrics.lp_sessions_total) == 1u);
    assert(load(metrics.lp_sessions_active) == 1u);

    assert(load(metrics.lp_polls_total) == 1u);

    assert(load(metrics.lp_messages_buffered) == 0u);
  }

  static void test_overflow_keeps_buffered_gauge_bounded()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        2u,
        &metrics};

    for (std::size_t index = 1u;
         index <= 10u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_message(index));

      assert(manager.buffer_size("session-1") <= 2u);

      assert(
          load(metrics.lp_messages_buffered) <=
          2u);
    }

    assert(manager.buffer_size("session-1") == 2u);

    assert(load(metrics.lp_messages_buffered) == 2u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        10u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 2u);

    assert(messages[0].id == "message-9");
    assert(messages[1].id == "message-10");

    assert(load(metrics.lp_messages_buffered) == 0u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        2u);
  }

  static void test_zero_buffer_limit_keeps_buffered_gauge_zero()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{60},
        0u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-1",
        make_message(2u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 0u);

    assert(load(metrics.lp_sessions_total) == 1u);
    assert(load(metrics.lp_sessions_active) == 1u);

    assert(load(metrics.lp_messages_buffered) == 0u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        2u);
  }

  static void test_sweep_updates_active_and_buffered_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    manager.push_to(
        "session-1",
        make_message(2u));

    manager.push_to(
        "session-2",
        make_message(3u));

    assert(manager.session_count() == 2u);

    assert(load(metrics.lp_sessions_total) == 2u);
    assert(load(metrics.lp_sessions_active) == 2u);

    assert(load(metrics.lp_messages_buffered) == 3u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    assert(load(metrics.lp_sessions_total) == 2u);
    assert(load(metrics.lp_sessions_active) == 0u);

    assert(load(metrics.lp_messages_buffered) == 0u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        3u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        0u);
  }

  static void test_active_sweep_does_not_change_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingManager manager{
        std::chrono::hours{24},
        16u,
        &metrics};

    manager.push_to(
        "session-1",
        make_message(1u));

    const std::uint64_t sessionsTotal =
        load(metrics.lp_sessions_total);

    const std::uint64_t sessionsActive =
        load(metrics.lp_sessions_active);

    const std::uint64_t buffered =
        load(metrics.lp_messages_buffered);

    manager.sweep_expired();

    assert(
        load(metrics.lp_sessions_total) ==
        sessionsTotal);

    assert(
        load(metrics.lp_sessions_active) ==
        sessionsActive);

    assert(
        load(metrics.lp_messages_buffered) ==
        buffered);
  }

  static void test_metrics_are_independent_between_managers()
  {
    WebSocketMetrics firstMetrics;
    WebSocketMetrics secondMetrics;

    LongPollingManager first{
        std::chrono::seconds{60},
        16u,
        &firstMetrics};

    LongPollingManager second{
        std::chrono::seconds{60},
        16u,
        &secondMetrics};

    first.push_to(
        "first-session",
        make_message(1u));

    first.push_to(
        "first-session",
        make_message(2u));

    second.push_to(
        "second-session",
        make_message(3u));

    assert(load(firstMetrics.lp_sessions_total) == 1u);
    assert(load(firstMetrics.lp_messages_buffered) == 2u);

    assert(load(secondMetrics.lp_sessions_total) == 1u);
    assert(load(secondMetrics.lp_messages_buffered) == 1u);
  }

} // namespace

int main()
{
  test_metrics_start_at_zero();
  test_manager_construction_does_not_create_session();

  test_first_push_creates_session_metrics();
  test_push_to_existing_session_does_not_recount_session();
  test_separate_sessions_update_session_metrics();

  test_poll_updates_poll_and_drain_metrics();
  test_multiple_polls_accumulate_metrics();
  test_empty_poll_still_counts_as_poll();
  test_poll_creation_updates_session_metrics();

  test_overflow_keeps_buffered_gauge_bounded();
  test_zero_buffer_limit_keeps_buffered_gauge_zero();

  test_sweep_updates_active_and_buffered_metrics();
  test_active_sweep_does_not_change_metrics();

  test_metrics_are_independent_between_managers();

  return 0;
}
