/**
 *
 *  @file websocket/websocket.hpp
 *  @author Gaspard Kirira
 *
 *  @brief Internal aggregation header for the Vix websocket module.
 *
 *  This file includes the full WebSocket stack provided by Vix,
 *  including server, client, routing, session handling, storage,
 *  long polling fallback, metrics, and runtime integration.
 *
 *  For most use cases, prefer:
 *    #include <vix/websocket.hpp>
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WEBSOCKET_WEBSOCKET_HPP
#define VIX_WEBSOCKET_WEBSOCKET_HPP

// Core config & protocol
#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>

// Core runtime
#include <vix/websocket/AttachedRuntime.hpp>

// Server / client
#include <vix/websocket/server.hpp>
#include <vix/websocket/client.hpp>
#include <vix/websocket/App.hpp>

// Routing & session
#include <vix/websocket/router.hpp>
#include <vix/websocket/session.hpp>

// HTTP bridge / API
#include <vix/websocket/HttpApi.hpp>
#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/LongPollingBridge.hpp>

// Storage
#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>

// Observability
#include <vix/websocket/Metrics.hpp>

// Docs / tooling
#include <vix/websocket/openapi_docs.hpp>

#endif // VIX_WEBSOCKET_WEBSOCKET_HPP
