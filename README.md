# Vix.cpp WebSocket Module

Native realtime communication for Vix.cpp.

The WebSocket module provides the runtime layer used to build realtime C++ applications with WebSocket servers, clients, sessions, typed messages, rooms, broadcasting, long-polling fallback, metrics, and message persistence.

## Documentation

Full documentation is available here:

https://docs.vixcpp.com/modules/websocket/

API reference:

https://docs.vixcpp.com/modules/websocket/api-reference

## What WebSocket provides

- Native WebSocket server
- Native WebSocket client
- Per-connection sessions
- Event routing
- Typed JSON messages
- Rooms and broadcasting
- Long-polling fallback
- HTTP bridge APIs
- Prometheus-style metrics
- Message persistence interfaces
- SQLite-backed message storage
- OpenAPI documentation helpers
- Integration with `vix::App`
- Shared runtime execution through `RuntimeExecutor`

## Public header

```cpp
#include <vix/websocket.hpp>
```

For HTTP and WebSocket together:

```cpp
#include <vix.hpp>
#include <vix/websocket.hpp>
```

## Minimal WebSocket server

```cpp
#include <memory>
#include <string>

#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/websocket.hpp>

int main()
{
  vix::config::Config config{".env"};

  auto executor =
      std::make_shared<vix::executor::RuntimeExecutor>(4);

  vix::websocket::Server ws{config, executor};

  ws.on_message(
    [](vix::websocket::Session &session, const std::string &message)
    {
      session.send_text("echo: " + message);
    });

  ws.listen_blocking();

  return 0;
}
```

## Typed messages

Vix WebSocket supports a typed JSON message model:

```json
{
  "type": "chat.message",
  "payload": {
    "user": "Ada",
    "text": "Hello"
  }
}
```

Example handler:

```cpp
ws.on_typed_message(
  [&ws](vix::websocket::Session &session,
        const std::string &type,
        const vix::json::kvs &payload)
  {
    (void)session;

    if (type == "chat.message")
    {
      ws.broadcast_json("chat.message", payload);
    }
  });
```

## Rooms

```cpp
ws.join_room(session.shared_from_this(), "general");

ws.broadcast_text_to_room(
  "general",
  "Hello room");
```

## HTTP and WebSocket together

```cpp
#include <memory>

#include <vix.hpp>
#include <vix/websocket.hpp>

int main()
{
  vix::App app;

  auto executor =
      std::make_shared<vix::executor::RuntimeExecutor>(4);

  vix::websocket::Server ws{app.config(), executor};

  app.get("/", [](vix::Request &req, vix::Response &res)
  {
    (void)req;

    res.text("HTTP + WebSocket");
  });

  ws.on_message(
    [](vix::websocket::Session &session, const std::string &message)
    {
      session.send_text("echo: " + message);
    });

  vix::websocket::AttachedRuntime runtime{app, ws, executor};

  app.run(8080);

  return 0;
}
```

## WebSocket architecture

```text
Server
  -> LowLevelServer
  -> Session
  -> Router
  -> user callbacks
```

For HTTP and WebSocket together:

```text
vix::App
  -> HTTP server

vix::websocket::Server
  -> WebSocket server

AttachedRuntime
  -> shared lifecycle
  -> shared RuntimeExecutor
  -> coordinated shutdown
```

## Build

Contributors should use the Vix CLI to build this module.

Vix wraps the C++ build workflow with project detection, presets, Ninja builds, clean logs, caching, and focused diagnostics. This keeps the contributor workflow consistent and helps avoid hidden C++ build issues.

### Build the project

```bash
git clone https://github.com/vixcpp/vix.git
cd vix
vix build
```

### Build all targets

Use this before running the full test suite, install workflows, or release checks:

```bash
vix build --build-target all
```

### Clean rebuild

Use this when the local CMake cache or build directory may be stale:

```bash
vix build --clean
```

### Release build

```bash
vix build --preset release
```

## Tests

Build all targets first, then run tests:

```bash
vix build --build-target all
vix tests
```

Before opening a pull request, use:

```bash
vix fmt --check
vix build --build-target all
vix tests
```

## Useful links

- WebSocket documentation: https://docs.vixcpp.com/modules/websocket/
- WebSocket API reference: https://docs.vixcpp.com/modules/websocket/api-reference
- Build command: https://docs.vixcpp.com/cli/build
- Tests command: https://docs.vixcpp.com/cli/tests
- Documentation: https://docs.vixcpp.com/
- Engineering notes: https://blog.vixcpp.com/
- Registry: https://registry.vixcpp.com/
- GitHub: https://github.com/vixcpp/vix

## License

MIT License.

See [`LICENSE`](../../LICENSE) for details.
