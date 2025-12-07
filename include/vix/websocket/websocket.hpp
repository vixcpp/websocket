#ifndef VIX_WEBSOCKET_ENGINE_HPP
#define VIX_WEBSOCKET_ENGINE_HPP

/**
 * @file websocket.hpp
 * @brief Low-level WebSocket server engine.
 *
 * This component:
 *  - owns the io_context
 *  - accepts TCP connections
 *  - creates vix::websocket::Session instances for each client
 */

#include <memory>
#include <vector>
#include <atomic>
#include <thread>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>

#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/config/Config.hpp>
#include <vix/utils/Logger.hpp>

namespace vix::websocket
{
    namespace net = boost::asio;
    namespace beast = boost::beast;
    using tcp = net::ip::tcp;
    using Logger = vix::utils::Logger;

    class LowLevelServer
    {
    public:
        LowLevelServer(vix::config::Config &coreConfig,
                       std::shared_ptr<vix::executor::IExecutor> executor,
                       std::shared_ptr<Router> router);

        ~LowLevelServer();

        /// Start accepting connections and running io_context_ in background threads.
        void run();

        /// Cooperative async stop: close acceptor and stop io_context_.
        void stop_async();

        /// Join all I/O threads.
        void join_threads();

        bool is_stop_requested() const { return stopRequested_.load(); }

    private:
        void init_acceptor(unsigned short port);
        void start_accept();
        void start_io_threads();
        void handle_client(tcp::socket socket);

        int compute_io_thread_count() const;

    private:
        vix::config::Config &coreConfig_;
        Config wsConfig_;
        std::shared_ptr<vix::executor::IExecutor> executor_;
        std::shared_ptr<Router> router_;

        std::shared_ptr<net::io_context> ioContext_;
        std::unique_ptr<tcp::acceptor> acceptor_;
        std::vector<std::thread> ioThreads_;

        std::atomic<bool> stopRequested_{false};
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ENGINE_HPP
