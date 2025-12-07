#pragma once

//
// Vix.cpp — WebSocket module convenience header
//
// Usage:
//   #include <vix/websocket.hpp>
//
// This pulls in the main building blocks:
//
//   - vix::websocket::Server          → dedicated WebSocket server
//   - vix::websocket::Client          → async WebSocket client
//   - vix::websocket::Session         → per-connection context
//   - vix::websocket::Router          → path-based WebSocket routing
//   - vix::websocket::JsonMessage     → { type, payload } JSON protocol
//   - vix::websocket::MessageStore    → abstract storage interface
//   - vix::websocket::SqliteMessageStore → SQLite + WAL implementation
//

#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/client.hpp>
#include <vix/websocket/server.hpp>
#include <vix/websocket/session.hpp>
#include <vix/websocket/router.hpp>
#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>
