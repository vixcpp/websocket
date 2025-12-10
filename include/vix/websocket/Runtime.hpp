#pragma once

//
// Vix.cpp — WebSocket module umbrella header
//
// Usage:
//   #include <vix/websocket.hpp>
//
// This pulls in all main building blocks of the WebSocket module:
//
//   Core types
//   ---------
//   - vix::websocket::Server              → dedicated WebSocket server
//   - vix::websocket::Session             → per-connection context
//   - vix::websocket::Client              → asynchronous WebSocket client
//   - vix::websocket::Router              → path-based WebSocket routing
//   - vix::websocket::Config              → WebSocket configuration helpers
//   - vix::websocket::Protocol / Json API → typed { type, payload } protocol
//
//   Persistence & Metrics
//   ---------------------
//   - vix::websocket::MessageStore        → abstract message storage interface
//   - vix::websocket::SqliteMessageStore  → SQLite-based message store (WAL-friendly)
//   - vix::websocket::Metrics             → simple WebSocket metrics helpers
//
//   HTTP + WebSocket integration
//   ----------------------------
//   - vix::websocket::App                 → high-level HTTP + WS app wrapper
//   - vix::websocket::HttpApi             → helpers to expose WS state via HTTP
//   - vix::websocket::AttachedRuntime     → runtime that attaches WS to vix::App
//   - vix::websocket::Runtime             → public alias for AttachedRuntime
//
//   Fallback / Long-polling bridges
//   -------------------------------
//   - vix::websocket::LongPolling         → HTTP long-polling transport
//   - vix::websocket::LongPollingBridge   → bridge between WS and long-polling
//
// This header is designed as a convenience entry point: it is safe to include
// in user code that builds real-time systems (chat, dashboards, notifications,
// multiplayer, collab tools, etc.).
//

// Core configuration & protocol
#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>

// Client / server / session / routing
#include <vix/websocket/client.hpp>
#include <vix/websocket/server.hpp>
#include <vix/websocket/session.hpp>
#include <vix/websocket/router.hpp>

// Message storage & persistence
#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>

// Metrics & high-level app wrapper
#include <vix/websocket/Metrics.hpp>
#include <vix/websocket/App.hpp>
#include <vix/websocket/HttpApi.hpp>

// Long-polling and fallback transports
#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/LongPollingBridge.hpp>

// Runtime helpers (attached to vix::App lifecycle)
#include <vix/websocket/AttachedRuntime.hpp>
#include <vix/websocket/Runtime.hpp>
