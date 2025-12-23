#pragma once

#include <vix/app/App.hpp>
#include <vix/websocket/server.hpp>
#include <vix/config/Config.hpp>
#include <vix/utils/ServerPrettyLogs.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>

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
                ws_.start(); 
                // Keep thread alive until stop() is called
                while (!stopped_.load(std::memory_order_relaxed))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                } });

            // Ensure WS stops when HTTP app is shutting down
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

            // Stop WS (async stop + join inside)
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
    struct HttpAndWsBundle
    {
        vix::App app;
        vix::websocket::Server ws;
    };

    inline HttpAndWsBundle make_http_and_ws(const std::filesystem::path &configPath)
    {
        auto &cfg = vix::config::Config::getInstance(configPath);

        auto exec_unique = vix::experimental::make_threadpool_executor(
            4, // minThreads
            8, // maxThreads
            0  // default priority
        );

        std::shared_ptr<vix::executor::IExecutor> exec_shared{std::move(exec_unique)};

        return HttpAndWsBundle{
            vix::App{exec_shared},
            vix::websocket::Server{cfg, exec_shared}};
    }

    inline void run_http_and_ws(vix::App &app,
                                vix::websocket::Server &ws,
                                int port = 8080)
    {
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
    inline void serve_http_and_ws(const std::filesystem::path &configPath,
                                  int port,
                                  ConfigureFn &&fn)
    {
        auto bundle = make_http_and_ws(configPath);

        auto &app = bundle.app;
        auto &ws = bundle.ws;

        fn(app, ws);

        run_http_and_ws(app, ws, port);
    }

    template <typename ConfigureFn>
    inline void serve_http_and_ws(ConfigureFn &&fn)
    {
        serve_http_and_ws("config/config.json",
                          8080,
                          std::forward<ConfigureFn>(fn));
    }

} // namespace vix
