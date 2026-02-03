/**
 *
 *  @file Config.hpp
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

#ifndef VIX_WEBSOCKET_CONFIG_HPP
#define VIX_WEBSOCKET_CONFIG_HPP

#include <cstddef>
#include <chrono>

#include <vix/config/Config.hpp>

namespace vix::websocket
{
  /**
   * @brief Runtime configuration for the WebSocket server.
   *
   * This struct is derived from the core configuration and controls
   * limits, timeouts, and protocol-level behavior of WebSocket connections.
   */
  struct Config
  {
    /** @brief Maximum accepted WebSocket message size in bytes. */
    std::size_t maxMessageSize = 64 * 1024; // 64 KiB

    /** @brief Idle connection timeout before closing. */
    std::chrono::seconds idleTimeout{60};

    /** @brief Enable per-message deflate compression. */
    bool enablePerMessageDeflate = true;

    /** @brief Automatically handle ping/pong frames. */
    bool autoPingPong = true;

    /** @brief Interval at which ping frames are sent. */
    std::chrono::seconds pingInterval{30};

    /**
     * @brief Build a WebSocket config from the core application config.
     */
    static Config from_core(const vix::config::Config &core);
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_CONFIG_HPP
