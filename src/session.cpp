#include <vix/websocket/session.hpp>

namespace vix::websocket
{
    static vix::utils::Logger &logger = vix::utils::Logger::getInstance();

    Session::Session(tcp::socket socket,
                     Config cfg,
                     std::shared_ptr<Router> router,
                     std::shared_ptr<vix::executor::IExecutor> executor)
        : ws_(std::move(socket)), // le stream possède le socket
          cfg_(std::move(cfg)),
          router_(std::move(router)),
          executor_(std::move(executor)),
          buffer_(),
          idleTimer_(ws_.get_executor()), // timer sur le même executor que le stream
          closing_(false)
    {
        // Basic server-side options
        ws_.read_message_max(cfg_.maxMessageSize);

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
                    // Beast gère déjà pong automatiquement.
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

        // Simple WebSocket accept: Beast lit la requête HTTP upgrade depuis le socket.
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
                logger.log(Logger::Level::INFO, "[WebSocket][Session] Closed by client");
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

        // On convertit en string, simple pour la démo
        std::string data = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        if (router_)
        {
            router_->handle_message(*this, data);
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

    void Session::send_text(std::string_view text)
    {
        if (closing_)
            return;

        auto self = shared_from_this();
        std::string payload{text};

        executor_->post(
            [self, payload = std::move(payload)]() mutable
            {
                self->do_write_text(std::move(payload));
            },
            vix::executor::TaskOptions{.priority = 1,
                                       .timeout = std::chrono::milliseconds{0}});
    }

    void Session::send_binary(const void *data, std::size_t size)
    {
        if (closing_)
            return;

        auto self = shared_from_this();
        std::vector<unsigned char> payload(
            static_cast<const unsigned char *>(data),
            static_cast<const unsigned char *>(data) + size);

        executor_->post(
            [self, payload = std::move(payload)]() mutable
            {
                self->do_write_binary(std::move(payload));
            },
            vix::executor::TaskOptions{.priority = 1,
                                       .timeout = std::chrono::milliseconds{0}});
    }

    void Session::do_write_text(std::string payload)
    {
        if (closing_ || !ws_.next_layer().is_open())
            return;

        auto self = shared_from_this();

        ws_.text(true);
        ws_.async_write(
            net::buffer(payload),
            [this, self, payload = std::move(payload)](const boost::system::error_code &ec, std::size_t bytes)
            {
                (void)payload;
                on_write_complete(ec, bytes);
            });
    }

    void Session::do_write_binary(std::vector<unsigned char> payload)
    {
        if (closing_ || !ws_.next_layer().is_open())
            return;

        auto self = shared_from_this();

        ws_.binary(true);
        ws_.async_write(
            net::buffer(payload.data(), payload.size()),
            [this, self, payload = std::move(payload)](const boost::system::error_code &ec, std::size_t bytes)
            {
                (void)payload;
                on_write_complete(ec, bytes);
            });
    }

    void Session::on_write_complete(const boost::system::error_code &ec, std::size_t bytes)
    {
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
            {
                logger.log(Logger::Level::WARN,
                           "[WebSocket][Session] Write error: {}", ec.message());
            }
            return;
        }

        logger.log(Logger::Level::DEBUG,
                   "[WebSocket][Session] Sent {} bytes", bytes);
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
