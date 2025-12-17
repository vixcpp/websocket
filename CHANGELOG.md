# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
## [1.1.2] - 2025-12-17

### Added
- 

### Changed
- 

### Removed
- 

## [1.1.1] - 2025-12-14

### Added
- 

### Changed
- 

### Removed
- 

## [1.1.0] - 2025-12-11

### Added
- 

### Changed
- 

### Removed
- 


websocket: introduce runtime layer and refresh examples

• Added new websocket runtime system: - include/vix/websocket/Runtime.hpp - include/vix/websocket/AttachedRuntime.hpp
providing a dedicated io_context, lifecycle management, and attach-to-HTTP mode.
• Added new example demo files: - examples/chat_room.cpp - examples/simple_client.cpp - examples/simple_server.cpp
• Updated existing example sources: - advanced/server.cpp to use new runtime API & metrics - simple/minimal_ws_server.cpp cleanup and API alignment
• Revised examples/CMakeLists.txt to: - build all demos consistently - expose runtime headers correctly
This prepares the module for integration with Vix::App and upcoming WS routing features.

## [1.0.0] - 2025-12-08

### Added

-

### Changed

-

### Removed

-

## [0.3.0] - 2025-12-08

### Added

-

### Changed

-

### Removed

-

## [0.2.0] - 2025-12-07

### Added

-

### Changed

-

### Removed

-

## [0.1.3] - 2025-12-07

### Added

-

### Changed

-

### Removed

-

## [0.1.2] - 2025-12-07

feat(websocket): add rooms + persistent message store (SQLite + WAL)

- Added high-level WebSocket rooms API:
  • join_room(session, room)
  • leave_room(session, room)
  • broadcast_room_json(room, type, payload)

- Updated websocket::Server to manage room membership and routing.

- Added extensible persistence layer:
  • MessageStore (interface) - append(message) - list_by_room(room, limit, before_id) - replay_from(id)

  • SqliteMessageStore (SQLite3 + WAL enabled) - Creates table ws_messages(id, kind, room, type, ts, payload_json) - Safe concurrent writes, WAL mode for high performance - Full JSON persistence for payload

- Added new files:
  • include/vix/websocket/MessageStore.hpp
  • include/vix/websocket/SqliteMessageStore.hpp
  • src/SqliteMessageStore.cpp
  • examples/config/config.json (WebSocket demo)

- Updated existing components:
  • client.hpp → added JSON message support, room commands, auto-reconnect improvements
  • protocol.hpp → extended JsonMessage with kind, room, timestamp, id
  • server.hpp → room map, typed messages, storage hooks
  • CMakeLists.txt → install SqliteMessageStore and enable SQLite linkage
  • examples/CMakeLists.txt → added chat example with rooms + persistence

This commit completes:
✓ WebSocket rooms
✓ Strict JSON protocol with envelopes
✓ Persistent chat history via SQLite + WAL
✓ Ready for replay API and message batching

### Added

-

### Changed

-

### Removed

-

## [0.1.1] - 2025-12-05

### Added

-

### Changed

-

### Removed

-

## [0.1.0] - 2025-12-05

### Added

-

### Changed

-

### Removed

-

## [v0.1.0] - 2025-12-05

### Added

- Initial project scaffolding for the `vixcpp/websocket` module.
- CMake build system:
  - STATIC vs header-only build depending on `src/` contents.
  - Integration with `vix::core` and optional JSON backend.
  - Support for sanitizers via `VIX_ENABLE_SANITIZERS`.
- Basic repository structure:
  - `include/vix/websocket/` for public headers.
  - `src/` for implementation files.
- Release workflow:
  - `Makefile` with `release`, `commit`, `push`, `merge`, and `tag` targets.
  - `changelog` target wired to `scripts/update_changelog.sh`.
