#ifndef VIX_WEBSOCKET_SIMPLE_SERVER_HPP
#define VIX_WEBSOCKET_SIMPLE_SERVER_HPP

/**
 * @file simple_server.hpp
 * @brief High-level, Node.js-style WebSocket API on top of vix::websocket::Server.
 *
 * Objectif :
 *   - Rendre l’usage du WebSocket aussi simple qu’en Node.js / Deno / Python
 *   - Cacher la plomberie : Router, Server, threads I/O
 *   - Fournir un helper JSON { type, payload } + broadcast
 *
 * Exemple :
 *
 *   vix::config::Config cfg{"config/config.json"};
 *   auto exec = make_threadpool_executor(...);
 *   vix::websocket::SimpleServer ws(cfg, exec);
 *
 *   ws.on_open([](auto &session) {
 *       // petit message système
 *       ws.broadcast_json("chat.system", {
 *           "user",   "server",
 *           "text",   "Welcome to Softadastra Chat 👋",
 *       });
 *   });
 *
 *   ws.on_typed_message([&ws](auto &session,
 *                             const std::string &type,
 *                             const nlohmann::json &payload) {
 *       if (type == "chat.message") {
 *           ws.broadcast_json("chat.message", payload);
 *       }
 *   });
 *
 *   ws.listen_blocking();
 */

#include <memory>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <optional>
#include <functional>
#include <algorithm>
#include <string>

#include <boost/system/error_code.hpp>

#include <vix/config/Config.hpp>
#include <vix/executor/IExecutor.hpp>

#include <vix/websocket/websocket.hpp> // vix::websocket::Server
#include <vix/websocket/router.hpp>    // vix::websocket::Router
#include <vix/websocket/session.hpp>   // vix::websocket::Session

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp> // vix::json::token / kvs

namespace vix::websocket
{
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
    // JsonMessage : protocole { type, payload }
    // ------------------------------------------------------------

    struct JsonMessage
    {
        std::string type;
        nlohmann::json payload;

