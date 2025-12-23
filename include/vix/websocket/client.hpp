#ifndef VIX_WEBSOCKET_CLIENT_HPP
#define VIX_WEBSOCKET_CLIENT_HPP

/**
 * @file client.hpp
 * @brief High-level WebSocket client with reconnection and heartbeat support.
 *
 * This component:
 *   - manages the full client lifecycle (resolve, connect, handshake, read)
 *   - exposes a simple event-driven API (open, message, close, error)
 *   - supports optional automatic reconnection and heartbeat (ping)
 *   - provides helpers for { type, payload } messages using vix::json::kvs
 *
 * Typical usage:
 *
 *   auto client = vix::websocket::Client::create("localhost", "9090", "/");
 *
 *   client->on_open([]{
 *       std::cout << "Connected to server\\n";
 *   });
 *
 *   client->on_message([](const std::string &msg){
 *       std::cout << "Server says: " << msg << "\\n";
 *   });
 *
 *   client->enable_auto_reconnect(true, std::chrono::seconds(3));
 *   client->enable_heartbeat(std::chrono::seconds(30));
 *
 *   client->connect();
 *
 *   for (std::string line; std::getline(std::cin, line); ) {
 *       if (line == "/quit") break;
 *       client->send_json_message("chat.message", {
 *           "user", "alice",
 *           "text", line,
 *       });
 *   }
 *
 *   client->close();
 */

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <deque>
#include <mutex>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp>
#include <vix/websocket/protocol.hpp>

namespace vix::websocket
{
    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = net::ip::tcp;

    class Client : public std::enable_shared_from_this<Client>
    {
    public:
        using OpenHandler = std::function<void()>;
        using MessageHandler = std::function<void(const std::string &)>;
        using CloseHandler = std::function<void()>;
        using ErrorHandler = std::function<void(const boost::system::error_code &)>;

        static std::shared_ptr<Client> create(std::string host,
                                              std::string port,
                                              std::string target = "/")
        {
            return std::shared_ptr<Client>(
                new Client(std::move(host), std::move(port), std::move(target)));
        }

        void on_open(OpenHandler cb) { onOpen_ = std::move(cb); }
        void on_message(MessageHandler cb) { onMessage_ = std::move(cb); }
        void on_close(CloseHandler cb) { onClose_ = std::move(cb); }
        void on_error(ErrorHandler cb) { onError_ = std::move(cb); }

        /// Enable / disable automatic reconnection.
        void enable_auto_reconnect(bool enable,
                                   std::chrono::seconds delay = std::chrono::seconds{3})
        {
            autoReconnect_ = enable;
            reconnectDelay_ = delay;
        }

        /// Enable periodic ping (heartbeat) from the client.
        void enable_heartbeat(std::chrono::seconds interval)
        {
            if (interval.count() <= 0)
            {
                heartbeatEnabled_ = false;
                return;
            }
            heartbeatInterval_ = interval;
            heartbeatEnabled_ = true;
        }

        /// Start resolve, connect, handshake and I/O thread.
        void connect()
        {
            if (!alive_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed))
                return;

            bool expected = false;
            if (!started_.compare_exchange_strong(expected, true))
                return; // already running

            init_io();

            auto self = shared_from_this();

            // I/O thread driving the event loop
            ioThread_ = std::thread([self]()
                                    {
                try
                {
                    self->ioc_->run();
                }
                catch (const std::exception &e)
                {
                    boost::system::error_code ec; // dummy
                    self->emit_error(ec, e.what());
                } });

            // Bootstrap pipeline (resolve) on io_context
            net::post(*ioc_, [self]()
                      { self->do_resolve(); });
        }

        void do_write()
        {
            if (!ws_ || closing_)
                return;

            std::string current;
            {
                std::lock_guard<std::mutex> lock(writeMutex_);
                if (writeQueue_.empty())
                {
                    writeInProgress_ = false;
                    return;
                }
                current = writeQueue_.front();
            }

            auto self = shared_from_this();
            ws_->async_write(
                net::buffer(current),
                [self](const boost::system::error_code &ec, std::size_t)
                {
                    {
                        std::lock_guard<std::mutex> lock(self->writeMutex_);
                        if (!self->writeQueue_.empty())
                            self->writeQueue_.pop_front();
                    }

                    if (ec && ec != net::error::operation_aborted)
                    {
                        self->emit_error(ec, "write");
                        self->maybe_schedule_reconnect(ec);
                        return;
                    }

                    // Enchaîner le message suivant s’il y en a un
                    self->do_write();
                });
        }

        /// Send a text message (thread-safe via executor).
        void send_text(const std::string &text)
        {
            if (!connected_ || closing_ || !ws_)
                return;

            auto self = shared_from_this();
            net::post(ws_->get_executor(),
                      [self, text]()
                      {
                          if (!self->ws_ || self->closing_)
                              return;

                          {
                              std::lock_guard<std::mutex> lock(self->writeMutex_);
                              self->writeQueue_.push_back(text);

                              // Si un write est déjà en cours, on ne lance pas un 2e
                              if (self->writeInProgress_)
                                  return;

                              self->writeInProgress_ = true;
                          }

                          self->do_write();
                      });
        }

