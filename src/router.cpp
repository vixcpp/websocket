/**
 *
 *  @file router.cpp
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
#include <vix/websocket/router.hpp>
#include <vix/websocket/session.hpp>

namespace vix::websocket
{
  void Router::handle_open(Session &session) const
  {
    if (openHandler_)
      openHandler_(session);
  }

  void Router::handle_close(Session &session) const
  {
    if (closeHandler_)
      closeHandler_(session);
  }

  void Router::handle_error(Session &session, const boost::system::error_code &ec) const
  {
    if (errorHandler_)
      errorHandler_(session, ec);
  }

  void Router::handle_message(Session &session, std::string payload) const
  {
    if (messageHandler_)
      messageHandler_(session, std::move(payload));
    else
      session.send_text(payload);
  }
} // namespace vix::websocket
