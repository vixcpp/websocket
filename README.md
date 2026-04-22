# WebSocket - Vix.cpp

Build real-time systems in C++.
No glue code. No fragmented stack. Just one runtime.

## What is this?

The Vix WebSocket module lets you build production-ready realtime systems with:

- native WebSocket server
- typed message protocol (`type + payload`)
- room-based messaging
- HTTP + WebSocket unified runtime
- long-polling fallback
- persistent message storage (SQLite)
- built-in metrics (Prometheus)

All powered by the same runtime.

## Why this exists

Building realtime systems in C++ is usually painful:

- multiple libraries (HTTP, WS, JSON, threads)
- inconsistent models
- no unified runtime
- complex setup

Vix removes that friction.

You get a single, coherent system where:

- HTTP and WebSocket share the same runtime
- messages follow a consistent protocol
- async is handled for you
- production features are built-in

## Install

```bash
curl -fsSL https://vixcpp.com/install.sh | bash
```

## Minimal example

```cpp
#include <vix.hpp>
#include <vix/websocket.hpp>

int main()
{
  auto exec = std::make_shared<vix::executor::RuntimeExecutor>();

  vix::config::Config cfg{".env"};
  vix::websocket::Server ws(cfg, exec);

  ws.on_open([](auto& session) {
    session.send_text(
      vix::websocket::JsonMessage::serialize(
        "system.welcome",
        {"message", "Hello from Vix"}
      )
    );
  });

  ws.on_typed_message([&](auto&, const std::string& type, const vix::json::kvs& payload) {
    if (type == "chat.message") {
      ws.broadcast_json("chat.message", payload);
    }
  });

  ws.listen_blocking();
}
```

## Typed protocol

Vix uses a simple and consistent format:

```json
{
  "type": "chat.message",
  "payload": {
    "user": "Ada",
    "text": "Hello"
  }
}
```

You never deal with raw frames.
You deal with events.

## Rooms

```cpp
ws.join_room(session, "general");

ws.broadcast_room_json(
  "general",
  "chat.message",
  {"user", "Ada", "text", "Hello"}
);
```

## HTTP + WebSocket together

```cpp
vix::run_http_and_ws(app, ws, executor, 8080);
```

One runtime. One system.

## Long polling fallback

When WebSocket is not available:

- `/ws/poll` -> receive messages
- `/ws/send` -> send messages

Same protocol. Same events.

## Persistence (SQLite)

```cpp
vix::websocket::SqliteMessageStore store{"chat.db"};

store.append(msg);
store.list_by_room("general", 50);
```

Works with WAL. Designed for reliability.

## Metrics

```cpp
vix::websocket::WebSocketMetrics metrics;

metrics.connections_total
metrics.messages_in_total
metrics.messages_out_total
```

Expose with:

```text
GET /metrics
```

Prometheus-ready.

## What you can build

- chat systems
- realtime dashboards
- monitoring systems
- collaborative tools
- live APIs
- messaging platforms

## Examples

See:

```text
examples/websocket/
```

- minimal server
- HTTP + WS
- chat with rooms
- persistent chat
- long-polling fallback
- metrics runtime
- native client
- realtime dashboard

## Design philosophy

Vix is not a wrapper.
It is a runtime.

- one model
- one protocol
- one execution system
- no hidden layers

Everything is explicit, predictable, and production-ready.

## Learn more

Learn more about the Vix runtime in the documentation.

## License

MIT License.

