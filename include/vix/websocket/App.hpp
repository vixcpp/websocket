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

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/json/Simple.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/server.hpp>

namespace vix::websocket
{

  class Session;

  /**
   * @brief High-level WebSocket application wrapper.
   *
   * Manages configuration, runtime executor, routes, and the underlying
   * WebSocket server.
   *
   * This version is aligned with the Vix runtime architecture and no longer
   * depends on the legacy threadpool-based executor abstraction.
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
     * @param executor Shared runtime executor used by the WebSocket stack.
     */
    App(
        const std::string &configPath,
        std::shared_ptr<vix::executor::RuntimeExecutor> executor);

    ~App() noexcept;

    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;

    /**
     * @brief Register a WebSocket endpoint with a typed handler.
     *
     * @param endpoint Logical endpoint key.
     * @param handler Typed message handler associated with the endpoint.
     * @return Current app instance.
     */
    [[nodiscard]] App &ws(const std::string &endpoint, TypedHandler handler);

    /**
     * @brief Run the WebSocket server in blocking mode.
     */
    void run_blocking();

    /**
     * @brief Stop the WebSocket app and shared executor.
     *
     * Safe to call multiple times.
     */
    void stop() noexcept;

    /**
     * @brief Access the underlying WebSocket server.
     *
     * @return Server reference.
     */
    [[nodiscard]] Server &server() noexcept
    {
      return server_;
    }

    /**
     * @brief Access the application configuration.
     *
     * @return Configuration reference.
     */
    [[nodiscard]] vix::config::Config &config() noexcept
    {
      return config_;
    }

    /**
     * @brief Access the runtime executor used by the app.
     *
     * @return Shared runtime executor.
     */
    [[nodiscard]] std::shared_ptr<vix::executor::RuntimeExecutor> executor() noexcept
    {
      return executor_;
    }

  private:
    /**
     * @brief Registered typed WebSocket route.
     */
    struct Route
    {
      std::string endpoint;
      TypedHandler handler;
    };

    /**
     * @brief Install the internal typed-message dispatcher.
     */
    void install_dispatcher();

    /**
     * @brief Dispatch one typed message to all registered handlers.
     */
    void dispatch_typed_message(
        Session &session,
        const std::string &type,
        const vix::json::kvs &payload);

  private:
    /** @brief Application configuration source. */
    vix::config::Config config_;

    /** @brief Shared runtime executor used by this app. */
    std::shared_ptr<vix::executor::RuntimeExecutor> executor_;

    /** @brief Underlying high-level WebSocket server. */
    Server server_;

    /** @brief Registered typed routes. */
    std::vector<Route> routes_;

    /** @brief Protects stop() from concurrent/double shutdown. */
    mutable std::mutex stopMutex_;

    /** @brief Indicates whether shutdown was already requested. */
    std::atomic<bool> stopped_{false};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_APP_HPP
