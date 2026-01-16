/**
 *
 *  @file Runtime.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#ifndef VIX_RUNTIME_HPP
#define VIX_RUNTIME_HPP

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

#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/client.hpp>
#include <vix/websocket/server.hpp>
#include <vix/websocket/session.hpp>
#include <vix/websocket/router.hpp>
#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>
#include <vix/websocket/Metrics.hpp>
#include <vix/websocket/App.hpp>
#include <vix/websocket/HttpApi.hpp>
#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/AttachedRuntime.hpp>
#include <vix/websocket/Runtime.hpp>

#endif
