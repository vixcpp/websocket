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
#include <filesystem>
#include <memory>
#include <mutex>
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
   * Starts the WebSocket server immediately and coordinates shutdown with the attached HTTP
   * application.
   *
   * Important lifecycle rule:
   * - The HTTP shutdown callback only requests an asynchronous stop.
   * - The final blocking shutdown is executed from an external safe control path.
   */
  class AttachedRuntime
  {
  public:
    /**
     * @brief Attach a WebSocket server to an existing HTTP app.
     *
     * Starts the WebSocket server immediately and registers an app shutdown
     * callback that requests a non-blocking WebSocket stop.
     *
     * @param app HTTP application instance.
     * @param ws WebSocket server instance.
     * @param exec Shared runtime executor.
     */
    AttachedRuntime(
        vix::App &app,
        vix::websocket::Server &ws,
        std::shared_ptr<vix::executor::RuntimeExecutor> exec)
        : app_(app),
          ws_(ws),
          exec_(std::move(exec)),
          state_(std::make_shared<State>())
    {
      vix::utils::console_wait_banner();
      ws_.start();

      auto state = state_;
      auto *wsPtr = &ws_;

      app_.set_shutdown_callback(
          [state, wsPtr]()
          {
            if (!state || !wsPtr)
            {
              return;
            }

            bool expected = false;
            if (!state->stop_requested.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
              return;
            }

            try
            {
              wsPtr->stop_async();
            }
            catch (...)
            {
            }
          });
    }

    /**
     * @brief Finalize shutdown if still needed.
     *
     * This destructor is defensive and idempotent.
     */
    ~AttachedRuntime() noexcept
    {
      finalize_shutdown();
    }

    AttachedRuntime(const AttachedRuntime &) = delete;
    AttachedRuntime &operator=(const AttachedRuntime &) = delete;
    AttachedRuntime(AttachedRuntime &&) = delete;
    AttachedRuntime &operator=(AttachedRuntime &&) = delete;

    /**
     * @brief Request an asynchronous WebSocket shutdown.
     *
     * This function is idempotent and intentionally non-blocking.
     * It is safe to call from a shutdown callback running inside a server
     * worker thread.
     */
    void request_stop() noexcept
    {
      if (!state_)
      {
        return;
      }

      bool expected = false;
      if (!state_->stop_requested.compare_exchange_strong(
              expected,
              true,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
      {
        return;
      }

      try
      {
        ws_.stop_async();
      }
      catch (...)
      {
      }
    }

    /**
     * @brief Perform the final blocking shutdown exactly once.
     *
     * Order matters:
     * 1. stop websocket blocking
     * 2. stop shared executor blocking
     */
    void finalize_shutdown() noexcept
    {
      if (!state_)
      {
        return;
      }

      std::lock_guard<std::mutex> lock(state_->finalize_mutex);

      if (state_->finalized.exchange(true, std::memory_order_acq_rel))
      {
        return;
      }

      try
      {
        ws_.stop();
      }
      catch (...)
      {
      }

      try
      {
        if (exec_)
        {
          exec_->stop();
        }
      }
      catch (...)
      {
      }
    }

  private:
    /**
     * @brief Shared shutdown state captured by callbacks.
     *
     * This avoids capturing the AttachedRuntime instance directly inside
     * the HTTP shutdown callback.
     */
    struct State
    {
      /** @brief Idempotent asynchronous stop flag. */
      std::atomic<bool> stop_requested{false};

      /** @brief Ensures final shutdown happens once. */
      std::atomic<bool> finalized{false};

      /** @brief Protects final shutdown sequence. */
      std::mutex finalize_mutex{};
    };

  private:
    /** @brief Attached HTTP application. */
    vix::App &app_;

    /** @brief Attached WebSocket server. */
    vix::websocket::Server &ws_;

    /** @brief Shared runtime executor. */
    std::shared_ptr<vix::executor::RuntimeExecutor> exec_;

    /** @brief Shared shutdown state used by callbacks and finalization. */
    std::shared_ptr<State> state_;
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
   * Registers WebSocket OpenAPI docs once, starts the attached WebSocket
   * runtime, then starts the HTTP server and waits until shutdown.
   *
   * @param app HTTP application instance.
   * @param ws WebSocket server instance.
   * @param exec Shared runtime executor.
   * @param port HTTP listening port.
   */
  inline void run_http_and_ws(
      vix::App &app,
      vix::websocket::Server &ws,
      std::shared_ptr<vix::executor::RuntimeExecutor> exec,
      int port = 8080)
  {
    register_ws_openapi_docs_once();

    auto r = app.router();
    if (r)
    {
      vix::openapi::register_openapi_and_docs(*r, "Vix API", "1.33.0");
    }

    vix::websocket::AttachedRuntime runtime{app, ws, exec};

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

    runtime.finalize_shutdown();
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

    run_http_and_ws(app, ws, exec, port);
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

#endif // VIX_ATTACHED_RUNTIME_HPP
