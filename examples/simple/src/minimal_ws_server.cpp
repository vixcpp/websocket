/**
 *
 *  @file minimal_ws_server.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <iostream>
#include <string>

#include <vix/config/Config.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>
#include <vix/websocket.hpp>
#include <vix/websocket/protocol.hpp>

namespace ws = vix::websocket;

int main()
{
  auto exec = vix::experimental::make_threadpool_executor(
      4, // min threads
      8, // max threads
      0  // default priority
  );

  ws::App app{"config/config.json", std::move(exec)};
  auto &server = app.server();

  std::cout << "[minimal] WebSocket server starting on port "
            << server.port() << std::endl;

  server.on_open(
      [](ws::Session &session)
      {
        vix::json::kvs payload{
            "message",
            std::string{"Welcome to minimal Vix WebSocket 👋"},
        };

        std::string text = ws::JsonMessage::serialize("system.welcome", payload);
        session.send_text(text);

        std::cout << "[minimal] New session opened, welcome sent" << std::endl;
      });

  [[maybe_unused]] auto &chatRoute = app.ws(
      "/chat",
      [&server](ws::Session &session,
                const std::string &type,
                const vix::json::kvs &payload)
      {
        (void)session;

        if (type == "chat.message")
        {
          server.broadcast_json("chat.message", payload);
        }
      });

  app.run_blocking();

  return 0;
}
