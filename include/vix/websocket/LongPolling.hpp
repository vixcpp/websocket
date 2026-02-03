/**
 *
 *  @file LongPolling.hpp
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
#ifndef VIX_LONG_POLLING_HPP
#define VIX_LONG_POLLING_HPP

#include <vix/websocket/protocol.hpp>
#include <vix/websocket/Metrics.hpp>

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vix::websocket
{
  /**
   * @brief In-memory long-polling session for WS-compatible JsonMessage delivery.
   *
   * Stores a bounded FIFO buffer of JsonMessage and tracks last activity time.
   * Used by LongPollingManager to emulate push-style delivery over HTTP polling.
   */
  struct LongPollingSession
  {
    std::string id;
    std::chrono::steady_clock::time_point lastSeen;
    std::deque<JsonMessage> buffer;

    LongPollingSession() = default;

    /** @brief Create a long-polling session with an id and fresh lastSeen. */
    explicit LongPollingSession(std::string sessionId)
        : id(std::move(sessionId)),
          lastSeen(std::chrono::steady_clock::now()),
          buffer()
    {
    }

    /** @brief Update lastSeen to now. */
    void touch() noexcept
    {
      lastSeen = std::chrono::steady_clock::now();
    }

    /** @brief Check if the session exceeded TTL relative to @p now. */
    bool is_expired(std::chrono::seconds ttl,
                    std::chrono::steady_clock::time_point now) const noexcept
    {
      return (now - lastSeen) > ttl;
    }

    /**
     * @brief Enqueue a message and cap the buffer size.
     *
     * Oldest messages are dropped if maxBufferSize is exceeded.
     */
    void enqueue(const JsonMessage &msg, std::size_t maxBufferSize)
    {
      buffer.push_back(msg);
      if (buffer.size() > maxBufferSize)
      {
        buffer.pop_front();
      }
      touch();
    }

    /**
     * @brief Drain up to @p maxCount messages (FIFO).
     *
     * Moves messages out of the internal buffer and refreshes lastSeen.
     */
    std::vector<JsonMessage> drain(std::size_t maxCount)
    {
      std::vector<JsonMessage> out;
      if (maxCount == 0 || buffer.empty())
        return out;

      const std::size_t n = std::min(maxCount, buffer.size());
      out.reserve(n);

      for (std::size_t i = 0; i < n; ++i)
      {
        out.push_back(std::move(buffer.front()));
        buffer.pop_front();
      }

      touch();
      return out;
    }
  };

  /**
   * @brief Thread-safe manager for long-polling sessions and buffers.
   *
   * Supports pushing typed JsonMessage to a session, polling batched messages,
   * sweeping expired sessions, and optional metrics tracking.
   */
  class LongPollingManager
  {
  public:
    using SessionId = std::string;

    /**
     * @brief Construct a manager with TTL, buffer limits, and optional metrics.
     *
     * @param sessionTtl Inactivity TTL before sessions are removed.
     * @param maxBufferPerSession Max queued messages per session (FIFO).
     * @param metrics Optional metrics collector (may be null).
     */
    LongPollingManager(
        std::chrono::seconds sessionTtl = std::chrono::seconds{60},
        std::size_t maxBufferPerSession = 256,
        WebSocketMetrics *metrics = nullptr);

    LongPollingManager(const LongPollingManager &) = delete;
    LongPollingManager &operator=(const LongPollingManager &) = delete;

    /** @brief Move-construct (mutex is reinitialized). */
    LongPollingManager(LongPollingManager &&other) noexcept;
    /** @brief Move-assign (mutex is reinitialized). */
    LongPollingManager &operator=(LongPollingManager &&other) noexcept;

    /**
     * @brief Push a message to a specific session buffer.
     *
     * If the session does not exist it is created.
     */
    void push_to(const SessionId &sessionId, const JsonMessage &message);

    /**
     * @brief Poll messages for a session (up to maxMessages).
     *
     * @param sessionId Target session id.
     * @param maxMessages Max messages to return (FIFO).
     * @param createIfMissing Create session when missing.
     */
    std::vector<JsonMessage> poll(const SessionId &sessionId, std::size_t maxMessages = 50, bool createIfMissing = true);

    /**
     * @brief Remove expired sessions based on lastSeen and TTL.
     */
    void sweep_expired();

    /** @brief Number of active sessions. */
    std::size_t session_count() const;

    /** @brief Current buffer size for a session (0 if missing). */
    std::size_t buffer_size(const SessionId &sessionId) const;

  private:
    /** @brief Get existing session or create it (requires mutex held). */
    LongPollingSession &get_or_create_unlocked(const SessionId &sessionId);

    std::chrono::seconds sessionTtl_;
    std::size_t maxBufferPerSession_;
    mutable std::mutex mutex_;
    std::unordered_map<SessionId, LongPollingSession> sessions_;
    WebSocketMetrics *metrics_;
  };

} // namespace vix::websocket

#endif
