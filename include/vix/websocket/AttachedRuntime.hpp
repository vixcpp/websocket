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

#include <vix/app/App.hpp>
#include <vix/websocket/server.hpp>
#include <vix/config/Config.hpp>
#include <vix/utils/ServerPrettyLogs.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>

// WebSocket OpenAPI docs (router-based)
#include <vix/websocket/openapi_docs.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>
#include <mutex>

namespace vix::websocket
{
  class AttachedRuntime
  {
  public:
    AttachedRuntime(vix::App &app, vix::websocket::Server &ws)
        : app_(app), ws_(ws), wsThread_(), stopped_(false)
    {
      wsThread_ = std::thread([this]()
                              {
        vix::utils::console_wait_banner();
        ws_.start();

        while (!stopped_.load(std::memory_order_relaxed))
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        } });

      app_.set_shutdown_callback([this]()
                                 { stop(); });
    }

    ~AttachedRuntime()
    {
      stop();
    }

    AttachedRuntime(const AttachedRuntime &) = delete;
    AttachedRuntime &operator=(const AttachedRuntime &) = delete;
    AttachedRuntime(AttachedRuntime &&) = delete;
    AttachedRuntime &operator=(AttachedRuntime &&) = delete;

    void stop() noexcept
    {
      bool expected = false;
      if (!stopped_.compare_exchange_strong(expected, true))
        return;

      ws_.stop();

      if (wsThread_.joinable())
        wsThread_.join();
    }

  private:
    vix::App &app_;
    vix::websocket::Server &ws_;
    std::thread wsThread_;
    std::atomic<bool> stopped_;
  };

} // namespace vix::websocket

namespace vix
{
  inline void register_ws_openapi_docs_once()
  {
    static std::once_flag once;
    std::call_once(once, []()
                   { vix::websocket::openapi::register_ws_docs(
                         "/ws",
                         "/ws/poll",
                         "/ws/send",
                         "/metrics"); });
  }

  inline void run_http_and_ws(vix::App &app, vix::websocket::Server &ws, int port = 8080)
  {
    // Register docs once (global registry). No router needed.
    register_ws_openapi_docs_once();

    vix::websocket::AttachedRuntime runtime{app, ws};

    app.listen(port, [&](const vix::utils::ServerReadyInfo &base)
               {
                 vix::utils::ServerReadyInfo info = base;
                 info.show_ws = true;
                 info.ws_scheme = "ws";
                 info.ws_host = "localhost";
                 info.ws_port = ws.port();
                 info.ws_path = "/";
                 vix::utils::RuntimeBanner::emit_server_ready(info); });

    app.wait();
    app.close();
  }

  template <typename ConfigureFn>
  inline void serve_http_and_ws(
      const std::filesystem::path &configPath,
      int port,
      ConfigureFn &&fn)
  {
    // Register docs once (global registry). No router needed.
    register_ws_openapi_docs_once();

    auto &cfg = vix::config::Config::getInstance(configPath);

    auto exec_unique = vix::experimental::make_threadpool_executor(4, 8, 0);
    std::shared_ptr<vix::executor::IExecutor> exec_shared{std::move(exec_unique)};

    vix::App app{exec_shared};
    vix::websocket::Server ws{cfg, exec_shared};

    fn(app, ws);

    run_http_and_ws(app, ws, port);
  }

  template <typename ConfigureFn>
  inline void serve_http_and_ws(ConfigureFn &&fn)
  {
    serve_http_and_ws("config/config.json", 8080, std::forward<ConfigureFn>(fn));
  }

} // namespace vix

#endif
