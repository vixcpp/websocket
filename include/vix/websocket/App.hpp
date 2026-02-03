/**
 *
 * @file App.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
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

  /**
   * @brief High-level WebSocket application wrapper.
   *
   * Manages configuration, thread pool, routes, and the underlying WebSocket server.
   */
  class App
  {
  public:
    /**
     * @brief Typed WebSocket message handler.
     */
    using TypedHandler = std::function<void(
        Session &,
        const std::string &type,
        const vix::json::kvs &payload)>;

    /**
     * @brief Construct a WebSocket app.
     *
     * @param configPath Path to the configuration file.
     * @param minThreads Minimum number of worker threads.
     * @param maxThreads Maximum number of worker threads.
     * @param defaultPrio Default task priority.
     */
    App(const std::string &configPath,
        std::size_t minThreads = 4,
        std::size_t maxThreads = 8,
        int defaultPrio = 0);

    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;

    /**
     * @brief Register a WebSocket endpoint with a typed handler.
     */
    [[nodiscard]] App &ws(const std::string &endpoint, TypedHandler handler);

    /**
     * @brief Run the WebSocket server in blocking mode.
     */
    void run_blocking();

    /**
     * @brief Stop the WebSocket server.
     */
    void stop();

    /** @brief Access the underlying WebSocket server. */
    [[nodiscard]] Server &server() noexcept { return server_; }

    /** @brief Access the application configuration. */
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

#endif // VIX_WEBSOCKET_APP_HPP
