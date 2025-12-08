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
     * @brief Aggregated counters for WebSocket + Long-Polling activity.
     */
    struct WebSocketMetrics
    {
        // ───── Core WebSocket metrics ─────
        std::atomic<std::uint64_t> connections_total{0};
        std::atomic<std::uint64_t> connections_active{0};
        std::atomic<std::uint64_t> messages_in_total{0};
        std::atomic<std::uint64_t> messages_out_total{0};
        std::atomic<std::uint64_t> errors_total{0};

        // ───── Long-polling fallback metrics ─────
        // Total sessions ever created
        std::atomic<std::uint64_t> lp_sessions_total{0};

        // Sessions currently considered active (not encore expirées)
        std::atomic<std::uint64_t> lp_sessions_active{0};

        // Total HTTP /ws/poll calls
        std::atomic<std::uint64_t> lp_polls_total{0};

        // Nb de messages actuellement bufferisés dans LongPollingManager
        std::atomic<std::uint64_t> lp_messages_buffered{0};

        // Total de messages enqueued dans le buffer LP
        std::atomic<std::uint64_t> lp_messages_enqueued_total{0};

        // Total de messages drainés via /ws/poll
        std::atomic<std::uint64_t> lp_messages_drained_total{0};

        /**
         * @brief Render all counters in Prometheus text format.
         */
        [[nodiscard]] std::string render_prometheus() const;
    };

    /**
     * @brief Run a minimal HTTP server exposing `/metrics` for Prometheus.
     */
    void run_metrics_http_exporter(WebSocketMetrics &metrics,
                                   const std::string &address = "0.0.0.0",
                                   std::uint16_t port = 9100);

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_METRICS_HPP
