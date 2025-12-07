#include <vix/websocket/session.hpp>

namespace vix::websocket
{
    static vix::utils::Logger &logger = vix::utils::Logger::getInstance();

    Session::Session(tcp::socket socket,
                     const Config &cfg,
                     std::shared_ptr<Router> router,
                     std::shared_ptr<vix::executor::IExecutor> executor)
        : ws_(std::move(socket)), cfg_(cfg), router_(std::move(router)), executor_(std::move(executor)), buffer_(), idleTimer_(ws_.get_executor()), closing_(false), writeQueue_(), writeInProgress_(false)
    {
        {
            boost::system::error_code ec;
            ws_.next_layer().set_option(tcp::no_delay(true), ec);
        }

        ws_.read_message_max(cfg_.maxMessageSize);

        buffer_.reserve(cfg_.maxMessageSize);

        if (cfg_.enablePerMessageDeflate)
        {
            ws_.set_option(ws::permessage_deflate(true));
        }

        if (cfg_.autoPingPong)
        {
            ws_.control_callback(
                [](ws::frame_type kind, boost::string_view payload)
                {
                    (void)kind;
                    (void)payload;
                });
        }
    }

    void Session::run()
    {
        logger.log(Logger::Level::DEBUG, "[WebSocket][Session] Starting handshake");
        do_accept();
    }

    void Session::do_accept()
    {
        auto self = shared_from_this();

        ws_.async_accept(
            [this, self](const boost::system::error_code &ec)
            {
                on_accept(ec);
            });
    }

    void Session::on_accept(const boost::system::error_code &ec)
    {
        if (ec)
        {
            logger.log(Logger::Level::ERROR,
                       "[WebSocket][Session] Accept failed: {}", ec.message());
            if (router_)
                router_->handle_error(*this, ec);
            return;
        }

        logger.log(Logger::Level::INFO, "[WebSocket][Session] Handshake OK");

        if (router_)
            router_->handle_open(*this);

        arm_idle_timer();
        do_read();
    }

    void Session::do_read()
    {
        auto self = shared_from_this();

        ws_.async_read(
            buffer_,
            [this, self](const boost::system::error_code &ec, std::size_t bytes)
            {
                on_read(ec, bytes);
            });
    }

    void Session::on_read(const boost::system::error_code &ec, std::size_t bytes)
    {
        cancel_idle_timer();

        if (ec)
        {
            if (ec == ws::error::closed)
            {
                logger.log(Logger::Level::INFO,
                           "[WebSocket][Session] Closed by client");
            }
            else if (ec != boost::asio::error::operation_aborted)
            {
                logger.log(Logger::Level::WARN,
                           "[WebSocket][Session] Read error: {}", ec.message());
            }

            if (router_)
                router_->handle_close(*this);
            return;
        }

        logger.log(Logger::Level::DEBUG,
                   "[WebSocket][Session] Received {} bytes", bytes);

        auto data = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        if (router_)
        {
            router_->handle_message(*this, std::move(data));
        }

        if (!closing_)
        {
            arm_idle_timer();
            do_read();
        }
    }

    void Session::arm_idle_timer()
    {
        if (cfg_.idleTimeout.count() <= 0)
            return;

        idleTimer_.expires_after(cfg_.idleTimeout);

        auto self = shared_from_this();

        idleTimer_.async_wait(
            [this, self](const boost::system::error_code &ec)
            {
                on_idle_timeout(ec);
            });
    }

    void Session::cancel_idle_timer()
    {
        boost::system::error_code ec;
        idleTimer_.cancel(ec);
    }

    void Session::on_idle_timeout(const boost::system::error_code &ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            logger.log(Logger::Level::WARN,
                       "[WebSocket][Session] Idle timer error: {}", ec.message());
            return;
        }

        logger.log(Logger::Level::WARN,
                   "[WebSocket][Session] Idle timeout reached, closing connection");

        close(ws::close_reason(ws::close_code::normal));
    }

    void Session::do_enqueue_message(bool isBinary, std::string payload)
    {
        if (closing_)
            return;

        writeQueue_.push_back(PendingMessage{
            .isBinary = isBinary,
            .data = std::move(payload),
        });

        if (!writeInProgress_)
        {
            do_write_next();
        }
    }

    void Session::do_write_next()
    {
        if (closing_)
        {
            writeQueue_.clear();
            writeInProgress_ = false;
            return;
        }

        if (writeQueue_.empty())
        {
            writeInProgress_ = false;
            return;
        }

        writeInProgress_ = true;

        auto self = shared_from_this();
        PendingMessage msg = std::move(writeQueue_.front());
        writeQueue_.pop_front();

        ws_.text(!msg.isBinary);
        ws_.async_write(
            net::buffer(msg.data),
            [this, self](const boost::system::error_code &ec, std::size_t bytes)
            {
                on_write_complete(ec, bytes);
            });
    }

    void Session::send_text(std::string_view text)
    {
        if (closing_)
            return;

        auto self = shared_from_this();
        std::string payload{text};

        net::post(
            ws_.get_executor(),
            [self, payload = std::move(payload)]() mutable
            {
                self->do_enqueue_message(/*isBinary=*/false, std::move(payload));
            });
    }

    void Session::send_binary(const void *data, std::size_t size)
    {
        if (closing_)
            return;

        auto self = shared_from_this();
        std::string payload{
            static_cast<const char *>(data),
            static_cast<const char *>(data) + size};

        net::post(
            ws_.get_executor(),
            [self, payload = std::move(payload)]() mutable
            {
                self->do_enqueue_message(/*isBinary=*/true, std::move(payload));
            });
    }

    void Session::on_write_complete(const boost::system::error_code &ec,
                                    std::size_t bytes)
    {
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
            {
                logger.log(Logger::Level::WARN,
                           "[WebSocket][Session] Write error: {}", ec.message());
            }
            closing_ = true;
            writeQueue_.clear();
            return;
        }

        logger.log(Logger::Level::DEBUG,
                   "[WebSocket][Session] Sent {} bytes", bytes);

        do_write_next();
    }

    void Session::close(ws::close_reason reason)
    {
        if (closing_)
            return;

        closing_ = true;
        cancel_idle_timer();

        auto self = shared_from_this();

        ws_.async_close(
            reason,
            [this, self](const boost::system::error_code &ec)
            {
                if (ec && ec != boost::asio::error::operation_aborted)
                {
                    logger.log(Logger::Level::WARN,
                               "[WebSocket][Session] Close error: {}", ec.message());
                }

                if (router_)
                    router_->handle_close(*this);
            });
    }

} // namespace vix::websocket
