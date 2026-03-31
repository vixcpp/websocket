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
#include <mutex>
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
   * Owns the native Vix listener, runtime context, worker threads, and
   * session lifecycle. This class stays focused on transport and connection
   * orchestration, while message events are delegated to the Router.
   *
   * This version uses the generic executor abstraction and no longer depends
   * on a concrete RuntimeExecutor type. :contentReference[oaicite:0]{index=0}
   */
  class LowLevelServer
  {
  public:
    /**
     * @brief Construct the WebSocket engine.
     *
     * @param coreConfig Core application configuration.
     * @param executor Shared executor used by async operations.
     * @param router Event router used by sessions.
     */
    LowLevelServer(
        vix::config::Config &coreConfig,
        std::shared_ptr<vix::executor::IExecutor> executor,
        std::shared_ptr<Router> router);

    /**
     * @brief Destroy the WebSocket engine.
     */
    ~LowLevelServer();

    /**
     * @brief Start listening and processing WebSocket connections.
     */
    void run();

    /**
     * @brief Request an asynchronous shutdown.
     */
    void stop_async();

    /**
     * @brief Join all IO worker threads.
     */
    void join_threads();

    /**
     * @brief Check whether a stop has been requested.
     *
     * @return True if shutdown has been requested.
     */
    bool is_stop_requested() const
    {
      return stopRequested_.load(std::memory_order_acquire);
    }

  private:
    /**
     * @brief Initialize the native Vix TCP listener on the configured port.
     *
     * @param port Port to bind.
     */
    task<void> init_listener(unsigned short port);

    /**
     * @brief Start the low-level server asynchronously.
     *
     * @return Task representing startup.
     */
    task<void> start_server();

    /**
     * @brief Begin accepting incoming TCP connections.
     */
    void start_accept();

    /**
     * @brief Native accept loop.
     *
     * @return Task representing the accept loop.
     */
    task<void> accept_loop();

    /**
     * @brief Spawn IO worker threads.
     */
    void start_io_threads();

    /**
     * @brief Handle a newly accepted TCP client stream.
     *
     * @param stream Accepted client stream.
     * @return Task representing the client lifecycle.
     */
    task<void> handle_client(std::unique_ptr<tcp_stream> stream);

    /**
     * @brief Close a client stream safely.
     *
     * @param stream Stream to close.
     */
    void close_stream(std::unique_ptr<tcp_stream> stream);

    /**
     * @brief Compute the number of IO threads to run.
     *
     * @return Number of worker threads.
     */
    std::size_t compute_io_thread_count() const;

    /**
     * @brief Build the bind endpoint from configuration.
     *
     * @return TCP endpoint used for bind/listen.
     */
    tcp_endpoint make_bind_endpoint() const;

  private:
    /** @brief Core application configuration source. */
    vix::config::Config &coreConfig_;

    /** @brief WebSocket-specific resolved configuration. */
    Config wsConfig_;

    /** @brief Shared executor used by the engine. */
    std::shared_ptr<vix::executor::IExecutor> executor_;

    /** @brief Shared event router used by all sessions. */
    std::shared_ptr<Router> router_;

    /** @brief Shared asynchronous IO context for listener and sessions. */
    std::shared_ptr<io_context> ioContext_;

    /** @brief Native TCP listener bound to the configured endpoint. */
    std::unique_ptr<tcp_listener> listener_;

    /** @brief Worker threads driving the IO context. */
    std::vector<std::thread> ioThreads_;

    /** @brief Global shutdown flag. */
    std::atomic<bool> stopRequested_{false};

    /** @brief Ensures the listen log line is emitted only once. */
    std::atomic<bool> logged_listen_{false};

    /** @brief Effective bound port, useful when binding to port 0. */
    std::atomic<int> boundPort_{0};

    /** @brief Protects the join phase against concurrent callers. */
    mutable std::mutex joinMutex_;

    /** @brief Ensures IO worker threads are joined only once. */
    std::atomic<bool> threadsJoined_{false};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ENGINE_HPP
