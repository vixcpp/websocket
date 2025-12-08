#include <vix/websocket/Metrics.hpp>

#include <sstream>
#include <iostream>

#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace vix::websocket
{
    using tcp = boost::asio::ip::tcp;
    namespace bb = boost::beast;
    namespace http = bb::http;
    namespace net = boost::asio;

    // -------------------------------------------------------------------------
    // WebSocketMetrics::render_prometheus
    // -------------------------------------------------------------------------

    std::string WebSocketMetrics::render_prometheus() const
    {
        std::ostringstream os;

        os << "# HELP vix_ws_connections_total Total WebSocket connections created\n";
        os << "# TYPE vix_ws_connections_total counter\n";
        os << "vix_ws_connections_total " << connections_total.load() << "\n\n";

        os << "# HELP vix_ws_connections_active Current active WebSocket connections\n";
        os << "# TYPE vix_ws_connections_active gauge\n";
        os << "vix_ws_connections_active " << connections_active.load() << "\n\n";

        os << "# HELP vix_ws_messages_in_total Total number of messages received\n";
        os << "# TYPE vix_ws_messages_in_total counter\n";
        os << "vix_ws_messages_in_total " << messages_in_total.load() << "\n\n";

        os << "# HELP vix_ws_messages_out_total Total number of messages sent\n";
        os << "# TYPE vix_ws_messages_out_total counter\n";
        os << "vix_ws_messages_out_total " << messages_out_total.load() << "\n\n";

        os << "# HELP vix_ws_errors_total Total number of WebSocket errors\n";
        os << "# TYPE vix_ws_errors_total counter\n";
        os << "vix_ws_errors_total " << errors_total.load() << "\n";

        return os.str();
    }

    // -------------------------------------------------------------------------
    // run_metrics_http_exporter
    // -------------------------------------------------------------------------

    void run_metrics_http_exporter(WebSocketMetrics &metrics,
                                   const std::string &address,
                                   std::uint16_t port)
    {
        try
        {
            net::io_context ioc{1};
            tcp::acceptor acceptor{ioc, {net::ip::make_address(address), port}};

            for (;;)
            {
                tcp::socket socket{ioc};
                acceptor.accept(socket);

                bb::flat_buffer buffer;
                http::request<http::string_body> req;
                http::read(socket, buffer, req);

                http::response<http::string_body> res;

                if (req.method() == http::verb::get &&
                    req.target() == "/metrics")
                {
                    std::string body = metrics.render_prometheus();

                    res.result(http::status::ok);
                    res.version(req.version());
                    res.set(http::field::content_type, "text/plain; version=0.0.4");
                    res.body() = std::move(body);
                    res.prepare_payload();
                }
                else
                {
                    res = http::response<http::string_body>(
                        http::status::not_found, req.version());
                    res.set(http::field::content_type, "text/plain");
                    res.body() = "Not Found\n";
                    res.prepare_payload();
                }

                http::write(socket, res);
                socket.shutdown(tcp::socket::shutdown_send);
            }
        }
        catch (const std::exception &e)
        {
            // Logging is intentionally minimal here to avoid depending on
            // vix::utils::Logger from this helper.
            std::cerr << "[websocket][metrics] server error: " << e.what() << "\n";
        }
    }

} // namespace vix::websocket
