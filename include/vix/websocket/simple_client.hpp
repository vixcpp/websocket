#ifndef VIX_WEBSOCKET_SIMPLE_CLIENT_HPP
#define VIX_WEBSOCKET_SIMPLE_CLIENT_HPP

/**
 * @file simple_client.hpp
 * @brief High-level WebSocket client for Vix.cpp (Node.js-style).
 *
 * Exemple d'utilisation :
 *
 *   auto client = vix::websocket::SimpleClient::create("localhost", "9090", "/");
 *
 *   client->on_open([]{
 *       std::cout << "Connected to server\n";
 *   });
 *
 *   client->on_message([](const std::string &msg){
 *       std::cout << "Server says: " << msg << "\n";
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

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp>

namespace vix::websocket
{
    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = net::ip::tcp;

    // ------------------------------------------------------------
    // Helpers JSON internes (esprit vix::json)
    // ------------------------------------------------------------

    inline nlohmann::json ws_token_to_nlohmann(const vix::json::token &t)
    {
        nlohmann::json j = nullptr;
        std::visit(
            [&](auto &&val)
            {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    j = nullptr;
                }
                else if constexpr (std::is_same_v<T, bool> ||
                                   std::is_same_v<T, long long> ||
                                   std::is_same_v<T, double> ||
                                   std::is_same_v<T, std::string>)
                {
                    j = val;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::array_t>>)
                {
                    if (!val)
                    {
                        j = nullptr;
                        return;
                    }
                    j = nlohmann::json::array();
                    for (const auto &el : val->elems)
                    {
                        j.push_back(ws_token_to_nlohmann(el));
                    }
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::kvs>>)
                {
                    if (!val)
                    {
                        j = nullptr;
                        return;
                    }
                    nlohmann::json obj = nlohmann::json::object();
                    const auto &a = val->flat;
                    const size_t n = a.size() - (a.size() % 2);
                    for (size_t i = 0; i < n; i += 2)
                    {
                        const auto &k = a[i].v;
                        const auto &vv = a[i + 1];
                        if (!std::holds_alternative<std::string>(k))
                            continue;
                        const auto &key = std::get<std::string>(k);
                        obj[key] = ws_token_to_nlohmann(vv);
                    }
                    j = std::move(obj);
                }
                else
                {
                    j = nullptr;
                }
            },
            t.v);
        return j;
    }

    inline nlohmann::json ws_kvs_to_nlohmann(const vix::json::kvs &list)
    {
        nlohmann::json obj = nlohmann::json::object();
        const auto &a = list.flat;
        const size_t n = a.size() - (a.size() % 2);

        for (size_t i = 0; i < n; i += 2)
        {
            const auto &k = a[i].v;
            const auto &v = a[i + 1];

            if (!std::holds_alternative<std::string>(k))
                continue;
            const std::string &key = std::get<std::string>(k);

            obj[key] = ws_token_to_nlohmann(v);
        }
        return obj;
    }

    // ------------------------------------------------------------
    // JsonMessage : protocole { type, payload{ user, text } }
    // ------------------------------------------------------------

    struct JsonMessage
    {
        std::string raw;
        std::string type;
        std::string user;
        std::string text;

        static JsonMessage parse(const std::string &s)
        {
            JsonMessage m;
            m.raw = s;

            try
            {
                auto j = nlohmann::json::parse(s);
                if (!j.is_object())
                    return m;

                m.type = j.value("type", "");

                if (j.contains("payload") && j["payload"].is_object())
                {
                    const auto &p = j["payload"];
                    m.user = p.value("user", "");
                    m.text = p.value("text", "");
                }
            }
            catch (...)
            {
                // JSON invalide → on garde juste raw
            }

            return m;
        }
    };

    // ------------------------------------------------------------
    // SimpleClient
    // ------------------------------------------------------------

    class SimpleClient : public std::enable_shared_from_this<SimpleClient>
    {
    public:
        using OpenHandler = std::function<void()>;
        using MessageHandler = std::function<void(const std::string &)>;
        using CloseHandler = std::function<void()>;
        using ErrorHandler = std::function<void(const boost::system::error_code &)>;

        /// Fabrique recommandée (garantit enable_shared_from_this)
        static std::shared_ptr<SimpleClient> create(std::string host,
                                                    std::string port,
                                                    std::string target = "/")
        {
            return std::shared_ptr<SimpleClient>(
                new SimpleClient(std::move(host), std::move(port), std::move(target)));
        }

        // ───────────── Handlers / callbacks ─────────────

        void on_open(OpenHandler cb) { onOpen_ = std::move(cb); }
        void on_message(MessageHandler cb) { onMessage_ = std::move(cb); }
        void on_close(CloseHandler cb) { onClose_ = std::move(cb); }
        void on_error(ErrorHandler cb) { onError_ = std::move(cb); }

        // ───────────── Config avancée ─────────────

        /// Active/désactive la reconnexion automatique
        void enable_auto_reconnect(bool enable,
                                   std::chrono::seconds delay = std::chrono::seconds{3})
        {
            autoReconnect_ = enable;
            reconnectDelay_ = delay;
        }

        /// Active l'envoi périodique de ping (heartbeat) côté client
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

        // ───────────── Connexion / boucle I/O ─────────────

        /// Lance la résolution, la connexion, le handshake et le thread I/O.
        void connect()
        {
            if (!alive_ || closing_)
                return;

            bool expected = false;
            if (!started_.compare_exchange_strong(expected, true))
                return; // déjà lancé

            init_io();

            auto self = shared_from_this();

            // Thread I/O qui fait tourner l'event loop
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

            // On poste le début du pipeline (resolve) dans l'io_context
            net::post(*ioc_, [self]()
                      { self->do_resolve(); });
        }

        /// Envoie un message texte (thread-safe via executor)
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
                          self->ws_->async_write(
                              net::buffer(text),
                              [self](const boost::system::error_code &ec, std::size_t)
                              {
                                  if (ec && ec != net::error::operation_aborted)
                                  {
                                      self->emit_error(ec, "write");
                                      self->maybe_schedule_reconnect(ec);
                                  }
                              });
                      });
        }

        /// Envoie un JSON brut (helper interne)
        void send_json(const nlohmann::json &j)
        {
            send_text(j.dump());
        }

        /// Envoie un message protocolaire { type, payload } en JSON
        /// payload en "esprit Vix" via vix::json::token
        void send_json_message(const std::string &type,
                               std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs payload{payloadTokens};
            nlohmann::json payloadJson = ws_kvs_to_nlohmann(payload);

            nlohmann::json j{
                {"type", type},
                {"payload", payloadJson},
            };

            send_text(j.dump());
        }

        /// Envoie un ping explicite (en plus du heartbeat éventuel)
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
                          websocket::ping_data data; // payload vide
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

        /// Fermeture propre
        void close()
        {
            if (closing_.exchange(true))
                return;

            alive_ = false;
            heartbeatStop_ = true;

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

            if (ioThread_.joinable())
                ioThread_.join();

            if (heartbeatThread_.joinable())
                heartbeatThread_.join();
        }

        ~SimpleClient()
        {
            try
            {
                close();
            }
            catch (...)
            {
            }
        }

    private:
        SimpleClient(std::string host, std::string port, std::string target)
            : host_(std::move(host)), port_(std::move(port)), target_(std::move(target))
        {
        }

        // ───────────── Initialisation Asio/Beast ─────────────

        void init_io()
        {
            // Si on reconnecte, s'assurer que tout est bien stoppé
            if (ioc_)
                ioc_->stop();
            if (ioThread_.joinable())
                ioThread_.join();
            if (heartbeatThread_.joinable())
                heartbeatThread_.join();

            ioc_ = std::make_unique<net::io_context>();
            resolver_ = std::make_unique<tcp::resolver>(*ioc_);
            ws_ = std::make_unique<websocket::stream<tcp::socket>>(*ioc_);
            buffer_.consume(buffer_.size());

            connected_ = false;
            closing_ = false;
            heartbeatStop_ = false;
        }

        // ───────────── Pipeline interne : resolve → connect → handshake → read ─────────────

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

            // Utilise la surcharge : (socket, EndpointSequence, handler)
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

                    self->connected_ = true;
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

                        self->connected_ = false;
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

        // ───────────── Heartbeat (ping/pong périodique) ─────────────

        void start_heartbeat()
        {
            if (heartbeatThread_.joinable())
                return;

            auto self = shared_from_this();
            heartbeatThread_ = std::thread([self]()
                                           {
                while (!self->heartbeatStop_ && self->alive_)
                {
                    std::this_thread::sleep_for(self->heartbeatInterval_);
                    if (!self->connected_ || self->closing_ || self->heartbeatStop_)
                        continue;

                    self->send_ping();
                } });
        }

        // ───────────── Reconnexion automatique ─────────────

        void maybe_schedule_reconnect(const boost::system::error_code &ec)
        {
            if (!autoReconnect_ || closing_ || !alive_)
                return;

            // Erreurs "normales" où on ne reconnecte pas automatiquement
            if (ec == websocket::error::closed ||
                ec == net::error::operation_aborted)
                return;

            bool expected = false;
            if (!reconnectScheduled_.compare_exchange_strong(expected, true))
                return; // déjà en attente

            auto self = shared_from_this();
            std::thread([self]()
                        {
                std::this_thread::sleep_for(self->reconnectDelay_);

                if (!self->alive_ || self->closing_)
                {
                    self->reconnectScheduled_ = false;
                    return;
                }

                self->started_ = false;
                self->reconnectScheduled_ = false;
                self->connect(); })
                .detach();
        }

        // ───────────── Erreurs ─────────────

        void emit_error(const boost::system::error_code &ec,
                        const char *stage)
        {
            if (onError_)
            {
                onError_(ec);
            }
            else
            {
                std::cerr << "[SimpleClient][" << stage
                          << "] error: " << ec.message() << "\n";
            }
        }

    private:
        // Config de connexion
        std::string host_;
        std::string port_;
        std::string target_;

        // Asio / Beast
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

        // Heartbeat
        std::atomic<bool> heartbeatEnabled_{false};
        std::atomic<bool> heartbeatStop_{false};
        std::chrono::seconds heartbeatInterval_{30};

        // Reconnexion
        std::atomic<bool> autoReconnect_{false};
        std::chrono::seconds reconnectDelay_{3};
        std::atomic<bool> reconnectScheduled_{false};

        // Callbacks
        OpenHandler onOpen_;
        MessageHandler onMessage_;
        CloseHandler onClose_;
        ErrorHandler onError_;
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SIMPLE_CLIENT_HPP
