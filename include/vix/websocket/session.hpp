/**
 *
 *  @file session.hpp
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
#ifndef VIX_WEBSOCKET_SESSION_HPP
#define VIX_WEBSOCKET_SESSION_HPP

#include <memory>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>
#include <deque>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/utils/Logger.hpp>

namespace vix::websocket
{
  namespace beast = boost::beast;
  namespace ws = boost::beast::websocket;
  namespace net = boost::asio;

  class Session : public std::enable_shared_from_this<Session>
  {
  public:
    Session(
        net::ip::tcp::socket socket,
        const Config &cfg,
        std::shared_ptr<Router> router,
        std::shared_ptr<vix::executor::IExecutor> executor);

    ~Session() = default;
    void run();
    void send_text(std::string_view text);
    void send_binary(const void *data, std::size_t size);
    void close(ws::close_reason reason = ws::close_reason{});

  private:
    void do_accept();
    void on_accept(const boost::system::error_code &ec);
    void do_read();
    void on_read(const boost::system::error_code &ec, std::size_t bytes);
    void arm_idle_timer();
    void cancel_idle_timer();
    void on_idle_timeout(const boost::system::error_code &ec);
    void on_write_complete(const boost::system::error_code &ec, std::size_t bytes);

  private:
    ws::stream<net::ip::tcp::socket> ws_;
    Config cfg_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<vix::executor::IExecutor> executor_;
    beast::flat_buffer buffer_;
    net::steady_timer idleTimer_;
    bool closing_ = false;

    struct PendingMessage
    {
      bool isBinary;
      std::string data;
    };

    std::deque<PendingMessage> writeQueue_;
    bool writeInProgress_ = false;
    void do_enqueue_message(bool isBinary, std::string payload);
    void do_write_next();
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SESSION_HPP
