/**
 *
 *  @file Metrics.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_WEBSOCKET_METRICS_HPP
#define VIX_WEBSOCKET_METRICS_HPP

#include <atomic>
#include <cstdint>
#include <string>

/**
 * @file Metrics.hpp
 * @brief Lightweight Prometheus-style metrics for the Vix WebSocket module.
 *
 * Defines canonical counters/gauges shared by WebSocket server/client components
 * and long-polling bridge, with a helper to render Prometheus text exposition.
 */

namespace vix::websocket
{
  /**
   * @brief Prometheus-style counters and gauges for WebSocket + long-polling.
   *
   * Counters are monotonically increasing totals. Gauges reflect current state.
   * All fields are atomic to allow updates across threads.
   */
  struct WebSocketMetrics
  {
    /** @brief Total number of accepted WS connections since start (counter). */
    std::atomic<std::uint64_t> connections_total{0};
    /** @brief Current number of active WS connections (gauge). */
    std::atomic<std::uint64_t> connections_active{0};

    /** @brief Total inbound messages processed (counter). */
    std::atomic<std::uint64_t> messages_in_total{0};
    /** @brief Total outbound messages sent/broadcast (counter). */
    std::atomic<std::uint64_t> messages_out_total{0};

    /** @brief Total errors observed (counter). */
    std::atomic<std::uint64_t> errors_total{0};

    /** @brief Total long-polling sessions created (counter). */
    std::atomic<std::uint64_t> lp_sessions_total{0};
    /** @brief Current number of active long-polling sessions (gauge). */
    std::atomic<std::uint64_t> lp_sessions_active{0};

    /** @brief Total long-polling poll calls served (counter). */
    std::atomic<std::uint64_t> lp_polls_total{0};

    /** @brief Current number of buffered messages across sessions (gauge-like). */
    std::atomic<std::uint64_t> lp_messages_buffered{0};
    /** @brief Total messages enqueued into LP buffers (counter). */
    std::atomic<std::uint64_t> lp_messages_enqueued_total{0};
    /** @brief Total messages drained from LP buffers (counter). */
    std::atomic<std::uint64_t> lp_messages_drained_total{0};

    /**
     * @brief Render metrics in Prometheus text exposition format.
     */
    [[nodiscard]] std::string render_prometheus() const;
  };

  /**
   * @brief Run a minimal HTTP exporter for metrics (blocking).
   *
   * Exposes an HTTP endpoint that returns metrics.render_prometheus().
   *
   * @param metrics Metrics instance to export.
   * @param address Bind address (default 0.0.0.0).
   * @param port Bind port (default 9100).
   */
  void run_metrics_http_exporter(
      WebSocketMetrics &metrics,
      const std::string &address = "0.0.0.0",
      std::uint16_t port = 9100);

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_METRICS_HPP
