#include <vix/websocket/Metrics.hpp>

#include <sstream>

#include <vix/utils/Logger.hpp>

#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace vix::websocket
{
    using tcp = boost::asio::ip::tcp;
    namespace bb = boost::beast;
    namespace http = bb::http;
    namespace net = boost::asio;

    namespace
    {
        [[nodiscard]] bool is_metrics_request(const http::request<http::string_body> &req) noexcept
        {
            return (req.method() == http::verb::get && req.target() == "/metrics");
        }

        void set_common_headers(http::response<http::string_body> &res, unsigned version)
        {
            res.version(version);
            res.set(http::field::server, "vix-ws-metrics");
            res.set(http::field::cache_control, "no-store");
            res.set(http::field::connection, "close");
        }
    } // namespace

    std::string WebSocketMetrics::render_prometheus() const
    {
        std::ostringstream os;

        os << "# HELP vix_ws_connections_total Total WebSocket connections created\n"
           << "# TYPE vix_ws_connections_total counter\n"
           << "vix_ws_connections_total " << connections_total.load() << "\n\n"

           << "# HELP vix_ws_connections_active Current active WebSocket connections\n"
           << "# TYPE vix_ws_connections_active gauge\n"
           << "vix_ws_connections_active " << connections_active.load() << "\n\n"

           << "# HELP vix_ws_messages_in_total Total number of WebSocket messages received\n"
           << "# TYPE vix_ws_messages_in_total counter\n"
           << "vix_ws_messages_in_total " << messages_in_total.load() << "\n\n"

           << "# HELP vix_ws_messages_out_total Total number of WebSocket messages sent\n"
           << "# TYPE vix_ws_messages_out_total counter\n"
           << "vix_ws_messages_out_total " << messages_out_total.load() << "\n\n"

           << "# HELP vix_ws_errors_total Total number of WebSocket errors\n"
           << "# TYPE vix_ws_errors_total counter\n"
           << "vix_ws_errors_total " << errors_total.load() << "\n\n";

        os << "# HELP vix_ws_lp_sessions_total Total long-polling sessions ever created\n"
           << "# TYPE vix_ws_lp_sessions_total counter\n"
           << "vix_ws_lp_sessions_total " << lp_sessions_total.load() << "\n\n"

           << "# HELP vix_ws_lp_sessions_active Current active long-polling sessions\n"
           << "# TYPE vix_ws_lp_sessions_active gauge\n"
           << "vix_ws_lp_sessions_active " << lp_sessions_active.load() << "\n\n"

           << "# HELP vix_ws_lp_polls_total Total /ws/poll HTTP calls\n"
           << "# TYPE vix_ws_lp_polls_total counter\n"
           << "vix_ws_lp_polls_total " << lp_polls_total.load() << "\n\n"

           << "# HELP vix_ws_lp_messages_buffered Current buffered messages for long-polling\n"
           << "# TYPE vix_ws_lp_messages_buffered gauge\n"
           << "vix_ws_lp_messages_buffered " << lp_messages_buffered.load() << "\n\n"

           << "# HELP vix_ws_lp_messages_enqueued_total Total messages enqueued into long-poll buffers\n"
           << "# TYPE vix_ws_lp_messages_enqueued_total counter\n"
           << "vix_ws_lp_messages_enqueued_total " << lp_messages_enqueued_total.load() << "\n\n"

           << "# HELP vix_ws_lp_messages_drained_total Total messages drained via /ws/poll\n"
           << "# TYPE vix_ws_lp_messages_drained_total counter\n"
           << "vix_ws_lp_messages_drained_total " << lp_messages_drained_total.load() << "\n";

        return os.str();
    }

    void run_metrics_http_exporter(WebSocketMetrics &metrics,
                                   const std::string &address,
                                   std::uint16_t port)
    {
        auto &log = vix::utils::Logger::getInstance();

        try
        {
            net::io_context ioc{1};

            tcp::endpoint ep{net::ip::make_address(address), port};
            tcp::acceptor acceptor{ioc};
            boost::system::error_code ec;

            acceptor.open(ep.protocol(), ec);
            if (ec)
                throw std::system_error(ec, "open");

            acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if (ec)
                throw std::system_error(ec, "reuse_address");

            acceptor.bind(ep, ec);
            if (ec)
            {
                if (ec == boost::system::errc::address_in_use)
                {
                    throw std::system_error(
                        ec,
                        "bind: address already in use. Another process is listening on this port.");
                }
                throw std::system_error(ec, "bind");
            }

            acceptor.listen(net::socket_base::max_listen_connections, ec);
            if (ec)
                throw std::system_error(ec, "listen");

            log.log(vix::utils::Logger::Level::INFO,
                    "[ws] metrics listening {}:{}  (GET /metrics)",
                    address, port);

            for (;;)
            {
                tcp::socket socket{ioc};
                acceptor.accept(socket, ec);
                if (ec)
                {
                    log.log(vix::utils::Logger::Level::DEBUG,
                            "[ws] metrics accept error ({})",
                            ec.message());
                    continue;
                }

                bb::flat_buffer buffer;
                http::request<http::string_body> req;
                http::read(socket, buffer, req, ec);

                if (ec)
                {
                    log.log(vix::utils::Logger::Level::DEBUG,
                            "[ws] metrics read error ({})",
                            ec.message());
                    boost::system::error_code ignore;
                    socket.shutdown(tcp::socket::shutdown_both, ignore);
                    socket.close(ignore);
                    continue;
                }

                http::response<http::string_body> res;

                if (is_metrics_request(req))
                {
                    res.result(http::status::ok);
                    set_common_headers(res, req.version());
                    res.set(http::field::content_type, "text/plain; version=0.0.4; charset=utf-8");
                    res.body() = metrics.render_prometheus();
                    res.prepare_payload();
                }
                else
                {
                    res.result(http::status::not_found);
                    set_common_headers(res, req.version());
                    res.set(http::field::content_type, "text/plain; charset=utf-8");
                    res.body() = "Not Found\n";
                    res.prepare_payload();
                }

                http::write(socket, res, ec);
                if (ec)
                {
                    log.log(vix::utils::Logger::Level::DEBUG,
                            "[ws] metrics write error ({})",
                            ec.message());
                }

                boost::system::error_code ignore;
                socket.shutdown(tcp::socket::shutdown_send, ignore);
                socket.close(ignore);
            }
        }
        catch (const std::exception &e)
        {
            log.log(vix::utils::Logger::Level::ERROR,
                    "[ws] metrics server error ({})",
                    e.what());
        }
    }

} // namespace vix::websocket
