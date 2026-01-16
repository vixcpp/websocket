/**
 *
 *  @file App.hpp
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
#ifndef VIX_WEBSOCKET_APP_HPP
#define VIX_WEBSOCKET_APP_HPP

#include <vix/websocket/server.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/config/Config.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vix::websocket
{
  class Session;

  class App
  {
  public:
    using TypedHandler = std::function<void(
        Session &,
        const std::string &type,
        const vix::json::kvs &payload)>;

    App(const std::string &configPath,
        std::size_t minThreads = 4,
        std::size_t maxThreads = 8,
        int defaultPrio = 0);

    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;
    [[nodiscard]] App &ws(const std::string &endpoint, TypedHandler handler);
    void run_blocking();
    void stop();
    [[nodiscard]] Server &server() noexcept { return server_; }
    [[nodiscard]] vix::config::Config &config() noexcept { return config_; }

  private:
    struct Route
    {
      std::string endpoint;
      TypedHandler handler;
    };

    vix::config::Config config_;
    std::shared_ptr<vix::executor::IExecutor> executor_;
    Server server_;
    std::vector<Route> routes_;
    void install_dispatcher();
  };

} // namespace vix::websocket

#endif
