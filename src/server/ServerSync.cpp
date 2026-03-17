#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <system_error>

namespace Network {

ServerSync::ServerSync(uint16_t port, asio::io_context& io_ctx)
    : ServerBase(port, io_ctx) {}

#include <asio/ip/tcp.hpp>
#include <asio/socket_base.hpp>
#include <expected>
#include <future>
#include <iostream>
#include <memory>
#include <system_error>

// Assuming TcpSocket is defined elsewhere
// class TcpSocket { ... };

std::expected<void, std::error_code> ServerSync::listen() {
  // 1. Setup Phase: Fail fast
  std::error_code ec;
  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
    return std::unexpected(ec);

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
    return std::unexpected(ec);

  _acceptor.bind(endpoint, ec);
  if (ec)
    return std::unexpected(ec);

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
    return std::unexpected(ec);

  // 2. Synchronization Phase: Prepare for async wait
  // We use a shared promise so the object remains valid across recursive lambda
  // calls
  auto promise =
      std::make_shared<std::promise<std::expected<void, std::error_code>>>();

  auto future = promise->get_future();
  // Start the async loop
  start_accept(std::move(promise));

  // 3. Blocking Phase: Wait for the loop to signal completion
  // This blocks the calling thread (which is safe as listen is already wrapped
  // in a thread)
  return future.get();
}

void ServerSync::start_accept(
    std::shared_ptr<std::promise<std::expected<void, std::error_code>>>
        promise) {
  if (!promise)
    return;  // Safety check

  // Check if we should stop immediately (graceful stop requested before accept)
  // Note: If is_stopped() is true, we stop accepting immediately.
  // To ensure pending async_accepts complete, ensure stop() also closes the
  // acceptor.
  if (is_stopped()) {
    promise->set_value(
        std::expected<void, std::error_code>{});  // Success on graceful stop
    return;
  }

  auto socket = std::make_unique<asio::ip::tcp::socket>(get_io_context());

  _acceptor.async_accept(*socket, [&, this, socket = std::move(socket),
                                   promise](std::error_code ec) mutable {
    // A. Check for Explicit Stop
    if (is_stopped()) {
      // Acceptor was closed or stop requested after socket created
      promise->set_value(std::expected<void, std::error_code>{});
      return;
    }

    // B. Check for Acceptor Closed Unexpectedly
    // operation_aborted implies the acceptor was closed.
    // Since is_stopped() is false, this was likely an error/crash, not graceful
    // shutdown.
    if (ec == asio::error::operation_aborted ||
        ec == asio::error::bad_descriptor) {
      promise->set_value(std::unexpected(ec));
      return;
    }

    // C. Handle Standard Error
    if (ec) {
      std::cout << "accept error: " << ec.message() << ", retrying..."
                << std::endl;
      // Recurse immediately on error (original behavior)
      start_accept(std::move(promise));
      return;
    }

    // D. Success: Accept Connection
    {
      auto new_socket = std::make_unique<TcpSocket>(std::move(*socket));
      handle_client(std::move(new_socket));
    }

    // E. Recurse for next connection
    start_accept(std::move(promise));
  });
}

void ServerSync::stop() {
  std::cout << "closing...." << std::endl;
  ServerBase::stop();
  std::error_code ec;
  _acceptor.cancel(ec);
  std::cout << "... closed" << std::endl;
}
}  // namespace Network
