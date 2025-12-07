#include <vix/config/Config.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>
#include <vix/websocket/server.hpp>

int main()
{
    vix::config::Config cfg{"config/config.json"};

    auto exec = vix::experimental::make_threadpool_executor(
        4, // min threads
        8, // max threads
        0  // default prio
    );

    vix::websocket::Server ws(cfg, std::move(exec));

    ws.on_open(
        [&ws](auto &session)
        {
            ws.broadcast_json(
                "chat.system",
                {
                    "user",
                    "server",
                    "text",
                    "Welcome to Softadastra Chat 👋",
                });
        });

    ws.on_typed_message(
        [&ws](auto &session,
              const std::string &type,
              const vix::json::kvs &payload)
        {
            if (type == "chat.message")
            {
                ws.broadcast_json("chat.message", payload);
            }
        });

    ws.listen_blocking();
}
