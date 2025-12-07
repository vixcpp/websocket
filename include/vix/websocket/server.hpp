#ifndef VIX_WEBSOCKET_SERVER_HPP
#define VIX_WEBSOCKET_SERVER_HPP

/**
 * @file server.hpp
 * @brief High-level WebSocket server with event-driven API and JSON helpers.
 *
 * This component:
 *   - exposes an event-driven interface (open, close, error, message)
 *   - manages routing, session tracking and I/O threads lifecycle
 *   - provides helpers for a { type, payload } JSON message convention
 *     using vix::json::kvs as public representation
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
#include <string_view>
#include <unordered_map>

#include <boost/system/error_code.hpp>

#include <vix/config/Config.hpp>
#include <vix/executor/IExecutor.hpp>

#include <vix/websocket/websocket.hpp> // LowLevelServer
#include <vix/websocket/router.hpp>    // Router
#include <vix/websocket/session.hpp>   // Session
#include <vix/websocket/protocol.hpp>  // JsonMessage

namespace vix::websocket
{
    class Server
    {
    public:
        using OpenHandler = std::function<void(Session &)>;
        using CloseHandler = std::function<void(Session &)>;
        using ErrorHandler = std::function<void(Session &, const boost::system::error_code &)>;
        using MessageHandler = std::function<void(Session &, const std::string &)>;
        using TypedMessageHandler = std::function<void(Session &, const std::string &, const vix::json::kvs &)>;

        using RoomId = std::string;

        Server(vix::config::Config &cfg,
               std::shared_ptr<vix::executor::IExecutor> executor)
            : cfg_(cfg),
              executor_(std::move(executor)),
              router_(std::make_shared<Router>()),
              engine_(cfg_, executor_, router_)
        {
            // Connect router to internal handlers
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
                    auto sp = s.shared_from_this();
                    unregister_session(sp);
                    // Also remove this session from all rooms
                    remove_session_from_all_rooms(sp);
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

                    // Raw string handler
                    if (userOnMessage_)
                        userOnMessage_(s, payload);

                    // Optional typed JSON { type, payload } handler
                    if (userOnTypedMessage_)
                    {
                        if (auto parsed = JsonMessage::parse(payload))
                        {
                            userOnTypedMessage_(s, parsed->type, parsed->payload);
                        }
                    }
                });
        }

        Server(vix::config::Config &cfg,
               std::unique_ptr<vix::executor::IExecutor> executor)
            : Server(cfg,
                     std::shared_ptr<vix::executor::IExecutor>(std::move(executor)))
        {
        }

        // ───────────── Event-driven API ─────────────

        void on_open(OpenHandler fn) { userOnOpen_ = std::move(fn); }
        void on_close(CloseHandler fn) { userOnClose_ = std::move(fn); }
        void on_error(ErrorHandler fn) { userOnError_ = std::move(fn); }
        void on_message(MessageHandler fn) { userOnMessage_ = std::move(fn); }

        /// Handler for the { type, payload } JSON convention using vix::json::kvs.
        void on_typed_message(TypedMessageHandler fn)
        {
            userOnTypedMessage_ = std::move(fn);
        }

        // ───────────── Start / stop lifecycle ─────────────

        /// Starts I/O threads (non-blocking).
        void start()
        {
            engine_.run();
        }

        /// Cooperative stop + join of worker threads.
        void stop()
        {
            engine_.stop_async();
            engine_.join_threads();
        }

        /// Convenience API: start and block the calling thread until stop is requested.
        void listen_blocking()
        {
            start();

            while (!engine_.is_stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            stop();
        }

        /// Returns the effective WebSocket port (from configuration).
        int port() const
        {
            return cfg_.getInt("websocket.port", 9090);
        }

        // ───────────── Broadcast helpers (global) ─────────────

        /// Broadcasts a text message to all active sessions.
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

        /// Broadcasts a { type, payload } JSON message using vix::json::kvs.
        void broadcast_json(const std::string &type, const vix::json::kvs &payload)
        {
            broadcast_text(JsonMessage::serialize(type, payload));
        }

        /// Broadcasts a { type, payload } JSON message using vix::json::token list.
        ///
        /// Example:
        ///   server.broadcast_json("chat.message", { "user", "alice", "text", "hello" });
        void broadcast_json(const std::string &type,
                            std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs kv{payloadTokens};
            broadcast_text(JsonMessage::serialize(type, kv));
        }

        // ───────────── Room management API ─────────────
        //
        // Rooms are simple named channels. A session can join multiple rooms.
        // All operations are thread-safe.

        /// Add a session to a room (idempotent).
        void join_room(Session &session, const RoomId &room)
        {
            auto sp = session.shared_from_this();
            std::lock_guard<std::mutex> lock(sessionsMutex_);

            cleanup_sessions_locked();
            cleanup_rooms_locked();

            auto &vec = rooms_[room];

            // Avoid duplicates
            auto it = std::find_if(
                vec.begin(), vec.end(),
                [&sp](const std::weak_ptr<Session> &w)
                {
                    auto wp = w.lock();
                    return wp && wp.get() == sp.get();
                });

            if (it == vec.end())
            {
                vec.emplace_back(sp);
            }
        }

        /// Remove a session from a specific room.
        void leave_room(Session &session, const RoomId &room)
        {
            auto sp = session.shared_from_this();
            std::lock_guard<std::mutex> lock(sessionsMutex_);

            auto itRoom = rooms_.find(room);
            if (itRoom == rooms_.end())
                return;

            auto &vec = itRoom->second;
            vec.erase(
                std::remove_if(
                    vec.begin(), vec.end(),
                    [&sp](const std::weak_ptr<Session> &w)
                    {
                        auto wp = w.lock();
                        return !wp || wp.get() == sp.get();
                    }),
                vec.end());

            if (vec.empty())
            {
                rooms_.erase(itRoom);
            }
        }

        /// Remove a session from all rooms where it is present.
        void leave_all_rooms(Session &session)
        {
            auto sp = session.shared_from_this();
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            remove_session_from_all_rooms_locked(sp);
        }

        /// Broadcast plain text to a specific room.
        void broadcast_room_text(const RoomId &room, const std::string &text)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);

            cleanup_sessions_locked();
            cleanup_rooms_locked();

            auto it = rooms_.find(room);
            if (it == rooms_.end())
                return;

            auto &vec = it->second;
            for (auto &weak : vec)
            {
                if (auto s = weak.lock())
                {
                    s->send_text(text);
                }
            }
        }

        /// Broadcast {type, payload} JSON to a specific room.
        void broadcast_room_json(const RoomId &room,
                                 const std::string &type,
                                 const vix::json::kvs &payload)
        {
            broadcast_room_text(room, JsonMessage::serialize(type, payload));
        }

        /// Broadcast {type, payload} JSON to a specific room with token list.
        void broadcast_room_json(const RoomId &room,
                                 const std::string &type,
                                 std::initializer_list<vix::json::token> payloadTokens)
        {
            vix::json::kvs kv{payloadTokens};
            broadcast_room_text(room, JsonMessage::serialize(type, kv));
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

        void cleanup_rooms_locked()
        {
            for (auto it = rooms_.begin(); it != rooms_.end();)
            {
                auto &vec = it->second;
                vec.erase(
                    std::remove_if(
                        vec.begin(), vec.end(),
                        [](const std::weak_ptr<Session> &w)
                        {
                            return w.expired();
                        }),
                    vec.end());

                if (vec.empty())
                {
                    it = rooms_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void remove_session_from_all_rooms(std::shared_ptr<Session> s)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            remove_session_from_all_rooms_locked(s);
        }

        void remove_session_from_all_rooms_locked(std::shared_ptr<Session> s)
        {
            for (auto it = rooms_.begin(); it != rooms_.end();)
            {
                auto &vec = it->second;
                vec.erase(
                    std::remove_if(
                        vec.begin(), vec.end(),
                        [&s](const std::weak_ptr<Session> &w)
                        {
                            auto sp = w.lock();
                            return !sp || sp.get() == s.get();
                        }),
                    vec.end());

                if (vec.empty())
                {
                    it = rooms_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

    private:
        vix::config::Config &cfg_;
        std::shared_ptr<vix::executor::IExecutor> executor_;
        std::shared_ptr<Router> router_;
        LowLevelServer engine_; // low-level engine from websocket.hpp

        // Active sessions (non-owning)
        std::mutex sessionsMutex_;
        std::vector<std::weak_ptr<Session>> sessions_;

        // Rooms: room id -> list of sessions (non-owning)
        std::unordered_map<RoomId, std::vector<std::weak_ptr<Session>>> rooms_;

        // User callbacks
        OpenHandler userOnOpen_;
        CloseHandler userOnClose_;
        ErrorHandler userOnError_;
        MessageHandler userOnMessage_;
        TypedMessageHandler userOnTypedMessage_;
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SERVER_HPP
