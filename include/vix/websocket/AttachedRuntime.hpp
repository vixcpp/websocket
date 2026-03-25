/**
 *
 *  @file AttachedRuntime.hpp
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
#ifndef VIX_ATTACHED_RUNTIME_HPP
#define VIX_ATTACHED_RUNTIME_HPP

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <vix/app/App.hpp>
#include <vix/config/Config.hpp>
#include <vix/console.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/openapi/register_docs.hpp>
#include <vix/utils/ServerPrettyLogs.hpp>
#include <vix/websocket/openapi_docs.hpp>
#include <vix/websocket/server.hpp>

namespace vix::websocket
{
  /**
   * @brief Runs a WebSocket server alongside an HTTP app, with shared lifecycle.
   *
   * Starts the WebSocket server on a dedicated thread and stops it when the HTTP
   * app requests shutdown or when this object is destroyed.
   */
  class AttachedRuntime
  {
  public:
    /**
     * @brief Attach a WebSocket server to an existing HTTP app.
     *
     * Starts the WebSocket server in a background thread and registers an app
     * shutdown callback to stop the WebSocket runtime.
     *
     * @param app HTTP application instance.
     * @param ws WebSocket server instance.
     */
    AttachedRuntime(vix::App &app, vix::websocket::Server &ws)
        : app_(app), ws_(ws), wsThread_(), stopped_(false)
    {
      wsThread_ = std::thread(
          [this]()
          {
            vix::utils::console_wait_banner();
            ws_.start();

            while (!stopped_.load(std::memory_order_relaxed))
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
          });

      app_.set_shutdown_callback(
          [this]()
          {
            stop();
          });
    }

    /**
     * @brief Stop the runtime if still running.
     */
    ~AttachedRuntime()
    {
      stop();
    }

    AttachedRuntime(const AttachedRuntime &) = delete;
    AttachedRuntime &operator=(const AttachedRuntime &) = delete;
    AttachedRuntime(AttachedRuntime &&) = delete;
    AttachedRuntime &operator=(AttachedRuntime &&) = delete;

    /**
     * @brief Stop the WebSocket server and join the worker thread.
     *
     * Safe to call multiple times.
     */
    void stop() noexcept
    {
      bool expected = false;
      if (!stopped_.compare_exchange_strong(expected, true))
      {
        return;
      }

      ws_.stop();

      if (wsThread_.joinable())
      {
        wsThread_.join();
      }
    }

  private:
    /** @brief Attached HTTP application. */
    vix::App &app_;

    /** @brief Attached WebSocket server. */
    vix::websocket::Server &ws_;

    /** @brief Background thread driving the WebSocket runtime. */
    std::thread wsThread_;

    /** @brief Idempotent stop flag. */
    std::atomic<bool> stopped_;
  };

} // namespace vix::websocket

namespace vix
{
  /**
   * @brief Register WebSocket OpenAPI documentation endpoints once per process.
   *
   * Uses a global registry and is guarded by std::call_once.
   */
  inline void register_ws_openapi_docs_once()
  {
    static std::once_flag once;
    std::call_once(
        once,
        []()
        {
          vix::websocket::openapi::register_ws_docs(
              "/ws",
              "/ws/poll",
              "/ws/send",
              "/metrics");
        });
  }

  /**
   * @brief Run HTTP and WebSocket servers together in blocking mode.
   *
   * Registers WebSocket OpenAPI docs once, starts the WebSocket runtime,
   * then starts the HTTP server and waits until shutdown.
   *
   * @param app HTTP application instance.
   * @param ws WebSocket server instance.
   * @param port HTTP listening port.
   */
  inline void run_http_and_ws(
      vix::App &app,
      vix::websocket::Server &ws,
      int port = 8080)
  {
    register_ws_openapi_docs_once();

    auto r = app.router();
    if (r)
    {
      vix::openapi::register_openapi_and_docs(*r, "Vix API", "1.33.0");
    }

    vix::websocket::AttachedRuntime runtime{app, ws};

    app.listen(
        port,
        [&]()
        {
          if (!app.has_server_ready_info())
          {
            console.warn("server ready info not available");
            return;
          }

          auto info = app.server_ready_info();
          info.show_ws = true;
          info.ws_scheme = "ws";
          info.ws_host = "localhost";
          info.ws_port = ws.port();
          info.ws_path = "/";

          vix::utils::RuntimeBanner::emit_server_ready(info);
        });

    app.wait();
    app.close();
  }

  /**
   * @brief Build and serve a combined HTTP + WebSocket runtime from a config path.
   *
   * Creates a shared runtime executor, constructs the HTTP app and WebSocket
   * server, lets the caller configure routes and handlers, then runs both
   * servers together.
   *
   * @tparam ConfigureFn Callable type.
   * @param configPath Path to the configuration file.
   * @param port HTTP listening port.
   * @param fn Configuration callback: fn(app, ws).
   */
  template <typename ConfigureFn>
  inline void serve_http_and_ws(
      const std::filesystem::path &configPath,
      int port,
      ConfigureFn &&fn)
  {
    register_ws_openapi_docs_once();

    auto &cfg = vix::config::Config::getInstance(configPath);

    auto exec = std::make_shared<vix::executor::RuntimeExecutor>();

    vix::App app{exec};
    vix::websocket::Server ws{cfg, exec};

    fn(app, ws);

    run_http_and_ws(app, ws, port);
  }

  /**
   * @brief Convenience overload using default config path and port.
   *
   * @tparam ConfigureFn Callable type.
   * @param fn Configuration callback: fn(app, ws).
   */
  template <typename ConfigureFn>
  inline void serve_http_and_ws(ConfigureFn &&fn)
  {
    serve_http_and_ws("config/config.json", 8080, std::forward<ConfigureFn>(fn));
  }

} // namespace vix

#endif