        /// Send a { type, payload } JSON message using vix::json::kvs.
        void send_json_message(const std::string &type,
                               const vix::json::kvs &payload)
        {
            nlohmann::json payloadJson = detail::ws_kvs_to_nlohmann(payload);

            nlohmann::json j{
                {"type", type},
                {"payload", payloadJson},
            };

            send_text(j.dump());
        }

        /// Send a { type, payload } JSON message using vix::json::token list.
        ///
        /// Example:
        ///   client->send_json_message("chat.message", {
        ///       "user", "alice",
        ///       "text", "hello",
        ///   });
        void send_json_message(const std::string &type,
                               std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs payload{payloadTokens};
            send_json_message(type, payload);
        }

        /// Explicit ping (in addition to optional heartbeat).
        void send_ping()
        {
            if (!connected_ || closing_ || !ws_)
                return;

            auto self = shared_from_this();
            net::post(ws_->get_executor(),
                      [self]()
                      {
                          if (!self->ws_ || self->closing_)
                              return;
                          websocket::ping_data data; // empty payload
                          self->ws_->async_ping(
                              data,
                              [self](const boost::system::error_code &ec)
                              {
                                  if (ec && ec != net::error::operation_aborted)
                                  {
                                      self->emit_error(ec, "ping");
                                      self->maybe_schedule_reconnect(ec);
                                  }
                              });
                      });
        }

        /// Graceful shutdown.
        void close()
        {
            if (closing_.exchange(true))
                return;

            alive_.store(false, std::memory_order_relaxed);
            heartbeatStop_.store(true, std::memory_order_relaxed);

            // Empêche toute reconnexion future
            autoReconnect_.store(false, std::memory_order_relaxed);
            reconnectScheduled_.store(false, std::memory_order_relaxed);

            auto self = shared_from_this();

            if (ws_)
            {
                net::post(ws_->get_executor(),
                          [self]()
                          {
                              if (!self->ws_)
                                  return;
                              self->ws_->async_close(
                                  websocket::close_code::normal,
                                  [self](const boost::system::error_code &ec2)
                                  {
                                      if (ec2 && ec2 != net::error::operation_aborted)
                                      {
                                          self->emit_error(ec2, "close");
                                      }
                                  });
                          });
            }

            if (ioc_)
                ioc_->stop();

            if (ioThread_.joinable() && std::this_thread::get_id() != ioThread_.get_id())
                ioThread_.join();

            if (heartbeatThread_.joinable() && std::this_thread::get_id() != heartbeatThread_.get_id())
                heartbeatThread_.join();
        }

        ~Client()
        {
            try
            {
                close();
            }
            catch (...)
            {
            }
        }

        /**
         * @brief Send a typed JSON message `{ "type", "payload" }` using kvs.
         *
         * Example:
         * @code{.cpp}
         * client->send("chat.message", {
         *     {"user", "alice"},
         *     {"text", "hello"},
         * });
         * @endcode
         */
        void send(const std::string &type,
                  const vix::json::kvs &payload)
        {
            send_json_message(type, payload);
        }

        /**
         * @brief Send a typed JSON message using an initializer-list of tokens.
         *
         * Example:
         * @code{.cpp}
         * client->send("chat.message", {
         *     "user", "alice",
         *     "text", "hello",
         * });
         * @endcode
         */
        void send(const std::string &type,
                  std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs payload{payloadTokens};
            send_json_message(type, payload);
        }

    private:
        Client(std::string host, std::string port, std::string target)
            : host_(std::move(host)), port_(std::move(port)), target_(std::move(target))
        {
        }

        void init_io()
        {
            // On reconnect, ensure previous context is fully stopped
            if (ioc_)
                ioc_->stop();
            if (ioThread_.joinable() && std::this_thread::get_id() != ioThread_.get_id())
                ioThread_.join();

            if (heartbeatThread_.joinable() && std::this_thread::get_id() != heartbeatThread_.get_id())
                heartbeatThread_.join();

            ioc_ = std::make_unique<net::io_context>();
            resolver_ = std::make_unique<tcp::resolver>(*ioc_);
            ws_ = std::make_unique<websocket::stream<tcp::socket>>(*ioc_);
            buffer_.consume(buffer_.size());

            connected_.store(false, std::memory_order_relaxed);
            closing_.store(false, std::memory_order_relaxed);
            heartbeatStop_.store(false, std::memory_order_relaxed);
        }

        void do_resolve()
        {
            auto self = shared_from_this();
            resolver_->async_resolve(
                host_,
                port_,
                [self](const boost::system::error_code &ec, tcp::resolver::results_type res)
                {
                    if (ec)
                    {
                        self->emit_error(ec, "resolve");
                        self->maybe_schedule_reconnect(ec);
                        return;
                    }
                    self->do_connect(res);
                });
        }

