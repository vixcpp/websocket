#include <vix/websocket/config.hpp>

#include <algorithm>

namespace vix::websocket
{
    Config Config::from_core(const vix::config::Config &core)
    {
        Config cfg;

        if (core.has("websocket.max_message_size"))
        {
            auto v = core.getInt("websocket.max_message_size", static_cast<int>(cfg.maxMessageSize));
            cfg.maxMessageSize = static_cast<std::size_t>(std::max(1024, v)); // min 1 KiB
        }

        if (core.has("websocket.idle_timeout"))
        {
            auto v = core.getInt("websocket.idle_timeout", static_cast<int>(cfg.idleTimeout.count()));
            cfg.idleTimeout = std::chrono::seconds(std::max(5, v)); // min 5s
        }

        if (core.has("websocket.enable_deflate"))
        {
            cfg.enablePerMessageDeflate = core.getBool("websocket.enable_deflate", cfg.enablePerMessageDeflate);
        }

        if (core.has("websocket.ping_interval"))
        {
            auto v = core.getInt("websocket.ping_interval", static_cast<int>(cfg.pingInterval.count()));
            if (v <= 0)
                cfg.pingInterval = std::chrono::seconds{0};
            else
                cfg.pingInterval = std::chrono::seconds(v);
        }

        if (core.has("websocket.auto_ping_pong"))
        {
            cfg.autoPingPong = core.getBool("websocket.auto_ping_pong", cfg.autoPingPong);
        }

        return cfg;
    }

} // namespace vix::websocket
