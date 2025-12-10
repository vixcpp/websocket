#pragma once

#include <vix/app/App.hpp>       // vix::App (HTTP runtime)
#include <vix/websocket.hpp>     // vix::websocket::Server
#include <vix/config/Config.hpp> // vix::config::Config
#include <vix/experimental/ThreadPoolExecutor.hpp>

#include <thread>
#include <atomic>
#include <string>
#include <filesystem>
#include <utility>
#include <tuple>
#include <type_traits>

namespace vix::websocket
{
    /**
     * @brief High-level runtime that attaches a WebSocket server
     *        to a vix::App (HTTP) lifecycle.
     *
     * This runtime ensures that:
     *  - the WebSocket server runs in its own dedicated thread,
     *  - the HTTP application gracefully triggers WebSocket shutdown,
     *  - the WebSocket server is stopped and joined exactly once,
     *    providing predictable and safe cleanup.
     *
     * This class is built using RAII principles: destruction guarantees
     * orderly shutdown of the WebSocket server.
     */
    class AttachedRuntime
    {
    public:
        /**
         * @brief Construct a runtime bound to an existing HTTP App and WebSocket Server.
         *
         * @param app Reference to the vix::App HTTP runtime.
         * @param ws  Reference to the WebSocket server to attach.
         *
         * Immediately:
         *  - launches ws.listen_blocking() in a dedicated thread,
         *  - registers a shutdown callback on the HTTP runtime to stop the WS server.
         */
        AttachedRuntime(vix::App &app, vix::websocket::Server &ws)
            : app_(app), ws_(ws), stopped_(false)
        {
            // 1) Launch the WebSocket server in its own dedicated thread.
            wsThread_ = std::thread([this]()
                                    { ws_.listen_blocking(); });

            // 2) Register a shutdown callback on the HTTP application.
            app_.set_shutdown_callback([this]()
                                       { this->stop(); });
        }

        /**
         * @brief Destructor — guarantees clean shutdown.
         *
         * Ensures:
         *  - stop() is called,
         *  - WebSocket thread is joined.
         */
        ~AttachedRuntime()
        {
            stop();
        }

        /**
         * @brief Stop the WebSocket server exactly once and join its thread.
         */
        void stop()
        {
            bool expected = false;

            // Prevent multiple stop attempts (thread-safe).
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

// -----------------------------------------------------------------------------
//  HTTP + WebSocket bundle for structured bindings
// -----------------------------------------------------------------------------
namespace vix
{
    /**
     * @brief Holds an HTTP App and WebSocket Server together.
     *
     * Enables structured bindings:
     *
     *    auto [app, ws] = vix::make_http_and_ws("config.json");
     */
    struct HttpAndWsBundle
    {
        vix::App app;
        vix::websocket::Server ws;
    };

    /**
     * @brief Construct both an HTTP App and WebSocket Server in one step.
     *
     * Notes:
     *  - No temporary variables are created.
     *  - The returned bundle is constructed in-place, allowing guaranteed
     *    copy elision under C++17.
     *
     * @param configPath Path to the configuration file.
     * @return HttpAndWsBundle containing both App and WS server.
     */
    inline HttpAndWsBundle make_http_and_ws(const std::filesystem::path &configPath)
    {
        // 1) Load global configuration (singleton).
        auto &cfg = vix::config::Config::getInstance(configPath);

        // 2) Create an executor for the WebSocket server.
        auto exec = vix::experimental::make_threadpool_executor(
            4, // minThreads
            8, // maxThreads
            0  // default priority
        );

        // 3) Construct bundle directly in the return expression (no moves).
        return HttpAndWsBundle{
            vix::App{},                                  // in-place App
            vix::websocket::Server{cfg, std::move(exec)} // in-place WebSocket server
        };
    }

    /**
     * @brief Attach a WebSocket server to an existing App and run HTTP.
     *
     * Automatically manages WebSocket shutdown through RAII.
     */
    inline void run_http_and_ws(vix::App &app,
                                vix::websocket::Server &ws,
                                int port = 8080)
    {
        vix::websocket::AttachedRuntime runtime{app, ws};
        app.run(port);
        // Runtime destructor ensures WS shutdown and thread join.
    }

    /**
     * @brief High-level helper: create HTTP + WS, configure them,
     *        and run everything together.
     *
     * Usage:
     *   vix::serve_http_and_ws("config/config.json", 8080,
     *       [](auto& app, auto& ws) {
     *           app.get("/", ...);
     *           ws.on_open(...);
     *       });
     */
    template <typename ConfigureFn>
    inline void serve_http_and_ws(const std::filesystem::path &configPath,
                                  int port,
                                  ConfigureFn &&fn)
    {
        auto bundle = make_http_and_ws(configPath);

        auto &app = bundle.app;
        auto &ws = bundle.ws;

        // User configures both HTTP + WebSocket runtimes.
        fn(app, ws);

        // Start servers.
        run_http_and_ws(app, ws, port);
    }

    /**
     * @brief Simplified overload: defaults to config/config.json and port 8080.
     *
     * Usage:
     *   vix::serve_http_and_ws([](auto& app, auto& ws) {
     *       ...
     *   });
     */
    template <typename ConfigureFn>
    inline void serve_http_and_ws(ConfigureFn &&fn)
    {
        serve_http_and_ws("config/config.json",
                          8080,
                          std::forward<ConfigureFn>(fn));
    }

} // namespace vix

// -----------------------------------------------------------------------------
//  Structured bindings specializations
// -----------------------------------------------------------------------------
namespace std
{
    template <>
    struct tuple_size<vix::HttpAndWsBundle>
        : std::integral_constant<std::size_t, 2>
    {
    };

    template <>
    struct tuple_element<0, vix::HttpAndWsBundle>
    {
        using type = vix::App;
    };

    template <>
    struct tuple_element<1, vix::HttpAndWsBundle>
    {
        using type = vix::websocket::Server;
    };
}

namespace vix
{
    template <std::size_t I>
    inline auto get(HttpAndWsBundle &bundle)
        -> std::tuple_element_t<I, HttpAndWsBundle> &
    {
        if constexpr (I == 0)
            return bundle.app;
        else
            return bundle.ws;
    }

    template <std::size_t I>
    inline auto get(const HttpAndWsBundle &bundle)
        -> const std::tuple_element_t<I, HttpAndWsBundle> &
    {
        if constexpr (I == 0)
            return bundle.app;
        else
            return bundle.ws;
    }
} // namespace vix