        void do_connect(const tcp::resolver::results_type &results)
        {
            auto self = shared_from_this();

            net::async_connect(
                ws_->next_layer(),
                results,
                [self](const boost::system::error_code &ec, const tcp::endpoint &)
                {
                    if (ec)
                    {
                        self->emit_error(ec, "connect");
                        self->maybe_schedule_reconnect(ec);
                        return;
                    }
                    self->do_handshake();
                });
        }

        void do_handshake()
        {
            auto self = shared_from_this();
            std::string host_header = host_ + ":" + port_;

            ws_->set_option(websocket::stream_base::timeout::suggested(
                beast::role_type::client));

            ws_->async_handshake(
                host_header,
                target_,
                [self](const boost::system::error_code &ec)
                {
                    if (ec)
                    {
                        self->emit_error(ec, "handshake");
                        self->maybe_schedule_reconnect(ec);
                        return;
                    }

                    self->connected_.store(true, std::memory_order_relaxed);
                    if (self->onOpen_)
                        self->onOpen_();

                    if (self->heartbeatEnabled_)
                        self->start_heartbeat();

                    self->do_read();
                });
        }

        void do_read()
        {
            auto self = shared_from_this();
            ws_->async_read(
                buffer_,
                [self](const boost::system::error_code &ec, std::size_t /*bytes*/)
                {
                    if (ec)
                    {
                        if (ec != websocket::error::closed &&
                            ec != net::error::operation_aborted)
                        {
                            self->emit_error(ec, "read");
                        }

                        self->connected_.store(false, std::memory_order_relaxed);
                        if (self->onClose_)
                            self->onClose_();

                        self->maybe_schedule_reconnect(ec);
                        return;
                    }

                    auto data = beast::buffers_to_string(self->buffer_.data());
                    self->buffer_.consume(self->buffer_.size());

                    if (self->onMessage_)
                        self->onMessage_(data);

                    if (!self->closing_)
                        self->do_read();
                });
        }

        void start_heartbeat()
        {
            if (heartbeatThread_.joinable())
                return;

            auto self = shared_from_this();
            heartbeatThread_ = std::thread([self]()
                                           {
             while (!self->heartbeatStop_.load(std::memory_order_relaxed) &&
                                              self->alive_.load(std::memory_order_relaxed))
                {
                    std::this_thread::sleep_for(self->heartbeatInterval_);
                 if (!self->connected_.load(std::memory_order_relaxed) ||
                        self->closing_.load(std::memory_order_relaxed) ||
                        self->heartbeatStop_.load(std::memory_order_relaxed))
                        continue;


                    self->send_ping();
                } });
        }

        void maybe_schedule_reconnect(const boost::system::error_code &ec)
        {
            if (!autoReconnect_.load(std::memory_order_relaxed) ||
                closing_.load(std::memory_order_relaxed) ||
                !alive_.load(std::memory_order_relaxed))
                return;

            // "Normal" closure: do not reconnect automatically.
            if (ec == websocket::error::closed ||
                ec == net::error::operation_aborted)
                return;

            bool expected = false;
            if (!reconnectScheduled_.compare_exchange_strong(expected, true))
                return; // already scheduled

            auto self = shared_from_this();
            std::thread([self]()
                        {
                            std::this_thread::sleep_for(self->reconnectDelay_);

                            if (!self->alive_.load(std::memory_order_relaxed) ||
                                self->closing_.load(std::memory_order_relaxed))
                            {
                                self->reconnectScheduled_.store(false, std::memory_order_relaxed);
                                return;
                            }

                            self->started_.store(false, std::memory_order_relaxed);
                            self->reconnectScheduled_.store(false, std::memory_order_relaxed);
                            self->connect(); })
                .detach();
        }

        void emit_error(const boost::system::error_code &ec,
                        const char *stage)
        {
            if (onError_)
            {
                onError_(ec);
            }
            else
            {
                std::cerr << "[Client][" << stage
                          << "] error: " << ec.message() << "\n";
            }
        }

    private:
        std::string host_;
        std::string port_;
        std::string target_;
        std::unique_ptr<net::io_context> ioc_;
        std::unique_ptr<tcp::resolver> resolver_;
        std::unique_ptr<websocket::stream<tcp::socket>> ws_;
        beast::flat_buffer buffer_;
        std::thread ioThread_;
        std::thread heartbeatThread_;
        std::atomic<bool> started_{false};
        std::atomic<bool> connected_{false};
        std::atomic<bool> closing_{false};
        std::atomic<bool> alive_{true};
        std::atomic<bool> heartbeatEnabled_{false};
        std::atomic<bool> heartbeatStop_{false};
        std::chrono::seconds heartbeatInterval_{30};
        std::atomic<bool> autoReconnect_{false};
        std::chrono::seconds reconnectDelay_{3};
        std::atomic<bool> reconnectScheduled_{false};
        OpenHandler onOpen_;
        MessageHandler onMessage_;
        CloseHandler onClose_;
        ErrorHandler onError_;
        std::mutex writeMutex_;
        std::deque<std::string> writeQueue_;
        bool writeInProgress_{false};
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_CLIENT_HPP
