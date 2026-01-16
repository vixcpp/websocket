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
 * This header provides a minimal, self-contained metrics structure that can be
 * used by WebSocket servers and clients to expose runtime statistics in a
 * Prometheus-compatible text format.
 *
 * The primary goals are:
 *   - Keep metrics opt-in and lightweight.
 *   - Avoid coupling the core WebSocket API to any specific monitoring stack.
 *   - Provide a canonical place for counters used across examples and apps.
 *
 * Typical usage
 * -------------
 * @code{.cpp}
 * using vix::websocket::WebSocketMetrics;
 *
 * WebSocketMetrics metrics;
 *
 * // in your WebSocket server:
 * ws.on_open([&](auto&){ metrics.connections_total++; metrics.connections_active++; });
 * ws.on_close([&](auto&){ metrics.connections_active--; });
 *
 * ws.on_typed_message([&](auto&, auto&, auto&){
 *     metrics.messages_in_total++;
 *     // ...
 * });
 *
 * // before broadcasting:
 * metrics.messages_out_total++;
 *
 * // export via HTTP endpoint:
 * std::thread([&]{
 *     vix::websocket::run_metrics_http_exporter(metrics, "0.0.0.0", 9100);
 * }).detach();
 * @endcode
 */

namespace vix::websocket
{
  struct WebSocketMetrics
  {
    std::atomic<std::uint64_t> connections_total{0};
    std::atomic<std::uint64_t> connections_active{0};
    std::atomic<std::uint64_t> messages_in_total{0};
    std::atomic<std::uint64_t> messages_out_total{0};
    std::atomic<std::uint64_t> errors_total{0};
    std::atomic<std::uint64_t> lp_sessions_total{0};
    std::atomic<std::uint64_t> lp_sessions_active{0};
    std::atomic<std::uint64_t> lp_polls_total{0};
    std::atomic<std::uint64_t> lp_messages_buffered{0};
    std::atomic<std::uint64_t> lp_messages_enqueued_total{0};
    std::atomic<std::uint64_t> lp_messages_drained_total{0};
    [[nodiscard]] std::string render_prometheus() const;
  };

  void run_metrics_http_exporter(
      WebSocketMetrics &metrics,
      const std::string &address = "0.0.0.0",
      std::uint16_t port = 9100);

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_METRICS_HPP
