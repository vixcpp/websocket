/**
 *
 *  @file App.cpp
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
#include <vix/websocket/App.hpp>
#include <vix/websocket/protocol.hpp>

#include <stdexcept>
#include <utility>

namespace vix::websocket
{

  App::App(
      const std::string &configPath,
      std::shared_ptr<vix::executor::RuntimeExecutor> executor)
      : config_(configPath),
        executor_(std::move(executor)),
        server_(config_, executor_)
  {
    if (!executor_)
    {
      throw std::invalid_argument(
          "vix::websocket::App requires a valid runtime executor");
    }

    install_dispatcher();
  }

  App::~App() noexcept
  {
    stop();
  }

  App &App::ws(const std::string &endpoint, TypedHandler handler)
  {
    routes_.push_back(Route{endpoint, std::move(handler)});
    install_dispatcher();
    return *this;
  }

  void App::install_dispatcher()
  {
    server_.on_typed_message(
        [this](Session &session,
               const std::string &type,
               const vix::json::kvs &payload)
        {
          dispatch_typed_message(session, type, payload);
        });
  }

  void App::dispatch_typed_message(
      Session &session,
      const std::string &type,
      const vix::json::kvs &payload)
  {
    for (auto &route : routes_)
    {
      if (route.handler)
      {
        route.handler(session, type, payload);
      }
    }
  }

  void App::run_blocking()
  {
    server_.listen_blocking();
  }

  void App::stop() noexcept
  {
    std::lock_guard<std::mutex> lock(stopMutex_);

    if (stopped_.exchange(true, std::memory_order_acq_rel))
    {
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Error,
          "[trace] websocket::App::stop already stopped");
      return;
    }

    vix::utils::Logger::getInstance().log(
        vix::utils::Logger::Level::Error,
        "[trace] websocket::App::stop enter");

    try
    {
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Error,
          "[trace] websocket::App::stop before server_.stop");
      server_.stop();
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Error,
          "[trace] websocket::App::stop after server_.stop");
    }
    catch (...)
    {
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Error,
          "[trace] websocket::App::stop server_.stop threw");
    }

    try
    {
      if (executor_)
      {
        vix::utils::Logger::getInstance().log(
            vix::utils::Logger::Level::Error,
            "[trace] websocket::App::stop before executor_->stop");
        executor_->stop();
        vix::utils::Logger::getInstance().log(
            vix::utils::Logger::Level::Error,
            "[trace] websocket::App::stop after executor_->stop");
      }
    }
    catch (...)
    {
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Error,
          "[trace] websocket::App::stop executor_->stop threw");
    }

    vix::utils::Logger::getInstance().log(
        vix::utils::Logger::Level::Error,
        "[trace] websocket::App::stop leave");
  }
} // namespace vix::websocket
