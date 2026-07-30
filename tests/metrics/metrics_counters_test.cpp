/**
 *
 * @file metrics_counters_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

#include <vix/websocket/Metrics.hpp>

namespace
{
  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

  template <typename Counter>
  static auto load(
      const Counter &counter) noexcept
  {
    return counter.load(
        std::memory_order_relaxed);
  }

  template <typename Counter>
  static void assert_zero(
      const Counter &counter)
  {
    assert(load(counter) == 0);
  }

  static void assert_all_zero(
      const WebSocketMetrics &metrics)
  {
    assert_zero(metrics.connections_total);
    assert_zero(metrics.connections_active);

    assert_zero(metrics.messages_in_total);
    assert_zero(metrics.messages_out_total);

    assert_zero(metrics.errors_total);

    assert_zero(metrics.lp_sessions_total);
    assert_zero(metrics.lp_sessions_active);

    assert_zero(metrics.lp_polls_total);
    assert_zero(metrics.lp_messages_buffered);

    assert_zero(
        metrics.lp_messages_enqueued_total);

    assert_zero(
        metrics.lp_messages_drained_total);
  }

  static void test_counter_types_are_atomic_integrals()
  {
    static_assert(
        std::is_integral_v<
            decltype(std::declval<
                         const WebSocketMetrics &>()
                         .connections_total
                         .load())>);

    static_assert(
        std::is_integral_v<
            decltype(std::declval<
                         const WebSocketMetrics &>()
                         .lp_sessions_total
                         .load())>);

    static_assert(
        std::is_integral_v<
            decltype(std::declval<
                         const WebSocketMetrics &>()
                         .lp_messages_buffered
                         .load())>);
  }

  static void test_initial_values_are_zero()
  {
    const WebSocketMetrics metrics;

    assert_all_zero(metrics);
  }

  static void test_websocket_counters_can_be_stored()
  {
    WebSocketMetrics metrics;

    metrics.connections_total.store(
        100u,
        std::memory_order_relaxed);

    metrics.connections_active.store(
        12u,
        std::memory_order_relaxed);

    metrics.messages_in_total.store(
        250u,
        std::memory_order_relaxed);

    metrics.messages_out_total.store(
        175u,
        std::memory_order_relaxed);

    metrics.errors_total.store(
        7u,
        std::memory_order_relaxed);

    assert(load(metrics.connections_total) == 100u);
    assert(load(metrics.connections_active) == 12u);

    assert(load(metrics.messages_in_total) == 250u);
    assert(load(metrics.messages_out_total) == 175u);

    assert(load(metrics.errors_total) == 7u);
  }

  static void test_long_polling_counters_can_be_stored()
  {
    WebSocketMetrics metrics;

    metrics.lp_sessions_total.store(
        20u,
        std::memory_order_relaxed);

    metrics.lp_sessions_active.store(
        4u,
        std::memory_order_relaxed);

    metrics.lp_polls_total.store(
        300u,
        std::memory_order_relaxed);

    metrics.lp_messages_buffered.store(
        15u,
        std::memory_order_relaxed);

    metrics.lp_messages_enqueued_total.store(
        500u,
        std::memory_order_relaxed);

    metrics.lp_messages_drained_total.store(
        485u,
        std::memory_order_relaxed);

    assert(load(metrics.lp_sessions_total) == 20u);
    assert(load(metrics.lp_sessions_active) == 4u);

    assert(load(metrics.lp_polls_total) == 300u);

    assert(
        load(metrics.lp_messages_buffered) ==
        15u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        500u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        485u);
  }

  static void test_fetch_add_returns_previous_value()
  {
    WebSocketMetrics metrics;

    const auto previous =
        metrics.connections_total.fetch_add(
            5u,
            std::memory_order_relaxed);

    assert(previous == 0u);
    assert(load(metrics.connections_total) == 5u);

    const auto secondPrevious =
        metrics.connections_total.fetch_add(
            3u,
            std::memory_order_relaxed);

    assert(secondPrevious == 5u);
    assert(load(metrics.connections_total) == 8u);
  }

  static void test_increment_operators()
  {
    WebSocketMetrics metrics;

    ++metrics.connections_total;
    ++metrics.connections_total;

    metrics.messages_in_total++;
    metrics.messages_in_total++;

    ++metrics.messages_out_total;
    metrics.errors_total++;

    assert(load(metrics.connections_total) == 2u);
    assert(load(metrics.messages_in_total) == 2u);
    assert(load(metrics.messages_out_total) == 1u);
    assert(load(metrics.errors_total) == 1u);
  }

  static void test_active_connection_gauge_can_decrease()
  {
    WebSocketMetrics metrics;

    metrics.connections_active.store(
        5u,
        std::memory_order_relaxed);

    const auto previous =
        metrics.connections_active.fetch_sub(
            2u,
            std::memory_order_relaxed);

    assert(previous == 5u);
    assert(load(metrics.connections_active) == 3u);

    --metrics.connections_active;

    assert(load(metrics.connections_active) == 2u);
  }

  static void test_active_session_gauge_can_decrease()
  {
    WebSocketMetrics metrics;

    metrics.lp_sessions_active.store(
        10u,
        std::memory_order_relaxed);

    metrics.lp_sessions_active.fetch_sub(
        4u,
        std::memory_order_relaxed);

    assert(
        load(metrics.lp_sessions_active) ==
        6u);

    --metrics.lp_sessions_active;

    assert(
        load(metrics.lp_sessions_active) ==
        5u);
  }

  static void test_buffered_message_gauge_can_change()
  {
    WebSocketMetrics metrics;

    metrics.lp_messages_buffered.fetch_add(
        10u,
        std::memory_order_relaxed);

    assert(
        load(metrics.lp_messages_buffered) ==
        10u);

    metrics.lp_messages_buffered.fetch_sub(
        3u,
        std::memory_order_relaxed);

    assert(
        load(metrics.lp_messages_buffered) ==
        7u);

    metrics.lp_messages_buffered.store(
        0u,
        std::memory_order_relaxed);

    assert_zero(
        metrics.lp_messages_buffered);
  }

  static void test_counters_accumulate_independently()
  {
    WebSocketMetrics metrics;

    metrics.connections_total.fetch_add(
        10u,
        std::memory_order_relaxed);

    metrics.connections_active.fetch_add(
        3u,
        std::memory_order_relaxed);

    metrics.messages_in_total.fetch_add(
        25u,
        std::memory_order_relaxed);

    metrics.messages_out_total.fetch_add(
        20u,
        std::memory_order_relaxed);

    metrics.errors_total.fetch_add(
        2u,
        std::memory_order_relaxed);

    metrics.lp_sessions_total.fetch_add(
        4u,
        std::memory_order_relaxed);

    metrics.lp_sessions_active.fetch_add(
        2u,
        std::memory_order_relaxed);

    metrics.lp_polls_total.fetch_add(
        30u,
        std::memory_order_relaxed);

    metrics.lp_messages_buffered.fetch_add(
        6u,
        std::memory_order_relaxed);

    metrics.lp_messages_enqueued_total.fetch_add(
        40u,
        std::memory_order_relaxed);

    metrics.lp_messages_drained_total.fetch_add(
        34u,
        std::memory_order_relaxed);

    assert(load(metrics.connections_total) == 10u);
    assert(load(metrics.connections_active) == 3u);

    assert(load(metrics.messages_in_total) == 25u);
    assert(load(metrics.messages_out_total) == 20u);

    assert(load(metrics.errors_total) == 2u);

    assert(load(metrics.lp_sessions_total) == 4u);
    assert(load(metrics.lp_sessions_active) == 2u);

    assert(load(metrics.lp_polls_total) == 30u);

    assert(
        load(metrics.lp_messages_buffered) ==
        6u);

    assert(
        load(
            metrics.lp_messages_enqueued_total) ==
        40u);

    assert(
        load(
            metrics.lp_messages_drained_total) ==
        34u);
  }

  static void test_modifying_one_counter_does_not_modify_others()
  {
    WebSocketMetrics metrics;

    metrics.messages_in_total.store(
        42u,
        std::memory_order_relaxed);

    assert(load(metrics.messages_in_total) == 42u);

    assert_zero(metrics.connections_total);
    assert_zero(metrics.connections_active);

    assert_zero(metrics.messages_out_total);
    assert_zero(metrics.errors_total);

    assert_zero(metrics.lp_sessions_total);
    assert_zero(metrics.lp_sessions_active);

    assert_zero(metrics.lp_polls_total);
    assert_zero(metrics.lp_messages_buffered);

    assert_zero(
        metrics.lp_messages_enqueued_total);

    assert_zero(
        metrics.lp_messages_drained_total);
  }

  static void test_instances_are_independent()
  {
    WebSocketMetrics first;
    WebSocketMetrics second;

    first.connections_total.store(
        10u,
        std::memory_order_relaxed);

    first.messages_in_total.store(
        20u,
        std::memory_order_relaxed);

    first.lp_sessions_total.store(
        30u,
        std::memory_order_relaxed);

    first.lp_messages_buffered.store(
        40u,
        std::memory_order_relaxed);

    assert(load(first.connections_total) == 10u);
    assert(load(first.messages_in_total) == 20u);
    assert(load(first.lp_sessions_total) == 30u);

    assert(
        load(first.lp_messages_buffered) ==
        40u);

    assert_all_zero(second);
  }

  static void test_exchange_replaces_counter_value()
  {
    WebSocketMetrics metrics;

    metrics.errors_total.store(
        8u,
        std::memory_order_relaxed);

    const auto previous =
        metrics.errors_total.exchange(
            3u,
            std::memory_order_relaxed);

    assert(previous == 8u);
    assert(load(metrics.errors_total) == 3u);
  }

  static void test_compare_exchange_updates_expected_value()
  {
    WebSocketMetrics metrics;

    metrics.connections_active.store(
        5u,
        std::memory_order_relaxed);

    decltype(metrics.connections_active.load())
        expected = 5u;

    const bool exchanged =
        metrics.connections_active
            .compare_exchange_strong(
                expected,
                6u,
                std::memory_order_relaxed);

    assert(exchanged == true);
    assert(expected == 5u);

    assert(
        load(metrics.connections_active) ==
        6u);
  }

  static void test_compare_exchange_failure_reports_current_value()
  {
    WebSocketMetrics metrics;

    metrics.connections_active.store(
        5u,
        std::memory_order_relaxed);

    decltype(metrics.connections_active.load())
        expected = 3u;

    const bool exchanged =
        metrics.connections_active
            .compare_exchange_strong(
                expected,
                6u,
                std::memory_order_relaxed);

    assert(exchanged == false);
    assert(expected == 5u);

    assert(
        load(metrics.connections_active) ==
        5u);
  }

  static void test_concurrent_counter_increments()
  {
    WebSocketMetrics metrics;

    constexpr std::size_t threadCount = 8u;
    constexpr std::size_t incrementsPerThread =
        10000u;

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (std::size_t threadIndex = 0u;
         threadIndex < threadCount;
         ++threadIndex)
    {
      workers.emplace_back(
          [&metrics]()
          {
            for (std::size_t index = 0u;
                 index < incrementsPerThread;
                 ++index)
            {
              metrics.messages_in_total.fetch_add(
                  1u,
                  std::memory_order_relaxed);

              metrics.lp_polls_total.fetch_add(
                  1u,
                  std::memory_order_relaxed);
            }
          });
    }

    for (std::thread &worker : workers)
    {
      worker.join();
    }

    constexpr std::size_t expected =
        threadCount * incrementsPerThread;

    assert(
        load(metrics.messages_in_total) ==
        expected);

    assert(
        load(metrics.lp_polls_total) ==
        expected);
  }

  static void test_concurrent_independent_counters()
  {
    WebSocketMetrics metrics;

    constexpr std::size_t increments = 20000u;

    std::thread incoming(
        [&metrics]()
        {
          for (std::size_t index = 0u;
               index < increments;
               ++index)
          {
            ++metrics.messages_in_total;
          }
        });

    std::thread outgoing(
        [&metrics]()
        {
          for (std::size_t index = 0u;
               index < increments;
               ++index)
          {
            ++metrics.messages_out_total;
          }
        });

    std::thread errors(
        [&metrics]()
        {
          for (std::size_t index = 0u;
               index < increments;
               ++index)
          {
            ++metrics.errors_total;
          }
        });

    incoming.join();
    outgoing.join();
    errors.join();

    assert(
        load(metrics.messages_in_total) ==
        increments);

    assert(
        load(metrics.messages_out_total) ==
        increments);

    assert(
        load(metrics.errors_total) ==
        increments);
  }

} // namespace

int main()
{
  test_counter_types_are_atomic_integrals();
  test_initial_values_are_zero();

  test_websocket_counters_can_be_stored();
  test_long_polling_counters_can_be_stored();

  test_fetch_add_returns_previous_value();
  test_increment_operators();

  test_active_connection_gauge_can_decrease();
  test_active_session_gauge_can_decrease();
  test_buffered_message_gauge_can_change();

  test_counters_accumulate_independently();
  test_modifying_one_counter_does_not_modify_others();
  test_instances_are_independent();

  test_exchange_replaces_counter_value();

  test_compare_exchange_updates_expected_value();
  test_compare_exchange_failure_reports_current_value();

  test_concurrent_counter_increments();
  test_concurrent_independent_counters();

  return 0;
}
