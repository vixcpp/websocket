#pragma once

#include <vix/websocket/server.hpp>
#include <vix/websocket/client.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>
#include <vix/websocket/Metrics.hpp>

#include <vix/config/Config.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>

#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace vix::websocket
{
    /**
     * @brief High-level WebSocket application wrapper for Vix.cpp.
     *
     * This class provides a minimal "sugar" API similar to runtimes that expose
     * a `ws("/chat", handler)` style interface. It wraps:
     *
     *   - vix::config::Config        (configuration loading)
     *   - vix::experimental::ThreadPoolExecutor (async scheduling)
     *   - vix::websocket::Server     (WebSocket server core)
     *
     * and installs a single `on_typed_message` callback that dispatches to
     * user handlers registered via `ws(endpoint, handler)`.
     *
     * Notes:
     *  - For now, the `endpoint` string (e.g. "/chat") is a *logical* label.
     *    The current implementation does not yet route by HTTP path; every
     *    registered handler sees all typed messages.
     *  - This API is designed to evolve later into true path-based routing
     *    once the underlying HTTP upgrade plumbing exposes the request path.
     */
    class App
    {
    public:
        using TypedHandler = std::function<void(
            Session &,
            const std::string &type,
            const vix::json::kvs &payload)>;

        /**
         * @brief Construct an App using a config file and thread-pool settings.
         *
         * @param configPath   Path to a JSON config file (e.g. "config/config.json").
         * @param minThreads   Minimum number of worker threads for the executor.
         * @param maxThreads   Maximum number of worker threads for the executor.
         * @param defaultPrio  Default scheduling priority for tasks.
         */
        App(const std::string &configPath,
            std::size_t minThreads = 4,
            std::size_t maxThreads = 8,
            int defaultPrio = 0);

        /**
         * @brief Register a WebSocket "endpoint" with a typed-message handler.
         *
         * Example:
         * @code{.cpp}
         * app.ws("/chat", [](Session& s, const std::string& type, const kvs& payload){
         *     if (type == "chat.message") { ... }
         * });
         * @endcode
         *
         * For now, the endpoint string is a purely logical label but is stored
         * along with the handler so that future versions can route based on
         * HTTP path or other connection metadata.
         *
         * @return *this for call chaining.
         */
        App &ws(const std::string &endpoint, TypedHandler handler);

        /**
         * @brief Start the underlying WebSocket server and block the calling thread.
         *
         * This is a convenience wrapper around `Server::listen_blocking()`.
         */
        void run_blocking();

        /**
         * @brief Access the underlying WebSocket server for advanced usage.
         */
        Server &server() noexcept { return server_; }

        /**
         * @brief Access the underlying config object.
         */
        vix::config::Config &config() noexcept { return config_; }

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
