/**
 *
 *  @file websocket.hpp
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
#ifndef VIX_WEBSOCKET_ENGINE_HPP
#define VIX_WEBSOCKET_ENGINE_HPP

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

  /**
   * @brief Low-level asynchronous WebSocket server engine.
   *
   * Owns the acceptor, IO context, worker threads and session lifecycle.
   * This class is intentionally protocol-agnostic and delegates all
   * connection events to the Router.
   */
  class LowLevelServer
  {
  public:
    /**
     * @brief Construct the WebSocket engine.
     *
     * @param coreConfig Core application configuration.
     * @param executor Shared executor for async continuations.
     * @param router Event router used by sessions.
     */
    LowLevelServer(
        vix::config::Config &coreConfig,
        std::shared_ptr<vix::executor::IExecutor> executor,
        std::shared_ptr<Router> router);

    ~LowLevelServer();

    /** @brief Start listening and processing WebSocket connections. */
    void run();

    /** @brief Request an asynchronous shutdown. */
    void stop_async();

    /** @brief Join all IO worker threads. */
    void join_threads();

    /** @brief Check whether a stop has been requested. */
    bool is_stop_requested() const { return stopRequested_.load(); }

  private:
    /** @brief Initialize the TCP acceptor on the configured port. */
    void init_acceptor(unsigned short port);

    /** @brief Begin accepting incoming TCP connections. */
    void start_accept();

    /** @brief Spawn IO worker threads. */
    void start_io_threads();

    /** @brief Handle a newly accepted TCP client socket. */
    void handle_client(net::ip::tcp::socket socket);

    /** @brief Compute number of IO threads to run. */
    std::size_t compute_io_thread_count() const;

  private:
    vix::config::Config &coreConfig_;
    Config wsConfig_;
    std::shared_ptr<vix::executor::IExecutor> executor_;
    std::shared_ptr<Router> router_;

    std::shared_ptr<net::io_context> ioContext_;
    std::unique_ptr<net::ip::tcp::acceptor> acceptor_;
    std::vector<std::thread> ioThreads_;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> logged_listen_{false};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ENGINE_HPP
