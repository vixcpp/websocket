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

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/config/Config.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/utils/Logger.hpp>
#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/websocket/session.hpp>

namespace vix::websocket
{
  using vix::async::core::io_context;
  using vix::async::core::task;
  using vix::async::net::tcp_endpoint;
  using vix::async::net::tcp_listener;
  using vix::async::net::tcp_stream;

  /**
   * @brief Low-level asynchronous WebSocket server engine.
   *
   * Owns the native Vix listener, runtime context, worker threads and session lifecycle.
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
    bool is_stop_requested() const
    {
      return stopRequested_.load(std::memory_order_acquire);
    }

  private:
    /** @brief Initialize the native Vix TCP listener on the configured port. */
    void init_listener(unsigned short port);

    /** @brief Begin accepting incoming TCP connections. */
    void start_accept();

    /** @brief Native accept loop. */
    task<void> accept_loop();

    /** @brief Spawn IO worker threads. */
    void start_io_threads();

    /** @brief Handle a newly accepted TCP client stream. */
    task<void> handle_client(std::unique_ptr<tcp_stream> stream);

    /** @brief Close a client stream safely. */
    void close_stream(std::unique_ptr<tcp_stream> stream);

    /** @brief Compute number of IO threads to run. */
    std::size_t compute_io_thread_count() const;

    /** @brief Build the bind endpoint from configuration. */
    tcp_endpoint make_bind_endpoint() const;

  private:
    vix::config::Config &coreConfig_;
    Config wsConfig_;
    std::shared_ptr<vix::executor::IExecutor> executor_;
    std::shared_ptr<Router> router_;

    std::shared_ptr<io_context> ioContext_;
    std::unique_ptr<tcp_listener> listener_;
    std::vector<std::thread> ioThreads_;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> logged_listen_{false};
    std::atomic<int> boundPort_{0};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ENGINE_HPP
