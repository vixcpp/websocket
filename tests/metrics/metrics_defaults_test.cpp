/**
 *
 * @file metrics_defaults_test.cpp
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
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <vix/websocket/Metrics.hpp>

namespace
{
  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

  template <typename Counter>
  static auto load_counter(
      const Counter &counter) noexcept
  {
    return counter.load(
        std::memory_order_relaxed);
  }

  template <typename Counter>
  static void assert_zero(
      const Counter &counter)
  {
    using Value =
        decltype(load_counter(counter));

    static_assert(
        std::is_integral_v<Value>);

    assert(load_counter(counter) == 0);
  }

  static bool contains_line(
      const std::string &output,
      std::string_view line)
  {
    const std::string expected =
        std::string{line} + '\n';

    return output.find(expected) !=
           std::string::npos;
  }

  static std::size_t count_occurrences(
      const std::string &value,
      std::string_view needle)
  {
    if (needle.empty())
    {
      return 0u;
    }

    std::size_t count = 0u;
    std::size_t position = 0u;

    while (true)
    {
      position =
          value.find(
              needle,
              position);

      if (position == std::string::npos)
      {
        break;
      }

      ++count;
      position += needle.size();
    }

    return count;
  }

  static void test_type_contracts()
  {
    static_assert(
        std::is_default_constructible_v<
            WebSocketMetrics>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         const WebSocketMetrics &>()
                         .render_prometheus()),
            std::string>);
  }

  static void test_websocket_counters_default_to_zero()
  {
    const WebSocketMetrics metrics;

    assert_zero(
        metrics.connections_total);

    assert_zero(
        metrics.connections_active);

    assert_zero(
        metrics.messages_in_total);

    assert_zero(
        metrics.messages_out_total);

    assert_zero(
        metrics.errors_total);
  }

  static void test_long_polling_counters_default_to_zero()
  {
    const WebSocketMetrics metrics;

    assert_zero(
        metrics.lp_sessions_total);

    assert_zero(
        metrics.lp_sessions_active);

    assert_zero(
        metrics.lp_polls_total);

    assert_zero(
        metrics.lp_messages_buffered);

    assert_zero(
        metrics.lp_messages_enqueued_total);

    assert_zero(
        metrics.lp_messages_drained_total);
  }

  static void test_all_counters_are_independent()
  {
    WebSocketMetrics metrics;

    metrics.connections_total.store(
        1u,
        std::memory_order_relaxed);

    assert(
        load_counter(
            metrics.connections_total) ==
        1u);

    assert_zero(
        metrics.connections_active);

    assert_zero(
        metrics.messages_in_total);

    assert_zero(
        metrics.messages_out_total);

    assert_zero(
        metrics.errors_total);

    assert_zero(
        metrics.lp_sessions_total);

    assert_zero(
        metrics.lp_sessions_active);

    assert_zero(
        metrics.lp_polls_total);

    assert_zero(
        metrics.lp_messages_buffered);

    assert_zero(
        metrics.lp_messages_enqueued_total);

    assert_zero(
        metrics.lp_messages_drained_total);
  }

  static void test_fresh_instances_have_independent_defaults()
  {
    WebSocketMetrics first;

    first.connections_total.store(
        10u,
        std::memory_order_relaxed);

    first.connections_active.store(
        4u,
        std::memory_order_relaxed);

    first.messages_in_total.store(
        100u,
        std::memory_order_relaxed);

    first.lp_sessions_total.store(
        8u,
        std::memory_order_relaxed);

    first.lp_messages_buffered.store(
        32u,
        std::memory_order_relaxed);

    const WebSocketMetrics second;

    assert_zero(
        second.connections_total);

    assert_zero(
        second.connections_active);

    assert_zero(
        second.messages_in_total);

    assert_zero(
        second.messages_out_total);

    assert_zero(
        second.errors_total);

    assert_zero(
        second.lp_sessions_total);

    assert_zero(
        second.lp_sessions_active);

    assert_zero(
        second.lp_polls_total);

    assert_zero(
        second.lp_messages_buffered);

    assert_zero(
        second.lp_messages_enqueued_total);

    assert_zero(
        second.lp_messages_drained_total);
  }

  static void test_default_prometheus_output_is_not_empty()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(!output.empty());
  }

  static void test_default_websocket_metrics_render_as_zero()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        contains_line(
            output,
            "vix_ws_connections_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_connections_active 0"));

    assert(
        contains_line(
            output,
            "vix_ws_messages_in_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_messages_out_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_errors_total 0"));
  }

  static void test_default_long_polling_metrics_render_as_zero()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        contains_line(
            output,
            "vix_ws_lp_sessions_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_lp_sessions_active 0"));

    assert(
        contains_line(
            output,
            "vix_ws_lp_polls_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_lp_messages_buffered 0"));

    assert(
        contains_line(
            output,
            "vix_ws_lp_messages_enqueued_total 0"));

    assert(
        contains_line(
            output,
            "vix_ws_lp_messages_drained_total 0"));
  }

  static void test_prometheus_output_contains_help_metadata()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        output.find(
            "# HELP vix_ws_connections_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_connections_active ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_messages_in_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_messages_out_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_errors_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_sessions_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_sessions_active ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_polls_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_messages_buffered ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_messages_enqueued_total ") !=
        std::string::npos);

    assert(
        output.find(
            "# HELP vix_ws_lp_messages_drained_total ") !=
        std::string::npos);
  }

  static void test_prometheus_output_contains_metric_types()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_connections_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_connections_active gauge"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_messages_in_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_messages_out_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_errors_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_sessions_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_sessions_active gauge"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_polls_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_buffered gauge"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_enqueued_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_drained_total counter"));
  }

  static void test_each_default_sample_is_rendered_once()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        count_occurrences(
            output,
            "vix_ws_connections_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_connections_active 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_messages_in_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_messages_out_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_errors_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_sessions_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_sessions_active 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_polls_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_messages_buffered 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_messages_enqueued_total 0\n") ==
        1u);

    assert(
        count_occurrences(
            output,
            "vix_ws_lp_messages_drained_total 0\n") ==
        1u);
  }

  static void test_default_render_is_deterministic()
  {
    const WebSocketMetrics first;
    const WebSocketMetrics second;

    const std::string firstOutput =
        first.render_prometheus();

    const std::string secondOutput =
        second.render_prometheus();

    assert(firstOutput == secondOutput);
  }

} // namespace

int main()
{
  test_type_contracts();

  test_websocket_counters_default_to_zero();
  test_long_polling_counters_default_to_zero();

  test_all_counters_are_independent();
  test_fresh_instances_have_independent_defaults();

  test_default_prometheus_output_is_not_empty();

  test_default_websocket_metrics_render_as_zero();
  test_default_long_polling_metrics_render_as_zero();

  test_prometheus_output_contains_help_metadata();
  test_prometheus_output_contains_metric_types();

  test_each_default_sample_is_rendered_once();
  test_default_render_is_deterministic();

  return 0;
}
