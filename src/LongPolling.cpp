#include <vix/websocket/LongPolling.hpp>

namespace vix::websocket
{
    LongPollingManager::LongPollingManager(std::chrono::seconds sessionTtl,
                                           std::size_t maxBufferPerSession,
                                           WebSocketMetrics *metrics)
        : sessionTtl_(sessionTtl),
          maxBufferPerSession_(maxBufferPerSession),
          mutex_(),
          sessions_(),
          metrics_(metrics)
    {
    }

    LongPollingManager::LongPollingManager(LongPollingManager &&other) noexcept
        : sessionTtl_(other.sessionTtl_),
          maxBufferPerSession_(other.maxBufferPerSession_),
          mutex_(),
          sessions_(std::move(other.sessions_)),
          metrics_(other.metrics_)
    {
    }

    LongPollingManager &LongPollingManager::operator=(LongPollingManager &&other) noexcept
    {
        if (this == &other)
            return *this;

        std::scoped_lock lock(mutex_, other.mutex_);

        sessionTtl_ = other.sessionTtl_;
        maxBufferPerSession_ = other.maxBufferPerSession_;
        sessions_ = std::move(other.sessions_);
        metrics_ = other.metrics_;

        return *this;
    }

    LongPollingSession &LongPollingManager::get_or_create_unlocked(const SessionId &sessionId)
    {
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            auto [insertedIt, _] = sessions_.emplace(sessionId, LongPollingSession{sessionId});

            if (metrics_)
            {
                metrics_->lp_sessions_total.fetch_add(1, std::memory_order_relaxed);
                metrics_->lp_sessions_active.fetch_add(1, std::memory_order_relaxed);
                // Ajouter la taille initiale du buffer (en pratique 0)
                metrics_->lp_messages_buffered.fetch_add(
                    insertedIt->second.buffer.size(), std::memory_order_relaxed);
            }

            return insertedIt->second;
        }
        return it->second;
    }

    void LongPollingManager::push_to(const SessionId &sessionId, const JsonMessage &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        LongPollingSession &session = get_or_create_unlocked(sessionId);

        // Taille avant insertion (pour ajuster lp_messages_buffered proprement)
        const auto beforeSize = session.buffer.size();

        session.enqueue(message, maxBufferPerSession_);

        if (metrics_)
        {
            // messages enqueued + gauge buffered
            const auto afterSize = session.buffer.size();
            const auto delta = static_cast<long long>(afterSize) - static_cast<long long>(beforeSize);

            if (delta > 0)
            {
                metrics_->lp_messages_buffered.fetch_add(
                    static_cast<std::uint64_t>(delta), std::memory_order_relaxed);
            }
            else if (delta < 0)
            {
                metrics_->lp_messages_buffered.fetch_sub(
                    static_cast<std::uint64_t>(-delta), std::memory_order_relaxed);
            }

            metrics_->lp_messages_enqueued_total.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::vector<JsonMessage> LongPollingManager::poll(const SessionId &sessionId,
                                                      std::size_t maxMessages,
                                                      bool createIfMissing)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (metrics_)
        {
            metrics_->lp_polls_total.fetch_add(1, std::memory_order_relaxed);
        }

        auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            if (!createIfMissing)
                return {};

            LongPollingSession &session = get_or_create_unlocked(sessionId);
            auto out = session.drain(maxMessages);

            if (metrics_)
            {
                const auto drained = static_cast<std::uint64_t>(out.size());
                if (drained > 0)
                {
                    metrics_->lp_messages_drained_total.fetch_add(drained, std::memory_order_relaxed);
                    metrics_->lp_messages_buffered.fetch_sub(drained, std::memory_order_relaxed);
                }
            }

            return out;
        }

        LongPollingSession &session = it->second;

        const auto beforeSize = session.buffer.size();
        auto out = session.drain(maxMessages);
        const auto afterSize = session.buffer.size();

        if (metrics_)
        {
            const auto drained = static_cast<std::uint64_t>(out.size());
            if (drained > 0)
            {
                metrics_->lp_messages_drained_total.fetch_add(drained, std::memory_order_relaxed);
            }

            const auto delta = static_cast<long long>(afterSize) - static_cast<long long>(beforeSize);
            if (delta > 0)
            {
                metrics_->lp_messages_buffered.fetch_add(
                    static_cast<std::uint64_t>(delta), std::memory_order_relaxed);
            }
            else if (delta < 0)
            {
                metrics_->lp_messages_buffered.fetch_sub(
                    static_cast<std::uint64_t>(-delta), std::memory_order_relaxed);
            }
        }

        return out;
    }

    void LongPollingManager::sweep_expired()
    {
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = sessions_.begin(); it != sessions_.end();)
        {
            if (it->second.is_expired(sessionTtl_, now))
            {
                if (metrics_)
                {
                    // Retirer la session + mettre à jour les gauges
                    const auto bufSize = static_cast<std::uint64_t>(it->second.buffer.size());
                    if (bufSize > 0)
                    {
                        metrics_->lp_messages_buffered.fetch_sub(bufSize, std::memory_order_relaxed);
                    }
                    metrics_->lp_sessions_active.fetch_sub(1, std::memory_order_relaxed);
                }

                it = sessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::size_t LongPollingManager::session_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

    std::size_t LongPollingManager::buffer_size(const SessionId &sessionId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
            return 0;

        return it->second.buffer.size();
    }

} // namespace vix::websocket
