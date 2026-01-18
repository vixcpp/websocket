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
  struct Config
  {
    std::size_t maxMessageSize = 64 * 1024; // 64 KiB
    std::chrono::seconds idleTimeout{60};
    bool enablePerMessageDeflate = true;
    bool autoPingPong = true;
    std::chrono::seconds pingInterval{30};
    static Config from_core(const vix::config::Config &core);
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_CONFIG_HPP
