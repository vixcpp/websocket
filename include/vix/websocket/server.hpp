/**
 *
 *  @file server.hpp
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
#ifndef VIX_WEBSOCKET_SERVER_HPP
#define VIX_WEBSOCKET_SERVER_HPP

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/json/Simple.hpp>
#include <vix/utils/Logger.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/router.hpp>
#include <vix/websocket/session.hpp>
#include <vix/websocket/websocket.hpp>

namespace vix::websocket
{
  /**
   * @brief High-level WebSocket server with routing, rooms, and typed messages.
   *
   * Wraps the low-level engine and router, manages active sessions, supports
   * broadcast (global or per-room), and optionally forwards typed JSON messages
   * to a LongPollingBridge.
   *
   * This Vix version is independent of Boost and uses the generic executor
   * abstraction so it can run on different executor implementations. :contentReference[oaicite:0]{index=0}
   */
  class Server
  {
  public:
    /** @brief Called when a session is opened. */
    using OpenHandler = std::function<void(Session &)>;
    /** @brief Called when a session is closed. */
    using CloseHandler = std::function<void(Session &)>;
    /** @brief Called on session errors. */
    using ErrorHandler = std::function<void(Session &, const std::string &)>;
    /** @brief Called on raw text frames. */
    using MessageHandler = std::function<void(Session &, const std::string &)>;
    /** @brief Called on typed {type,payload} JSON messages. */
    using TypedMessageHandler =
        std::function<void(Session &, const std::string &, const vix::json::kvs &)>;
    /** @brief Room identifier. */
    using RoomId = std::string;

    /**
     * @brief Construct a WebSocket server.
     *
     * @param cfg Config provider used for port and engine settings.
     * @param executor Shared runtime executor used by the WebSocket engine.
     */
    explicit Server(
        vix::config::Config &cfg,
        std::shared_ptr<vix::executor::RuntimeExecutor> executor)
        : cfg_(cfg),
          executor_(std::move(executor)),
          router_(std::make_shared<Router>()),
          engine_(cfg_, executor_, router_),
          sessionsMutex_(),
          sessions_(),
          rooms_(),
          longPollingBridge_(nullptr),
          userOnOpen_(),
          userOnClose_(),
          userOnError_(),
          userOnMessage_(),
          userOnTypedMessage_()
    {
      if (!executor_)
      {
        throw std::invalid_argument(
            "vix::websocket::Server requires a valid runtime executor");
      }

      router_->on_open(
          [this](Session &s)
          {
            register_session(s.shared_from_this());

            if (userOnOpen_)
            {
              userOnOpen_(s);
            }
          });

      router_->on_close(
          [this](Session &s)
          {
            auto sp = s.shared_from_this();
            unregister_session(sp);
            remove_session_from_all_rooms(sp);

            if (userOnClose_)
            {
              userOnClose_(s);
            }
          });

      router_->on_error(
          [this](Session &s, const std::string &err)
          {
            if (userOnError_)
            {
              userOnError_(s, err);
            }
          });

      router_->on_message(
          [this](Session &s, std::string payload)
          {
            if (userOnMessage_)
            {
              userOnMessage_(s, payload);
            }

            auto parsed = JsonMessage::parse(payload);
            if (!parsed)
            {
              return;
            }

            if (longPollingBridge_)
            {
              longPollingBridge_->on_ws_message(*parsed);
            }

            if (userOnTypedMessage_)
            {
              userOnTypedMessage_(s, parsed->type, parsed->payload);
            }
          });
    }

    /**
     * @brief Construct a WebSocket server from an owning executor.
     *
     * @param cfg Config provider used for port and engine settings.
     * @param executor Unique runtime executor transferred to this server.
     */
    explicit Server(
        vix::config::Config &cfg,
        std::unique_ptr<vix::executor::RuntimeExecutor> executor)
        : Server(
              cfg,
              std::shared_ptr<vix::executor::RuntimeExecutor>(std::move(executor)))
    {
    }

    /** @brief Set the on-open handler. */
    void on_open(OpenHandler fn)
    {
      userOnOpen_ = std::move(fn);
    }

    /** @brief Set the on-close handler. */
    void on_close(CloseHandler fn)
    {
      userOnClose_ = std::move(fn);
    }

    /** @brief Set the on-error handler. */
    void on_error(ErrorHandler fn)
    {
      userOnError_ = std::move(fn);
    }

    /** @brief Set the on-message handler for raw text frames. */
    void on_message(MessageHandler fn)
    {
      userOnMessage_ = std::move(fn);
    }

    /**
     * @brief Set the typed message handler for the {type,payload} JSON convention.
     *
     * @param fn Callback invoked when an incoming message matches the typed JSON format.
     */
    void on_typed_message(TypedMessageHandler fn)
    {
      userOnTypedMessage_ = std::move(fn);
    }

    /**
     * @brief Start the WebSocket engine.
     */
    void start()
    {
      vix::utils::Logger::getInstance().log(
          vix::utils::Logger::Level::Debug,
          "[ws] start() called on port {}",
          port());

      engine_.run();
    }

    /**
     * @brief Request an asynchronous stop of the WebSocket engine.
     *
     * This function is non-blocking and only signals shutdown to the internal
     * low-level engine. It does not join worker threads.
     */
    void stop_async()
    {
      engine_.stop_async();
    }

    /**
     * @brief Join internal WebSocket worker threads.
     *
     * This function blocks until the low-level engine threads have exited.
     */
    void join()
    {
      engine_.join_threads();
    }

    /**
     * @brief Stop the WebSocket engine and join its internal threads.
     *
     * This is the blocking shutdown path.
     */
    void stop()
    {
      stop_async();
      join();
    }

    /**
     * @brief Start the server and block until stop is requested.
     */
    void listen_blocking()
    {
      start();

      while (!engine_.is_stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }

    /**
     * @brief Return the configured listening port.
     *
     * @return Configured WebSocket port, defaulting to 9090.
     */
    int port() const
    {
      return cfg_.getInt("websocket.port", 9090);
    }

    /**
     * @brief Broadcast a raw text frame to all connected sessions.
     *
     * @param text UTF-8 text payload to broadcast.
     */
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

    /**
     * @brief Broadcast a typed {type,payload} JSON message to all sessions.
     *
     * @param type Logical message type.
     * @param payload Key-value payload.
     */
    void broadcast_json(const std::string &type, const vix::json::kvs &payload)
    {
      broadcast_text(JsonMessage::serialize(type, payload));
    }

    /**
     * @brief Broadcast a typed {type,payload} JSON message to all sessions.
     *
     * Example:
     * @code
     * server.broadcast_json("chat.message", { "user", "alice", "text", "hello" });
     * @endcode
     *
     * @param type Logical message type.
     * @param payloadTokens Payload tokens forwarded to vix::json::kvs.
     */
    void broadcast_json(
        const std::string &type,
        std::initializer_list<vix::json::token> payloadTokens)
    {
      vix::json::kvs kv{payloadTokens};
      broadcast_text(JsonMessage::serialize(type, kv));
    }

    /**
     * @brief Add a session to a room.
     *
     * This operation is idempotent for the same session and room.
     *
     * @param session Session to add.
     * @param room Target room identifier.
     */
    void join_room(Session &session, const RoomId &room)
    {
      auto sp = session.shared_from_this();
      std::lock_guard<std::mutex> lock(sessionsMutex_);

      cleanup_sessions_locked();
      cleanup_rooms_locked();

      auto &vec = rooms_[room];

      auto it = std::find_if(
          vec.begin(),
          vec.end(),
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

    /**
     * @brief Remove a session from a room.
     *
     * @param session Session to remove.
     * @param room Room identifier.
     */
    void leave_room(Session &session, const RoomId &room)
    {
      auto sp = session.shared_from_this();
      std::lock_guard<std::mutex> lock(sessionsMutex_);

      auto itRoom = rooms_.find(room);
      if (itRoom == rooms_.end())
      {
        return;
      }

      auto &vec = itRoom->second;
      vec.erase(
          std::remove_if(
              vec.begin(),
              vec.end(),
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

    /**
     * @brief Remove a session from all rooms.
     *
     * @param session Session to remove.
     */
    void leave_all_rooms(Session &session)
    {
      auto sp = session.shared_from_this();
      std::lock_guard<std::mutex> lock(sessionsMutex_);
      remove_session_from_all_rooms_locked(sp);
    }

    /**
     * @brief Broadcast a raw text frame to all sessions in a room.
     *
     * @param room Room identifier.
     * @param text UTF-8 text payload.
     */
    void broadcast_room_text(const RoomId &room, const std::string &text)
    {
      std::lock_guard<std::mutex> lock(sessionsMutex_);

      cleanup_sessions_locked();
      cleanup_rooms_locked();

      auto it = rooms_.find(room);
      if (it == rooms_.end())
      {
        return;
      }

      auto &vec = it->second;
      for (auto &weak : vec)
      {
        if (auto s = weak.lock())
        {
          s->send_text(text);
        }
      }
    }

    /**
     * @brief Broadcast a typed {type,payload} JSON message to a specific room.
     *
     * @param room Room identifier.
     * @param type Logical message type.
     * @param payload Key-value payload.
     */
    void broadcast_room_json(
        const RoomId &room,
        const std::string &type,
        const vix::json::kvs &payload)
    {
      broadcast_room_text(room, JsonMessage::serialize(type, payload));
    }

    /**
     * @brief Broadcast a typed {type,payload} JSON message to a room.
     *
     * @param room Room identifier.
     * @param type Logical message type.
     * @param payloadTokens Payload tokens forwarded to vix::json::kvs.
     */
    void broadcast_room_json(
        const RoomId &room,
        const std::string &type,
        std::initializer_list<vix::json::token> payloadTokens)
    {
      vix::json::kvs kv{payloadTokens};
      broadcast_room_text(room, JsonMessage::serialize(type, kv));
    }

    /**
     * @brief Attach a long-polling bridge to receive typed JsonMessage events.
     *
     * Once attached, every parsed JsonMessage will be forwarded to the bridge.
     *
     * @param bridge Shared long-polling bridge instance.
     */
    void attach_long_polling_bridge(std::shared_ptr<LongPollingBridge> bridge)
    {
      longPollingBridge_ = std::move(bridge);
    }

    /**
     * @brief Return the currently attached long-polling bridge.
     *
     * @return Shared bridge instance, or null if none is attached.
     */
    std::shared_ptr<LongPollingBridge> long_polling_bridge() const noexcept
    {
      return longPollingBridge_;
    }

  private:
    /**
     * @brief Track a newly opened session.
     *
     * @param s Shared session instance.
     */
    void register_session(std::shared_ptr<Session> s)
    {
      std::lock_guard<std::mutex> lock(sessionsMutex_);
      sessions_.emplace_back(std::move(s));
    }

    /**
     * @brief Remove a session from the tracked session list.
     *
     * @param s Shared session instance.
     */
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

    /**
     * @brief Drop expired sessions from the global session list.
     *
     * Must be called while holding sessionsMutex_.
     */
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

    /**
     * @brief Drop expired sessions from rooms and remove empty rooms.
     *
     * Must be called while holding sessionsMutex_.
     */
    void cleanup_rooms_locked()
    {
      for (auto it = rooms_.begin(); it != rooms_.end();)
      {
        auto &vec = it->second;
        vec.erase(
            std::remove_if(
                vec.begin(),
                vec.end(),
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

    /**
     * @brief Remove a session from all rooms.
     *
     * This overload acquires the internal session mutex.
     *
     * @param s Shared session instance.
     */
    void remove_session_from_all_rooms(std::shared_ptr<Session> s)
    {
      std::lock_guard<std::mutex> lock(sessionsMutex_);
      remove_session_from_all_rooms_locked(std::move(s));
    }

    /**
     * @brief Remove a session from all rooms.
     *
     * Requires sessionsMutex_ to already be held.
     *
     * @param s Shared session instance.
     */
    void remove_session_from_all_rooms_locked(std::shared_ptr<Session> s)
    {
      for (auto it = rooms_.begin(); it != rooms_.end();)
      {
        auto &vec = it->second;
        vec.erase(
            std::remove_if(
                vec.begin(),
                vec.end(),
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
    /** @brief WebSocket configuration source. */
    vix::config::Config &cfg_;

    /** @brief Shared runtime executor used by the WebSocket layer. */
    std::shared_ptr<vix::executor::RuntimeExecutor> executor_;

    /** @brief High-level event router. */
    std::shared_ptr<Router> router_;

    /** @brief Low-level WebSocket server engine. */
    LowLevelServer engine_;

    /** @brief Mutex protecting sessions_ and rooms_. */
    std::mutex sessionsMutex_;

    /** @brief Weak list of active sessions. */
    std::vector<std::weak_ptr<Session>> sessions_;

    /** @brief Room membership table. */
    std::unordered_map<RoomId, std::vector<std::weak_ptr<Session>>> rooms_;

    /** @brief Optional long-polling bridge receiving typed WebSocket events. */
    std::shared_ptr<LongPollingBridge> longPollingBridge_;

    /** @brief User callback invoked on session open. */
    OpenHandler userOnOpen_{};

    /** @brief User callback invoked on session close. */
    CloseHandler userOnClose_{};

    /** @brief User callback invoked on session error. */
    ErrorHandler userOnError_{};

    /** @brief User callback invoked on raw text message. */
    MessageHandler userOnMessage_{};

    /** @brief User callback invoked on typed JSON message. */
    TypedMessageHandler userOnTypedMessage_{};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SERVER_HPP
