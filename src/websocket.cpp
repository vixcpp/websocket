/**
 *
 *  @file websocket.cpp
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
#include <vix/websocket/websocket.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <vix/async/core/spawn.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/async/net/asio_net_service.hpp>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace vix::websocket
{
  using Logger = vix::utils::Logger;
  using vix::async::core::spawn_detached;

  namespace
  {
    inline Logger &logger()
    {
      return Logger::getInstance();
    }

    void init_logger_from_env_once()
    {
      static std::once_flag once;
      std::call_once(
          once,
          []()
          {
            auto &log = Logger::getInstance();
            log.setLevelFromEnv("VIX_LOG_LEVEL");
            log.setFormatFromEnv("VIX_LOG_FORMAT");
          });
    }

    void set_affinity(std::size_t thread_index)
    {
      (void)thread_index;
    }
  } // namespace

  LowLevelServer::LowLevelServer(
      vix::config::Config &coreConfig,
      std::shared_ptr<vix::executor::RuntimeExecutor> executor,
      std::shared_ptr<Router> router)
      : coreConfig_(coreConfig),
        wsConfig_(Config::from_core(coreConfig_)),
        executor_(std::move(executor)),
        router_(std::move(router)),
        ioContext_(std::make_shared<io_context>()),
        listener_(nullptr),
        ioThreads_(),
        stopRequested_(false),
        logged_listen_(false),
        boundPort_(0),
        joinMutex_(),
        threadsJoined_(false)
  {
    if (!executor_)
    {
      throw std::invalid_argument(
          "vix::websocket::LowLevelServer requires a valid runtime executor");
    }

    const int port = coreConfig_.getInt("websocket.port", 9090);
    if ((port != 0 && port < 1024) || port > 65535)
    {
      logger().log(
          Logger::Level::Error,
          "[ws] port out of range (1024-65535): {}",
          port);
      throw std::invalid_argument("Invalid WebSocket port");
    }
  }

  LowLevelServer::~LowLevelServer()
  {
    try
    {
      stop_async();

      if (!threadsJoined_.load(std::memory_order_acquire))
      {
        join_threads();
      }
    }
    catch (...)
    {
    }
  }

  vix::async::net::tcp_endpoint LowLevelServer::make_bind_endpoint() const
  {
    vix::async::net::tcp_endpoint ep{};
    ep.host = coreConfig_.getString("websocket.host", "0.0.0.0");
    ep.port = static_cast<std::uint16_t>(coreConfig_.getInt("websocket.port", 9090));
    return ep;
  }

  vix::async::core::task<void> LowLevelServer::init_listener(unsigned short port)
  {
    listener_ = vix::async::net::make_tcp_listener(*ioContext_);
    if (!listener_)
    {
      throw std::runtime_error("failed to create native Vix TCP listener");
    }

    try
    {
      vix::async::net::tcp_endpoint endpoint{};
      endpoint.host = coreConfig_.getString("websocket.host", "0.0.0.0");
      endpoint.port = port;

      co_await listener_->async_listen(endpoint);

      boundPort_.store(static_cast<int>(port), std::memory_order_relaxed);
    }
    catch (const std::exception &e)
    {
      logger().log(
          Logger::Level::Error,
          "[ws] listener init failed on port {}: {}",
          static_cast<unsigned int>(port),
          e.what());
      throw;
    }

    co_return;
  }

  vix::async::core::task<void> LowLevelServer::start_server()
  {
    const int port = coreConfig_.getInt("websocket.port", 9090);

    co_await init_listener(static_cast<unsigned short>(port));

    if (stopRequested_.load(std::memory_order_acquire))
    {
      co_return;
    }

    if (!listener_ || !listener_->is_open())
    {
      throw std::runtime_error("websocket listener is not open");
    }

    if (!logged_listen_.exchange(true, std::memory_order_acq_rel))
    {
      spawn_detached(*ioContext_, accept_loop());
    }

    co_return;
  }

  void LowLevelServer::run()
  {
    init_logger_from_env_once();
    vix::utils::console_wait_banner();

    start_io_threads();
    spawn_detached(*ioContext_, start_server());
  }

  void LowLevelServer::start_accept()
  {
    if (stopRequested_.load(std::memory_order_acquire))
    {
      return;
    }

    if (!listener_ || !listener_->is_open())
    {
      throw std::runtime_error("websocket listener is not open");
    }

    if (!logged_listen_.exchange(true, std::memory_order_acq_rel))
    {
      spawn_detached(*ioContext_, accept_loop());
    }
  }

  vix::async::core::task<void> LowLevelServer::accept_loop()
  {
    while (!stopRequested_.load(std::memory_order_acquire))
    {
      if (!listener_ || !listener_->is_open())
      {
        break;
      }

      try
      {
        auto stream = co_await listener_->async_accept();

        if (!stream)
        {
          if (stopRequested_.load(std::memory_order_acquire) ||
              !listener_ || !listener_->is_open())
          {
            break;
          }

          continue;
        }

        if (stopRequested_.load(std::memory_order_acquire))
        {
          close_stream(std::move(stream));
          break;
        }

        spawn_detached(*ioContext_, handle_client(std::move(stream)));
      }
      catch (const std::exception &e)
      {
        if (stopRequested_.load(std::memory_order_acquire) ||
            !listener_ || !listener_->is_open())
        {
          break;
        }

        const auto *se = dynamic_cast<const std::system_error *>(&e);
        if (se)
        {
          const auto code = se->code();
          if (code == std::errc::operation_canceled ||
              code == std::errc::bad_file_descriptor)
          {
            break;
          }
        }
      }
    }

    co_return;
  }

  void LowLevelServer::start_io_threads()
  {
    const std::size_t n = compute_io_thread_count();
    ioThreads_.reserve(n);

    for (std::size_t i = 0; i < n; ++i)
    {
      ioThreads_.emplace_back(
          [this, i]()
          {
            vix::utils::console_wait_banner();

            try
            {
              logger().log(
                  Logger::Level::Error,
                  "[trace] LowLevelServer::io thread {} enter",
                  static_cast<unsigned long long>(i));

              ioContext_->run();

              logger().log(
                  Logger::Level::Error,
                  "[trace] LowLevelServer::io thread {} leave",
                  static_cast<unsigned long long>(i));
            }
            catch (const std::exception &e)
            {
              logger().log(
                  Logger::Level::Error,
                  "[ws] io thread {} error ({})",
                  i,
                  e.what());
            }
          });
    }
  }

  vix::async::core::task<void> LowLevelServer::handle_client(
      std::unique_ptr<tcp_stream> stream)
  {
    if (!stream)
    {
      co_return;
    }

    try
    {
      auto session = std::make_shared<Session>(
          std::move(stream),
          wsConfig_,
          router_,
          executor_,
          ioContext_);

      co_await session->run();
    }
    catch (const std::exception &e)
    {
      logger().log(
          Logger::Level::Error,
          "[ws] failed to create or run session ({})",
          e.what());

      if (stream)
      {
        close_stream(std::move(stream));
      }
    }

    co_return;
  }

  void LowLevelServer::close_stream(std::unique_ptr<tcp_stream> stream)
  {
    if (!stream)
    {
      return;
    }

    try
    {
      stream->close();
    }
    catch (...)
    {
    }
  }

  std::size_t LowLevelServer::compute_io_thread_count() const
  {
    return 1;
  }

  void LowLevelServer::stop_async()
  {
    logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async enter");

    const bool already =
        stopRequested_.exchange(true, std::memory_order_acq_rel);

    if (already)
    {
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async already stopping");
      return;
    }

    try
    {
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async before listener close");
      if (listener_)
      {
        listener_->close();
      }
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async after listener close");
    }
    catch (...)
    {
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async listener close threw");
    }

    if (ioContext_)
    {
      try
      {
        logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async before ioContext net stop");
        ioContext_->net().stop();
        logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async after ioContext net stop");
      }
      catch (...)
      {
        logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async ioContext net stop threw");
      }

      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async before ioContext stop");
      ioContext_->stop();
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async after ioContext stop");
    }

    logger().log(Logger::Level::Error, "[trace] LowLevelServer::stop_async leave");
  }

  void LowLevelServer::join_threads()
  {
    logger().log(Logger::Level::Error, "[trace] LowLevelServer::join_threads enter");

    std::lock_guard<std::mutex> lock(joinMutex_);

    if (threadsJoined_.load(std::memory_order_acquire))
    {
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::join_threads already joined");
      return;
    }

    const std::thread::id current_id = std::this_thread::get_id();
    bool deferred_completion = false;

    if (ioContext_)
    {
      try
      {
        logger().log(Logger::Level::Error,
                     "[trace] LowLevelServer::join_threads before ioContext net join");
        ioContext_->net().join();
        logger().log(Logger::Level::Error,
                     "[trace] LowLevelServer::join_threads after ioContext net join");
      }
      catch (...)
      {
        logger().log(Logger::Level::Error,
                     "[trace] LowLevelServer::join_threads ioContext net join threw");
      }
    }

    for (std::size_t i = 0; i < ioThreads_.size(); ++i)
    {
      if (!ioThreads_[i].joinable())
      {
        continue;
      }

      if (ioThreads_[i].get_id() == current_id)
      {
        logger().log(
            Logger::Level::Warn,
            "[ws] join_threads: detaching current io thread {}",
            i);
        ioThreads_[i].detach();
        deferred_completion = true;
        continue;
      }

      logger().log(
          Logger::Level::Error,
          "[trace] LowLevelServer::join_threads before io join {}",
          static_cast<unsigned long long>(i));
      ioThreads_[i].join();
      logger().log(
          Logger::Level::Error,
          "[trace] LowLevelServer::join_threads after io join {}",
          static_cast<unsigned long long>(i));
    }

    ioThreads_.clear();

    if (!deferred_completion && ioContext_)
    {
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::join_threads before ioContext shutdown");
      ioContext_->shutdown();
      logger().log(Logger::Level::Error, "[trace] LowLevelServer::join_threads after ioContext shutdown");
    }

    if (!deferred_completion)
    {
      threadsJoined_.store(true, std::memory_order_release);
    }

    logger().log(Logger::Level::Error, "[trace] LowLevelServer::join_threads leave");
  }
} // namespace vix::websocket
