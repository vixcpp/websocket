#ifndef VIX_WEBSOCKET_METRICS_HPP
#define VIX_WEBSOCKET_METRICS_HPP

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

#include <atomic>
#include <cstdint>
#include <string>

namespace vix::websocket
{
    /**
     * @struct WebSocketMetrics
     * @brief Aggregated counters for WebSocket activity.
     *
     * All fields are 64-bit atomics and can be safely incremented from multiple
     * threads without external synchronization.
     *
     * The `render_prometheus()` member produces a textual representation of all
     * counters using Prometheus' "text exposition format" (v0.0.4).
     */
    struct WebSocketMetrics
    {
        std::atomic<std::uint64_t> connections_total{0};
        std::atomic<std::uint64_t> connections_active{0};
        std::atomic<std::uint64_t> messages_in_total{0};
        std::atomic<std::uint64_t> messages_out_total{0};
        std::atomic<std::uint64_t> errors_total{0};

        /**
         * @brief Render all counters in Prometheus text format.
         *
         * This produces a single string containing HELP/TYPE metadata and one
         * sample per metric. It is intended to be served as
         * "text/plain; version=0.0.4".
         */
        [[nodiscard]] std::string render_prometheus() const;
    };

    /**
     * @brief Run a minimal HTTP server exposing `/metrics` for Prometheus.
     *
     * This helper starts a blocking HTTP loop on the given address and port.
     * For each incoming GET request on `/metrics`, it responds with the output
     * of `metrics.render_prometheus()`. Any other path returns 404.
     *
     * Typical usage is to spawn it on a dedicated thread:
     * @code{.cpp}
     * WebSocketMetrics metrics;
     *
     * std::thread([&]{
     *     vix::websocket::run_metrics_http_exporter(metrics, "0.0.0.0", 9100);
     * }).detach();
     * @endcode
     *
     * @param metrics  Shared metrics instance to read counters from.
     * @param address  Address to bind (e.g. "0.0.0.0").
     * @param port     TCP port to listen on (e.g. 9100).
     */
    void run_metrics_http_exporter(WebSocketMetrics &metrics,
                                   const std::string &address = "0.0.0.0",
                                   std::uint16_t port = 9100);

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_METRICS_HPP