        static std::optional<JsonMessage> parse(const std::string &s)
        {
            try
            {
                auto j = nlohmann::json::parse(s);
                if (!j.is_object())
                    return std::nullopt;

                JsonMessage msg;
                msg.type = j.value("type", "");

                if (j.contains("payload"))
                    msg.payload = j["payload"];
                else
                    msg.payload = nlohmann::json::object();

                return msg;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        /// Version brute nlohmann::json
        static std::string serialize(const std::string &type,
                                     const nlohmann::json &payload)
        {
            nlohmann::json j{
                {"type", type},
                {"payload", payload},
            };
            return j.dump();
        }

        /// Version "esprit Vix" (kvs → JSON)
        static std::string serialize(const std::string &type,
                                     const vix::json::kvs &payloadKvs)
        {
            nlohmann::json payload = ws_kvs_to_nlohmann(payloadKvs);
            nlohmann::json j{
                {"type", type},
                {"payload", payload},
            };
            return j.dump();
        }
    };

    // ------------------------------------------------------------
    // SimpleServer : façade haut niveau
    // ------------------------------------------------------------

    class SimpleServer
    {
    public:
        using OpenHandler = std::function<void(Session &)>;
        using CloseHandler = std::function<void(Session &)>;
        using ErrorHandler = std::function<void(Session &, const boost::system::error_code &)>;
        using MessageHandler = std::function<void(Session &, const std::string &)>;
        using TypedMessageHandler = std::function<void(Session &, const std::string &, const nlohmann::json &)>;

        SimpleServer(vix::config::Config &cfg,
                     std::shared_ptr<vix::executor::IExecutor> executor)
            : cfg_(cfg),
              executor_(std::move(executor)),
              router_(std::make_shared<Router>()),
              server_(cfg_, executor_, router_)
        {
            // On connecte le Router aux handlers internes qui vont :
            //  - gérer la liste des sessions
            //  - appeler les callbacks utilisateur
            router_->on_open(
                [this](Session &s)
                {
                    register_session(s.shared_from_this());
                    if (userOnOpen_)
                        userOnOpen_(s);
                });

            router_->on_close(
                [this](Session &s)
                {
                    unregister_session(s.shared_from_this());
                    if (userOnClose_)
                        userOnClose_(s);
                });

            router_->on_error(
                [this](Session &s, const boost::system::error_code &ec)
                {
                    if (userOnError_)
                        userOnError_(s, ec);
                });

            router_->on_message(
                [this](Session &s, std::string_view payloadView)
                {
                    std::string payload{payloadView};

                    // Handler brut (string)
                    if (userOnMessage_)
                        userOnMessage_(s, payload);

                    // Handler typé JSON type/payload (optionnel)
                    if (userOnTypedMessage_)
                    {
                        if (auto parsed = JsonMessage::parse(payload))
                        {
                            userOnTypedMessage_(s, parsed->type, parsed->payload);
                        }
                    }
                });
        }

        SimpleServer(vix::config::Config &cfg,
                     std::unique_ptr<vix::executor::IExecutor> executor)
            : SimpleServer(cfg,
                           std::shared_ptr<vix::executor::IExecutor>(std::move(executor)))
        {
        }

        // ───────────── API évènementielle façon Node.js ─────────────

        void on_open(OpenHandler fn) { userOnOpen_ = std::move(fn); }
        void on_close(CloseHandler fn) { userOnClose_ = std::move(fn); }
        void on_error(ErrorHandler fn) { userOnError_ = std::move(fn); }
        void on_message(MessageHandler fn) { userOnMessage_ = std::move(fn); }

        /// Handler pour le petit protocole JSON { type, payload }
        void on_typed_message(TypedMessageHandler fn)
        {
            userOnTypedMessage_ = std::move(fn);
        }

        // ───────────── Démarrage / arrêt ─────────────

        /// Démarre les threads I/O (non bloquant).
        void start()
        {
            server_.run();
        }

        /// Arrêt coopératif + join des threads.
        void stop()
        {
            server_.stop_async();
            server_.join_threads();
        }

        /// API ultra simple : démarre et bloque le thread appelant.
        void listen_blocking()
        {
            start();

            while (!server_.is_stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            stop();
        }

        /// Retourne le port WebSocket effectif (issu de la config).
        int port() const
        {
            return cfg_.getInt("websocket.port", 9090);
        }

        // ───────────── Broadcast helpers ─────────────

        /// Diffuse un message texte à toutes les sessions actives
        void broadcast_text(const std::string &text)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            cleanup_sessions_locked();

            for (auto &weak : sessions_)
            {
                if (auto s = weak.lock())
                {
                    s->send_text(text);
                }
            }
        }

        /// Diffuse un JSON brut (nlohmann)
        void broadcast_json_raw(const nlohmann::json &j)
        {
            broadcast_text(j.dump());
        }

        /// Diffuse un message de protocole { type, payload } avec nlohmann::json
        void broadcast_json(const std::string &type, const nlohmann::json &payload)
        {
            broadcast_text(JsonMessage::serialize(type, payload));
        }

        /// Diffuse un message de protocole { type, payload } "esprit Vix"
        /// Exemple d'appel :
        ///   ws.broadcast_json("chat.message", { "user", "alice", "text", "salut" });
        void broadcast_json(const std::string &type,
                            std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs kv{payloadTokens};
            broadcast_text(JsonMessage::serialize(type, kv));
        }

    private:
        void register_session(std::shared_ptr<Session> s)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_.emplace_back(std::move(s));
        }

        void unregister_session(std::shared_ptr<Session> s)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);

            sessions_.erase(
                std::remove_if(
                    sessions_.begin(),
                    sessions_.end(),
                    [&s](const std::weak_ptr<Session> &w)
                    {
                        auto sp = w.lock();
                        return !sp || sp.get() == s.get();
                    }),
                sessions_.end());
        }

        void cleanup_sessions_locked()
        {
            sessions_.erase(
                std::remove_if(
                    sessions_.begin(),
                    sessions_.end(),
                    [](const std::weak_ptr<Session> &w)
                    {
                        return w.expired();
                    }),
                sessions_.end());
        }

    private:
        vix::config::Config &cfg_;
        std::shared_ptr<vix::executor::IExecutor> executor_;
        std::shared_ptr<Router> router_;
        Server server_;

        // Sessions actives (non-possessif)
        std::mutex sessionsMutex_;
        std::vector<std::weak_ptr<Session>> sessions_;

        // Callbacks utilisateurs
        OpenHandler userOnOpen_;
        CloseHandler userOnClose_;
        ErrorHandler userOnError_;
        MessageHandler userOnMessage_;
        TypedMessageHandler userOnTypedMessage_;
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SIMPLE_SERVER_HPP
