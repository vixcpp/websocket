#ifndef VIX_WEBSOCKET_ROUTER_HPP
#define VIX_WEBSOCKET_ROUTER_HPP

#include <functional>
#include <string>
#include <boost/system/error_code.hpp>

namespace vix::websocket
{
    class Session;

    class Router
    {
    public:
        using OpenHandler = std::function<void(Session &)>;
        using CloseHandler = std::function<void(Session &)>;
        using ErrorHandler = std::function<void(Session &, const boost::system::error_code &)>;
        using MessageHandler = std::function<void(Session &, std::string)>;

        Router() = default;

        void on_open(OpenHandler cb) { openHandler_ = std::move(cb); }
        void on_close(CloseHandler cb) { closeHandler_ = std::move(cb); }
        void on_error(ErrorHandler cb) { errorHandler_ = std::move(cb); }
        void on_message(MessageHandler cb) { messageHandler_ = std::move(cb); }

        void handle_open(Session &session) const;
        void handle_close(Session &session) const;
        void handle_error(Session &session, const boost::system::error_code &ec) const;
        void handle_message(Session &session, std::string payload) const;

    private:
        OpenHandler openHandler_{};
        CloseHandler closeHandler_{};
        ErrorHandler errorHandler_{};
        MessageHandler messageHandler_{};
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ROUTER_HPP
