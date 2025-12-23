#pragma once

/**
 * @file LongPolling.hpp
 * @brief Internal helpers for WebSocket fallback long-polling.
 */

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
    struct LongPollingSession
    {
        std::string id;
        std::chrono::steady_clock::time_point lastSeen;
        std::deque<JsonMessage> buffer;

        LongPollingSession() = default;

        explicit LongPollingSession(std::string sessionId)
            : id(std::move(sessionId)),
              lastSeen(std::chrono::steady_clock::now()),
              buffer()
        {
        }

        void touch() noexcept
        {
            lastSeen = std::chrono::steady_clock::now();
        }

        bool is_expired(std::chrono::seconds ttl,
                        std::chrono::steady_clock::time_point now) const noexcept
        {
            return (now - lastSeen) > ttl;
        }

        void enqueue(const JsonMessage &msg, std::size_t maxBufferSize)
        {
            buffer.push_back(msg);
            if (buffer.size() > maxBufferSize)
            {
                buffer.pop_front();
            }
            touch();
        }

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

    class LongPollingManager
    {
    public:
        using SessionId = std::string;

        /// @param sessionTtl          TTL des sessions inactives
        /// @param maxBufferPerSession taille max du buffer par session
        /// @param metrics             pointeur optionnel vers WebSocketMetrics
        LongPollingManager(std::chrono::seconds sessionTtl = std::chrono::seconds{60},
                           std::size_t maxBufferPerSession = 256,
                           WebSocketMetrics *metrics = nullptr);

        LongPollingManager(const LongPollingManager &) = delete;
        LongPollingManager &operator=(const LongPollingManager &) = delete;
        LongPollingManager(LongPollingManager &&other) noexcept;
        LongPollingManager &operator=(LongPollingManager &&other) noexcept;
        void push_to(const SessionId &sessionId, const JsonMessage &message);
        std::vector<JsonMessage> poll(const SessionId &sessionId, std::size_t maxMessages = 50, bool createIfMissing = true);
        void sweep_expired();
        std::size_t session_count() const;
        std::size_t buffer_size(const SessionId &sessionId) const;

    private:
        LongPollingSession &get_or_create_unlocked(const SessionId &sessionId);

    private:
        std::chrono::seconds sessionTtl_;
        std::size_t maxBufferPerSession_;
        mutable std::mutex mutex_;
        std::unordered_map<SessionId, LongPollingSession> sessions_;
        WebSocketMetrics *metrics_;
    };

} // namespace vix::websocket
