/**
 *
 *  @file openapi_docs.hpp
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
#ifndef VIX_WEBSOCKET_REGISTER_DOCS_HPP
#define VIX_WEBSOCKET_REGISTER_DOCS_HPP

#include <string>
#include <utility>

#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <vix/router/RouteDoc.hpp>
#include <vix/openapi/Registry.hpp>

namespace vix::websocket::openapi
{
  /**
   * Register WebSocket + Long-Polling docs into the global OpenAPI registry.
   *
   */
  inline void register_ws_docs(
      std::string ws_upgrade_path = "/ws",
      std::string lp_poll_path = "/ws/poll",
      std::string lp_send_path = "/ws/send",
      std::string metrics_path = "/metrics")
  {
    namespace http = boost::beast::http;

    auto add = [](http::verb method, std::string path, vix::router::RouteDoc doc)
    {
      vix::openapi::Registry::add(method, std::move(path), std::move(doc));
    };

    // 1) WebSocket upgrade endpoint (doc only)
    {
      vix::router::RouteDoc doc;
      doc.summary = "WebSocket endpoint";
      doc.description =
          "WebSocket upgrade endpoint. Connect using a WebSocket client. "
          "Swagger UI cannot fully exercise the 101 upgrade, but the route is documented for clients.";
      doc.tags = {"ws"};

      doc.responses["101"] = {{"description", "Switching Protocols (WebSocket upgrade)"}};
      doc.responses["426"] = {{"description", "Upgrade Required (when called as plain HTTP)"}};

      // Optional OpenAPI hints
      doc.x["x-ws-upgrade"] = true;
      doc.x["x-ws-url"] = "ws://<host>:<ws_port>/";

      add(http::verb::get, std::move(ws_upgrade_path), std::move(doc));
    }

    // 2) Long-poll endpoint (GET)
    {
      vix::router::RouteDoc doc;
      doc.summary = "WebSocket long-poll (pull)";
      doc.description =
          "HTTP long-polling bridge. Use when WebSocket is not available. "
          "Query params: session_id (string), max (int).";
      doc.tags = {"ws", "long-poll"};

      // If your RouteDoc supports parameters, keep it minimal via description.
      doc.responses["200"] = {{"description", "Array of queued messages"}};
      doc.responses["503"] = {{"description", "Long-poll bridge not attached"}};

      add(http::verb::get, std::move(lp_poll_path), std::move(doc));
    }

    // 3) Long-poll send endpoint (POST)
    {
      vix::router::RouteDoc doc;
      doc.summary = "WebSocket long-poll (push)";
      doc.description =
          "HTTP push entrypoint for the long-poll bridge. "
          "JSON body: type (string), payload (object), optional room, session_id, id, ts, kind.";
      doc.tags = {"ws", "long-poll"};

      doc.request_body = {
          {"required", true},
          {"content",
           {
               {"application/json",
                {
                    {"schema",
                     {
                         {"type", "object"},
                         {"required", {"type"}},
                         {"properties",
                          {
                              {"session_id", {{"type", "string"}}},
                              {"room", {{"type", "string"}}},
                              {"type", {{"type", "string"}}},
                              {"kind", {{"type", "string"}}},
                              {"id", {{"type", "string"}}},
                              {"ts", {{"type", "string"}}},
                              {"payload", {{"type", "object"}}},
                          }},
                     }},
                }},
           }},
      };

      // ICI: example pour pre-remplir Swagger UI
      doc.request_body["content"]["application/json"]["example"] = {
          {"type", "chat.message"},
          {"room", "general"},
          {"payload", {{"text", "Hello"}}},
      };

      doc.responses["202"] = {{"description", "Queued"}};
      doc.responses["400"] = {{"description", "Invalid JSON body or missing fields"}};
      doc.responses["503"] = {{"description", "Long-poll bridge not attached"}};

      add(http::verb::post, std::move(lp_send_path), std::move(doc));
    }

    // 4) Metrics endpoint (GET /metrics)
    {
      vix::router::RouteDoc doc;
      doc.summary = "WebSocket metrics";
      doc.description =
          "Prometheus text metrics for the WebSocket runtime. "
          "This endpoint may be served by the main HTTP app or by a dedicated exporter.";
      doc.tags = {"ws", "metrics"};

      doc.responses["200"] = {{"description", "Prometheus text format"}};
      doc.responses["501"] = {{"description", "Not configured in this app"}};

      add(http::verb::get, std::move(metrics_path), std::move(doc));
    }
  }

} // namespace vix::websocket::openapi

#endif // VIX_WEBSOCKET_REGISTER_DOCS_HPP
