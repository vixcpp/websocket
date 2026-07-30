/**
 *
 * @file metrics_prometheus_test.cpp
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
#include <unordered_map>
#include <utility>
#include <vector>

#include <vix/websocket/Metrics.hpp>

namespace
{
  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

  static const std::string &
  expected_default_output()
  {
    static const std::string output =
        "# HELP vix_ws_connections_total Total WebSocket connections created\n"
        "# TYPE vix_ws_connections_total counter\n"
        "vix_ws_connections_total 0\n"
        "\n"
        "# HELP vix_ws_connections_active Current active WebSocket connections\n"
        "# TYPE vix_ws_connections_active gauge\n"
        "vix_ws_connections_active 0\n"
        "\n"
        "# HELP vix_ws_messages_in_total Total number of WebSocket messages received\n"
        "# TYPE vix_ws_messages_in_total counter\n"
        "vix_ws_messages_in_total 0\n"
        "\n"
        "# HELP vix_ws_messages_out_total Total number of WebSocket messages sent\n"
        "# TYPE vix_ws_messages_out_total counter\n"
        "vix_ws_messages_out_total 0\n"
        "\n"
        "# HELP vix_ws_errors_total Total number of WebSocket errors\n"
        "# TYPE vix_ws_errors_total counter\n"
        "vix_ws_errors_total 0\n"
        "\n"
        "# HELP vix_ws_lp_sessions_total Total long-polling sessions ever created\n"
        "# TYPE vix_ws_lp_sessions_total counter\n"
        "vix_ws_lp_sessions_total 0\n"
        "\n"
        "# HELP vix_ws_lp_sessions_active Current active long-polling sessions\n"
        "# TYPE vix_ws_lp_sessions_active gauge\n"
        "vix_ws_lp_sessions_active 0\n"
        "\n"
        "# HELP vix_ws_lp_polls_total Total /ws/poll HTTP calls\n"
        "# TYPE vix_ws_lp_polls_total counter\n"
        "vix_ws_lp_polls_total 0\n"
        "\n"
        "# HELP vix_ws_lp_messages_buffered Current buffered messages for long-polling\n"
        "# TYPE vix_ws_lp_messages_buffered gauge\n"
        "vix_ws_lp_messages_buffered 0\n"
        "\n"
        "# HELP vix_ws_lp_messages_enqueued_total Total messages enqueued into long-poll buffers\n"
        "# TYPE vix_ws_lp_messages_enqueued_total counter\n"
        "vix_ws_lp_messages_enqueued_total 0\n"
        "\n"
        "# HELP vix_ws_lp_messages_drained_total Total messages drained via /ws/poll\n"
        "# TYPE vix_ws_lp_messages_drained_total counter\n"
        "vix_ws_lp_messages_drained_total 0\n";

    return output;
  }

  static const std::string &
  expected_populated_output()
  {
    static const std::string output =
        "# HELP vix_ws_connections_total Total WebSocket connections created\n"
        "# TYPE vix_ws_connections_total counter\n"
        "vix_ws_connections_total 100\n"
        "\n"
        "# HELP vix_ws_connections_active Current active WebSocket connections\n"
        "# TYPE vix_ws_connections_active gauge\n"
        "vix_ws_connections_active 12\n"
        "\n"
        "# HELP vix_ws_messages_in_total Total number of WebSocket messages received\n"
        "# TYPE vix_ws_messages_in_total counter\n"
        "vix_ws_messages_in_total 250\n"
        "\n"
        "# HELP vix_ws_messages_out_total Total number of WebSocket messages sent\n"
        "# TYPE vix_ws_messages_out_total counter\n"
        "vix_ws_messages_out_total 175\n"
        "\n"
        "# HELP vix_ws_errors_total Total number of WebSocket errors\n"
        "# TYPE vix_ws_errors_total counter\n"
        "vix_ws_errors_total 7\n"
        "\n"
        "# HELP vix_ws_lp_sessions_total Total long-polling sessions ever created\n"
        "# TYPE vix_ws_lp_sessions_total counter\n"
        "vix_ws_lp_sessions_total 20\n"
        "\n"
        "# HELP vix_ws_lp_sessions_active Current active long-polling sessions\n"
        "# TYPE vix_ws_lp_sessions_active gauge\n"
        "vix_ws_lp_sessions_active 4\n"
        "\n"
        "# HELP vix_ws_lp_polls_total Total /ws/poll HTTP calls\n"
        "# TYPE vix_ws_lp_polls_total counter\n"
        "vix_ws_lp_polls_total 300\n"
        "\n"
        "# HELP vix_ws_lp_messages_buffered Current buffered messages for long-polling\n"
        "# TYPE vix_ws_lp_messages_buffered gauge\n"
        "vix_ws_lp_messages_buffered 15\n"
        "\n"
        "# HELP vix_ws_lp_messages_enqueued_total Total messages enqueued into long-poll buffers\n"
        "# TYPE vix_ws_lp_messages_enqueued_total counter\n"
        "vix_ws_lp_messages_enqueued_total 500\n"
        "\n"
        "# HELP vix_ws_lp_messages_drained_total Total messages drained via /ws/poll\n"
        "# TYPE vix_ws_lp_messages_drained_total counter\n"
        "vix_ws_lp_messages_drained_total 485\n";

    return output;
  }

  static void populate_metrics(
      WebSocketMetrics &metrics)
  {
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
  }

  static bool contains_line(
      const std::string &output,
      std::string_view line)
  {
    return output.find(
               std::string{line} + "\n") !=
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

  static std::vector<std::string>
  split_lines(
      const std::string &value)
  {
    std::vector<std::string> lines;

    std::size_t start = 0u;

    while (start < value.size())
    {
      const std::size_t end =
          value.find('\n', start);

      if (end == std::string::npos)
      {
        lines.emplace_back(
            value.substr(start));

        break;
      }

      lines.emplace_back(
          value.substr(
              start,
              end - start));

      start = end + 1u;
    }

    return lines;
  }

  static std::unordered_map<
      std::string,
      std::string>
  parse_samples(
      const std::string &output)
  {
    std::unordered_map<
        std::string,
        std::string>
        samples;

    for (const std::string &line :
         split_lines(output))
    {
      if (line.empty() ||
          line.front() == '#')
      {
        continue;
      }

      const std::size_t separator =
          line.find(' ');

      assert(
          separator !=
          std::string::npos);

      const std::string name =
          line.substr(
              0u,
              separator);

      const std::string value =
          line.substr(
              separator + 1u);

      const auto [_, inserted] =
          samples.emplace(
              name,
              value);

      assert(inserted == true);
    }

    return samples;
  }

  static void test_default_output_matches_exact_format()
  {
    const WebSocketMetrics metrics;

    assert(
        metrics.render_prometheus() ==
        expected_default_output());
  }

  static void test_populated_output_matches_exact_format()
  {
    WebSocketMetrics metrics;

    populate_metrics(metrics);

    assert(
        metrics.render_prometheus() ==
        expected_populated_output());
  }

  static void test_output_ends_with_newline()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(!output.empty());
    assert(output.back() == '\n');
  }

  static void test_output_does_not_use_carriage_returns()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        output.find('\r') ==
        std::string::npos);
  }

  static void test_output_contains_eleven_samples()
  {
    const WebSocketMetrics metrics;

    const auto samples =
        parse_samples(
            metrics.render_prometheus());

    assert(samples.size() == 11u);
  }

  static void test_sample_names_are_exact()
  {
    const WebSocketMetrics metrics;

    const auto samples =
        parse_samples(
            metrics.render_prometheus());

    assert(
        samples.contains(
            "vix_ws_connections_total"));

    assert(
        samples.contains(
            "vix_ws_connections_active"));

    assert(
        samples.contains(
            "vix_ws_messages_in_total"));

    assert(
        samples.contains(
            "vix_ws_messages_out_total"));

    assert(
        samples.contains(
            "vix_ws_errors_total"));

    assert(
        samples.contains(
            "vix_ws_lp_sessions_total"));

    assert(
        samples.contains(
            "vix_ws_lp_sessions_active"));

    assert(
        samples.contains(
            "vix_ws_lp_polls_total"));

    assert(
        samples.contains(
            "vix_ws_lp_messages_buffered"));

    assert(
        samples.contains(
            "vix_ws_lp_messages_enqueued_total"));

    assert(
        samples.contains(
            "vix_ws_lp_messages_drained_total"));
  }

  static void test_populated_sample_values_are_exact()
  {
    WebSocketMetrics metrics;

    populate_metrics(metrics);

    const auto samples =
        parse_samples(
            metrics.render_prometheus());

    assert(
        samples.at(
            "vix_ws_connections_total") ==
        "100");

    assert(
        samples.at(
            "vix_ws_connections_active") ==
        "12");

    assert(
        samples.at(
            "vix_ws_messages_in_total") ==
        "250");

    assert(
        samples.at(
            "vix_ws_messages_out_total") ==
        "175");

    assert(
        samples.at(
            "vix_ws_errors_total") ==
        "7");

    assert(
        samples.at(
            "vix_ws_lp_sessions_total") ==
        "20");

    assert(
        samples.at(
            "vix_ws_lp_sessions_active") ==
        "4");

    assert(
        samples.at(
            "vix_ws_lp_polls_total") ==
        "300");

    assert(
        samples.at(
            "vix_ws_lp_messages_buffered") ==
        "15");

    assert(
        samples.at(
            "vix_ws_lp_messages_enqueued_total") ==
        "500");

    assert(
        samples.at(
            "vix_ws_lp_messages_drained_total") ==
        "485");
  }

  static void test_help_lines_are_rendered_once()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        count_occurrences(
            output,
            "# HELP vix_ws_") ==
        11u);
  }

  static void test_type_lines_are_rendered_once()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        count_occurrences(
            output,
            "# TYPE vix_ws_") ==
        11u);
  }

  static void test_counter_types_are_correct()
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
            "# TYPE vix_ws_lp_polls_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_enqueued_total counter"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_drained_total counter"));
  }

  static void test_gauge_types_are_correct()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_connections_active gauge"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_sessions_active gauge"));

    assert(
        contains_line(
            output,
            "# TYPE vix_ws_lp_messages_buffered gauge"));
  }

  static void test_metadata_precedes_each_sample()
  {
    const WebSocketMetrics metrics;

    const std::vector<std::string> names{
        "vix_ws_connections_total",
        "vix_ws_connections_active",
        "vix_ws_messages_in_total",
        "vix_ws_messages_out_total",
        "vix_ws_errors_total",
        "vix_ws_lp_sessions_total",
        "vix_ws_lp_sessions_active",
        "vix_ws_lp_polls_total",
        "vix_ws_lp_messages_buffered",
        "vix_ws_lp_messages_enqueued_total",
        "vix_ws_lp_messages_drained_total"};

    const std::string output =
        metrics.render_prometheus();

    for (const std::string &name : names)
    {
      const std::size_t helpPosition =
          output.find(
              "# HELP " + name + " ");

      const std::size_t typePosition =
          output.find(
              "# TYPE " + name + " ");

      const std::size_t samplePosition =
          output.find(
              "\n" + name + " ");

      assert(
          helpPosition !=
          std::string::npos);

      assert(
          typePosition !=
          std::string::npos);

      assert(
          samplePosition !=
          std::string::npos);

      assert(helpPosition < typePosition);
      assert(typePosition < samplePosition);
    }
  }

  static void test_metric_order_is_stable()
  {
    const WebSocketMetrics metrics;

    const std::vector<std::string> names{
        "vix_ws_connections_total ",
        "vix_ws_connections_active ",
        "vix_ws_messages_in_total ",
        "vix_ws_messages_out_total ",
        "vix_ws_errors_total ",
        "vix_ws_lp_sessions_total ",
        "vix_ws_lp_sessions_active ",
        "vix_ws_lp_polls_total ",
        "vix_ws_lp_messages_buffered ",
        "vix_ws_lp_messages_enqueued_total ",
        "vix_ws_lp_messages_drained_total "};

    const std::string output =
        metrics.render_prometheus();

    std::size_t previousPosition = 0u;

    for (std::size_t index = 0u;
         index < names.size();
         ++index)
    {
      const std::string sample =
          "\n" + names[index];

      const std::size_t position =
          output.find(sample);

      assert(
          position !=
          std::string::npos);

      if (index > 0u)
      {
        assert(position > previousPosition);
      }

      previousPosition = position;
    }
  }

  static void test_render_reflects_counter_changes()
  {
    WebSocketMetrics metrics;

    assert(
        contains_line(
            metrics.render_prometheus(),
            "vix_ws_messages_in_total 0"));

    metrics.messages_in_total.store(
        42u,
        std::memory_order_relaxed);

    assert(
        contains_line(
            metrics.render_prometheus(),
            "vix_ws_messages_in_total 42"));

    metrics.messages_in_total.fetch_add(
        8u,
        std::memory_order_relaxed);

    assert(
        contains_line(
            metrics.render_prometheus(),
            "vix_ws_messages_in_total 50"));
  }

  static void test_render_does_not_modify_counters()
  {
    WebSocketMetrics metrics;

    populate_metrics(metrics);

    const auto connectionsTotal =
        metrics.connections_total.load();

    const auto connectionsActive =
        metrics.connections_active.load();

    const auto messagesIn =
        metrics.messages_in_total.load();

    const auto messagesOut =
        metrics.messages_out_total.load();

    const auto errors =
        metrics.errors_total.load();

    const auto sessionsTotal =
        metrics.lp_sessions_total.load();

    const auto sessionsActive =
        metrics.lp_sessions_active.load();

    const auto polls =
        metrics.lp_polls_total.load();

    const auto buffered =
        metrics.lp_messages_buffered.load();

    const auto enqueued =
        metrics.lp_messages_enqueued_total.load();

    const auto drained =
        metrics.lp_messages_drained_total.load();

    const std::string output =
        metrics.render_prometheus();

    assert(!output.empty());

    assert(
        metrics.connections_total.load() ==
        connectionsTotal);

    assert(
        metrics.connections_active.load() ==
        connectionsActive);

    assert(
        metrics.messages_in_total.load() ==
        messagesIn);

    assert(
        metrics.messages_out_total.load() ==
        messagesOut);

    assert(
        metrics.errors_total.load() ==
        errors);

    assert(
        metrics.lp_sessions_total.load() ==
        sessionsTotal);

    assert(
        metrics.lp_sessions_active.load() ==
        sessionsActive);

    assert(
        metrics.lp_polls_total.load() ==
        polls);

    assert(
        metrics.lp_messages_buffered.load() ==
        buffered);

    assert(
        metrics.lp_messages_enqueued_total.load() ==
        enqueued);

    assert(
        metrics.lp_messages_drained_total.load() ==
        drained);
  }

  static void test_repeated_render_is_deterministic()
  {
    WebSocketMetrics metrics;

    populate_metrics(metrics);

    const std::string first =
        metrics.render_prometheus();

    const std::string second =
        metrics.render_prometheus();

    const std::string third =
        metrics.render_prometheus();

    assert(first == second);
    assert(second == third);
  }

  static void test_output_contains_no_labels()
  {
    const WebSocketMetrics metrics;

    const std::string output =
        metrics.render_prometheus();

    assert(
        output.find('{') ==
        std::string::npos);

    assert(
        output.find('}') ==
        std::string::npos);
  }

  static void test_output_contains_no_timestamps()
  {
    const WebSocketMetrics metrics;

    for (const auto &[name, value] :
         parse_samples(
             metrics.render_prometheus()))
    {
      (void)name;

      assert(
          value.find(' ') ==
          std::string::npos);
    }
  }

} // namespace

int main()
{
  test_default_output_matches_exact_format();
  test_populated_output_matches_exact_format();

  test_output_ends_with_newline();
  test_output_does_not_use_carriage_returns();

  test_output_contains_eleven_samples();
  test_sample_names_are_exact();
  test_populated_sample_values_are_exact();

  test_help_lines_are_rendered_once();
  test_type_lines_are_rendered_once();

  test_counter_types_are_correct();
  test_gauge_types_are_correct();

  test_metadata_precedes_each_sample();
  test_metric_order_is_stable();

  test_render_reflects_counter_changes();
  test_render_does_not_modify_counters();
  test_repeated_render_is_deterministic();

  test_output_contains_no_labels();
  test_output_contains_no_timestamps();

  return 0;
}
