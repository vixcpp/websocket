

#ifndef VIX_WEBSOCKET_SESSION_HPP
#define VIX_WEBSOCKET_SESSION_HPP

/**
 * @file session.hpp
 * @brief Per-connection WebSocket session for Vix.cpp.
 *
 * Responsibilities:
 *  - Perform WebSocket handshake.
 *  - Configure WS options (timeout, max message size, deflate...).
 *  - Read messages asynchronously and dispatch to Router.
 *  - Send text/binary frames back to the client.
 *  - Enforce idle timeout and ping/pong (if configured).
 */

#include <memory>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>
#include <deque>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/utils/Logger.hpp>

namespace vix::websocket
{
    namespace beast = boost::beast;
    namespace ws = boost::beast::websocket;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        Session(tcp::socket socket,
                const Config &cfg,
                std::shared_ptr<Router> router,
                std::shared_ptr<vix::executor::IExecutor> executor);

        ~Session() = default;

        /// Start handshake then message loop.
        void run();

        /// Send a text frame (thread-safe via executor).
        void send_text(std::string_view text);

        /// Send a binary frame.
        void send_binary(const void *data, std::size_t size);

        /// Close the connection with a reason (optional).
        void close(ws::close_reason reason = ws::close_reason{});

    private:
        void do_accept();
        void on_accept(const boost::system::error_code &ec);

        void do_read();
        void on_read(const boost::system::error_code &ec, std::size_t bytes);

        void arm_idle_timer();
        void cancel_idle_timer();
        void on_idle_timeout(const boost::system::error_code &ec);
        void on_write_complete(const boost::system::error_code &ec, std::size_t bytes);

    private:
        // Le stream POSSÈDE le socket (NextLayer = tcp::socket)
        ws::stream<tcp::socket> ws_;

        Config cfg_;
        std::shared_ptr<Router> router_;
        std::shared_ptr<vix::executor::IExecutor> executor_;

        beast::flat_buffer buffer_;
        net::steady_timer idleTimer_;
        bool closing_ = false;

        // 🔥 Nouvelle file d’attente write + état
        struct PendingMessage
        {
            bool isBinary;
            std::string data;
        };

        std::deque<PendingMessage> writeQueue_;
        bool writeInProgress_ = false;

        using Logger = vix::utils::Logger;

        // Nouveau helper interne
        void do_enqueue_message(bool isBinary, std::string payload);
        void do_write_next();
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SESSION_HPP
