#ifndef VIX_WEBSOCKET_CONFIG_HPP
#define VIX_WEBSOCKET_CONFIG_HPP

/**
 * @file config.hpp
 * @brief WebSocket-specific configuration for Vix.cpp.
 *
 * @details
 * Wraps the core `vix::config::Config` into a strongly-typed structure used
 * by the WebSocket server and sessions. Keeps all WS-related knobs in one
 * place instead of scattering literals across the codebase.
 */

#include <cstddef>
#include <chrono>

#include <vix/config/Config.hpp>

namespace vix::websocket
{
    /**
     * @struct Config
     * @brief Tunables controlling WebSocket behaviour.
     */
    struct Config
    {
        /// Maximum accepted payload size in bytes (soft limit).
        std::size_t maxMessageSize = 64 * 1024; // 64 KiB

        /// Idle timeout after which an inactive connection is closed.
        std::chrono::seconds idleTimeout{60};

        /// Enable permessage-deflate compression if client supports it.
        bool enablePerMessageDeflate = true;

        /// Allow automatic ping/pong management by Beast.
        bool autoPingPong = true;

        /// Interval between server-initiated pings (0 = disabled).
        std::chrono::seconds pingInterval{30};

        /**
         * @brief Build a Config from the core Vix config.
         *
         * Expected keys (optional):
         *  - websocket.max_message_size (int, bytes)
         *  - websocket.idle_timeout     (int, seconds)
         *  - websocket.enable_deflate   (bool)
         *  - websocket.ping_interval    (int, seconds)
         */
        static Config from_core(const vix::config::Config &core);
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_CONFIG_HPP
