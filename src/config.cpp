/**
 *
 *  @file config.cpp
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
#include <vix/websocket/config.hpp>

#include <algorithm>
#include <cstdint>

namespace vix::websocket
{
  Config Config::from_core(const vix::config::Config &core)
  {
    Config cfg;

    {
      const int value = core.getInt(
          "websocket.max_message_size",
          static_cast<int>(cfg.maxMessageSize));

      cfg.maxMessageSize = static_cast<std::size_t>(std::max(1024, value));
    }

    {
      const int value = core.getInt(
          "websocket.idle_timeout",
          static_cast<int>(cfg.idleTimeout.count()));

      if (value <= 0)
      {
        cfg.idleTimeout = std::chrono::seconds::zero();
      }
      else
      {
        cfg.idleTimeout = std::chrono::seconds{value};
      }
    }

    cfg.enablePerMessageDeflate =
        core.getBool("websocket.enable_deflate", cfg.enablePerMessageDeflate);

    {
      const int value = core.getInt(
          "websocket.ping_interval",
          static_cast<int>(cfg.pingInterval.count()));

      if (value <= 0)
      {
        cfg.pingInterval = std::chrono::seconds::zero();
      }
      else
      {
        cfg.pingInterval = std::chrono::seconds{value};
      }
    }

    cfg.autoPingPong =
        core.getBool("websocket.auto_ping_pong", cfg.autoPingPong);

    return cfg;
  }

} // namespace vix::websocket
