# Network Stack - HOWTO

This guide explains how to use the network stack for synchronous and asynchronous client/server communication with proper lifetime management.

## Quick Start

```cpp
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "core/details/ContextDetail.h"
#include "server/Server.h"
#include "client/Client.h"

// 1. Create io_context wrapper (thread pool starts automatically)
Network::IoContextWrapper io_ctx;

// 2. Start server (async) - pass a ClientHandler callback
Network::Server server(8080, io_ctx, [](std::unique_ptr<Network::DualSocket> sock)
{
  // handle client...
});
asio::co_spawn(Network::detail::getExecutor(io_ctx), server.asyncListen(), asio::detached);

// 3. Connect client (async)
Network::Client client("127.0.0.1", 8080, io_ctx);
auto connect_future = asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  [&client]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
  {
    co_return co_await client.asyncConnect();
  },
  asio::use_future);
auto result = connect_future.get();
if (result) { /* use *result */ }

// 4. Cleanup
server.stop();
io_ctx.shutdown();
```

**Important:** Always track client connections in a struct to prevent dangling pointers and thread leaks. Server destructor must cancel sockets and join all client coroutines/threads.

---

## Table of Contents

- [Core Concepts](#core-concepts)
- [Async Client/Server](#async-client-server)
- [Sync Client/Server](#sync-client-server)
- [TLS Support](#tls-support)
- [Common Patterns](#common-patterns)
- [Troubleshooting](#troubleshooting)

---

## Core Concepts

### Error Handling

All operations return `std::expected<T, std::error_code>` - **no exceptions**:

```cpp
auto result = client.connect();
if (result)
{
  // success: *result contains the socket
}
else
{
  // error: result.error() contains the error code
}
```

### IoContextWrapper

Use `IoContextWrapper` for a managed `asio::thread_pool` with background threads:

```cpp
#include "core/Context.h"

Network::IoContextWrapper io_ctx;        // Thread pool starts immediately (default 4 threads)
Network::IoContextWrapper io_ctx(8);     // Or specify thread count

// ... your code ...

io_ctx.shutdown();  // Optional: explicit, idempotent shutdown
```

`IoContextWrapper` is a copyable value type backed by `std::shared_ptr`. It is **not** a singleton. The thread pool starts automatically on construction. `shutdown()` is optional and idempotent - the destructor also cleans up.

**Important:** `IoContextWrapper` must outlive all async operations and client/server objects that reference it.

### Sync vs Async

`Server` and `Client` are unified classes that support both sync and async operations:

| Operation | Sync (blocking) | Async (`co_await`) |
|---|---|---|
| Server listen | `server.listen()` | `co_await server.asyncListen()` |
| Client connect | `client.connect()` | `co_await client.asyncConnect()` |
| Socket read | `sock->readSome()` | `co_await sock->asyncReadSome()` |
| Socket write | `sock->writeAll()` | `co_await sock->asyncWriteAll()` |

**`co_await` only works with the `async*` methods.** The sync methods block the calling thread and cannot be awaited.

### Getting the Executor

Many ASIO functions (like `co_spawn`) need an executor. Get it from `IoContextWrapper` via:

```cpp
#include "core/details/ContextDetail.h"

asio::any_io_executor exec = Network::detail::getExecutor(io_ctx);
```

---

## Lifetime Management

**Why do we need a `ClientConnection` struct?** The examples use a struct to track active connections. Without it, you'd get **dangling pointers** and **thread leaks**:

### The Problem

```cpp
// BAD: Socket destroyed immediately after handler returns
void handle_client(std::unique_ptr<DualSocket> sock)
{
  std::thread t([sock = std::move(sock)]() {
    sock->readSome(...);  // Dangling pointer! sock is destroyed when handler returns
  });
  t.detach();  // Thread leaked, no way to join it later
}
```

### The Solution

```cpp
// GOOD: Socket and thread tracked in struct
struct ClientConnection
{
  std::unique_ptr<DualSocket> sock;  // Keeps socket alive
  std::thread thread;                 // Track thread for joining
};

void handle_client(std::unique_ptr<DualSocket> sock)
{
  ClientConnection entry{std::move(sock)};

  {
    std::lock_guard<std::mutex> lock(mutex);
    clients.push_back(std::move(entry));  // Store in vector
  }

  clients.back().thread = std::thread(
    [sock_ptr = clients.back().sock.get()]() {
      sock_ptr->readSome(...);  // Safe - sock is kept alive by vector
    });
}

~MyServer()
{
  for (auto& c : clients)
  {
    c.sock->cancelSocket();  // Cancel pending operations
    c.thread.join();         // Join thread to wait for cleanup
  }
}  // Destructor ensures all resources are cleaned up
```

### Key Rules

1. **Async servers**: Store `std::future<void>` to wait for coroutines to finish
2. **Sync servers**: Store `std::thread` to join threads before destruction
3. **Always**: Store `std::unique_ptr<DualSocket>` to prevent dangling pointers
4. **Never**: Detach client threads (always track and join them)
5. **Always**: Cancel sockets before destroying to drain pending operations

---

## Async Client/Server

### Server Overview

Async servers use coroutines to handle multiple clients concurrently. The server accepts connections and invokes the `ClientHandler` callback for each new connection.

#### Create a Custom Server

```cpp
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "core/details/ContextDetail.h"
#include "server/Server.h"
#include "socket/DualSocket.h"

class MyServer : public Network::Server
{
  struct ClientConnection
  {
    std::future<void> future;
    std::unique_ptr<Network::DualSocket> sock;
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;

  void handle_client_impl(asio::any_io_executor executor, std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    ClientConnection entry{std::future<void>{}, std::move(sock)};
    Network::DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
      auto& client_ref = _clients.back();

      client_ref.future = asio::co_spawn(
        executor,
        [sock_ptr]() mutable -> asio::awaitable<void>
        {
          std::array<std::byte, 1024> buffer{};
          while (true)
          {
            auto recv_result = co_await sock_ptr->asyncReadSome(std::span(buffer));
            if (!recv_result || *recv_result == 0)
              break;

            auto send_result = co_await sock_ptr->asyncWriteAll(std::span(buffer).first(*recv_result));
            if (!send_result)
              break;
          }
          co_return;
        },
        asio::use_future);
    }
  }

public:
  MyServer(uint16_t port, Network::IoContextWrapper io_ctx)
    : Server(port, io_ctx,
             [this, executor = Network::detail::getExecutor(io_ctx)](std::unique_ptr<Network::DualSocket> sock)
             { handle_client_impl(executor, std::move(sock)); })
  {
  }

  ~MyServer() override
  {
    std::vector<ClientConnection> to_cleanup;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& c : _clients)
      {
        if (c.sock)
          c.sock->cancelSocket();
        to_cleanup.push_back(std::move(c));
      }
      _clients.clear();
    }

    for (auto& c : to_cleanup)
    {
      if (c.future.valid())
        c.future.get();
    }
  }
};
```

#### Start the Server

```cpp
Network::IoContextWrapper io_ctx;
MyServer server(8080, io_ctx);

asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  server.asyncListen(),
  asio::detached);  // Start listening (fire-and-forget)
```

**Important:**
- Use `asio::co_spawn(..., server.asyncListen(), asio::detached)` - never `co_await server.asyncListen()` directly (it's an infinite accept loop)
- The `ClientHandler` callback is invoked for each accepted connection
- Server destructor cancels all client sockets and waits for coroutines to finish

#### Stop the Server

```cpp
server.stop();  // Stop accepting new connections
// Destructor will join all client coroutines
```

### Client Overview

Clients connect to remote servers using coroutines.

#### Connect to a Server

```cpp
#include "core/ErrorCodes.h"
#include "client/Client.h"

Network::IoContextWrapper io_ctx;
Network::Client client("127.0.0.1", 8080, io_ctx);

// asyncConnect() must be co_awaited inside a coroutine
auto connect_future = asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  [&client]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
  {
    co_return co_await client.asyncConnect();
  },
  asio::use_future);

auto result = connect_future.get();
if (result)
{
  auto socket = std::move(*result);
  // Use socket for async operations
}
else
{
  // handle error: result.error()
}
```

#### Send and Receive Data

```cpp
// Write data (must be inside a coroutine)
std::string msg = "Hello, Server!";
auto send_result = co_await socket->asyncWriteAll(
  std::as_bytes(std::span(msg)));
if (!send_result)
{
  // handle error
}

// Read data (must be inside a coroutine)
std::array<std::byte, 1024> buffer{};
auto recv_result = co_await socket->asyncReadSome(std::span(buffer));
if (recv_result && *recv_result > 0)
{
  // received *recv_result bytes
  std::string_view response(reinterpret_cast<const char*>(buffer.data()), *recv_result);
}
```

#### Close the Connection

```cpp
socket->closeSocket();  // Graceful close
// or
socket->cancelSocket();  // Cancel all pending operations
```

---

## Sync Client/Server

### Server Overview

Sync servers use threads to handle clients. Each client connection runs in its own thread. The `listen()` call blocks, so it must run in a separate thread.

#### Create a Custom Server

```cpp
#include "core/ErrorCodes.h"
#include "server/Server.h"

class MySyncServer : public Network::Server
{
  struct ClientConnection
  {
    unsigned int id;
    std::thread thread;
    std::unique_ptr<Network::DualSocket> sock;
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;

  void handle_client(std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    ClientConnection entry;
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    Network::DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
    }

    auto& client_ref = _clients.back();
    client_ref.thread = std::thread(
      [sock_ptr, id = client_ref.id]()
      {
        std::array<std::byte, 1024> buffer{};
        bool running = true;

        while (running)
        {
          auto recv_result = sock_ptr->readSome(std::span(buffer));
          if (!recv_result || *recv_result == 0)
          {
            running = false;
            break;
          }

          auto send_result = sock_ptr->writeAll(std::span(buffer).first(*recv_result));
          if (!send_result)
          {
            running = false;
            break;
          }
        }
      });
  }

public:
  MySyncServer(uint16_t port, Network::IoContextWrapper io_ctx)
    : Server(port, io_ctx, [this](std::unique_ptr<Network::DualSocket> sock)
             { handle_client(std::move(sock)); })
  {
  }

  ~MySyncServer() override
  {
    std::vector<ClientConnection> to_join;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& c : _clients)
      {
        if (c.thread.joinable())
          to_join.push_back(std::move(c));
      }
      _clients.clear();
    }

    for (auto& c : to_join)
    {
      if (c.sock)
        c.sock->cancelSocket();
      if (c.thread.joinable())
        c.thread.join();
    }
  }
};
```

#### Start the Server

```cpp
Network::IoContextWrapper io_ctx;
MySyncServer server(8080, io_ctx);

std::thread server_thread([&server]() { server.listen(); });
std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Wait for server to start
```

#### Stop the Server

```cpp
server.stop();          // Signal server to stop accepting
server_thread.join();   // Wait for server thread to exit
// Destructor joins all client threads
```

### Client Overview

Clients connect to remote servers using blocking calls.

#### Connect to a Server

```cpp
#include "core/ErrorCodes.h"
#include "client/Client.h"

Network::IoContextWrapper io_ctx;
Network::Client client("127.0.0.1", 8080, io_ctx);

auto result = client.connect();
if (result)
{
  auto socket = std::move(*result);
  // Use socket for sync operations
}
else
{
  // handle error: result.error()
}
```

#### Send and Receive Data

```cpp
// Write data
std::string msg = "Hello, Server!";
auto send_result = socket->writeAll(
  std::as_bytes(std::span(msg)));
if (!send_result)
{
  // handle error
}

// Read data
std::array<std::byte, 1024> buffer{};
auto recv_result = socket->readSome(std::span(buffer));
if (recv_result && *recv_result > 0)
{
  // received *recv_result bytes
  std::string_view response(reinterpret_cast<const char*>(buffer.data()), *recv_result);
}
```

#### Close the Connection

```cpp
socket->closeSocket();  // Graceful close
// or
socket->cancelSocket();  // Cancel all pending operations
```

---

## TLS Support

### Server TLS Configuration

TLS server options require certificate and key file paths via `TlsServerOptions`:

```cpp
#include "socket/TlsOptions.h"

// Async TLS listen
Network::TlsServerOptions tls_server_opts{"path/to/server.crt", "path/to/server.key"};
asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  server.asyncListenTls(tls_server_opts),
  asio::detached);

// Sync TLS listen (in a thread)
Network::TlsServerOptions tls_server_opts{"path/to/server.crt", "path/to/server.key"};
server.listenTls(tls_server_opts);
```

### Client TLS Configuration

```cpp
// Async client TLS
Network::Client client("host", port, io_ctx);
auto connect_future = asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  [&client]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
  {
    co_return co_await client.asyncConnectTls();
  },
  asio::use_future);
auto result = connect_future.get();

// Sync client TLS
Network::Client client("host", port, io_ctx);
auto result = client.connectTls();
```

### Using Test Certificates

Test certificates are available in `tests/certs/`:

- CA certificate: `tests/certs/ca.crt`
- Server certificate: `tests/certs/server.crt`
- Server key: `tests/certs/server.key`
- Client certificate: `tests/certs/client.crt`
- Client key: `tests/certs/client.key`

---

## Common Patterns

### Echo Server

#### Async Echo Server

```cpp
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "core/details/ContextDetail.h"
#include "server/Server.h"
#include "socket/DualSocket.h"

class EchoServer : public Network::Server
{
  struct ClientConnection
  {
    std::future<void> future;
    std::unique_ptr<Network::DualSocket> sock;
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;

  void handle_client_impl(asio::any_io_executor executor, std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    ClientConnection entry{std::future<void>{}, std::move(sock)};
    Network::DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
      auto& client_ref = _clients.back();

      client_ref.future = asio::co_spawn(
        executor,
        [sock_ptr]() mutable -> asio::awaitable<void>
        {
          std::array<std::byte, 1024> buffer{};
          while (true)
          {
            auto recv_result = co_await sock_ptr->asyncReadSome(std::span(buffer));
            if (!recv_result || *recv_result == 0)
              break;
            auto send_result = co_await sock_ptr->asyncWriteAll(std::span(buffer).first(*recv_result));
            if (!send_result)
              break;
          }
          co_return;
        },
        asio::use_future);
    }
  }

public:
  EchoServer(uint16_t port, Network::IoContextWrapper io_ctx)
    : Server(port, io_ctx,
             [this, executor = Network::detail::getExecutor(io_ctx)](std::unique_ptr<Network::DualSocket> sock)
             { handle_client_impl(executor, std::move(sock)); })
  {
  }

  ~EchoServer() override
  {
    std::vector<ClientConnection> to_cleanup;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& c : _clients)
      {
        if (c.sock)
          c.sock->cancelSocket();
        to_cleanup.push_back(std::move(c));
      }
      _clients.clear();
    }

    for (auto& c : to_cleanup)
    {
      if (c.future.valid())
        c.future.get();
    }
  }
};
```

#### Sync Echo Server

```cpp
#include "core/ErrorCodes.h"
#include "server/Server.h"
#include "socket/DualSocket.h"

class SyncEchoServer : public Network::Server
{
  struct ClientConnection
  {
    unsigned int id;
    std::thread thread;
    std::unique_ptr<Network::DualSocket> sock;
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;

  void handle_client(std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    ClientConnection entry;
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    Network::DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
    }

    auto& client_ref = _clients.back();
    client_ref.thread = std::thread(
      [sock_ptr, id = client_ref.id]()
      {
        std::array<std::byte, 1024> buffer{};
        bool running = true;

        while (running)
        {
          auto recv_result = sock_ptr->readSome(std::span(buffer));
          if (!recv_result || *recv_result == 0)
          {
            running = false;
            break;
          }

          auto send_result = sock_ptr->writeAll(std::span(buffer).first(*recv_result));
          if (!send_result)
          {
            running = false;
            break;
          }
        }
      });
  }

public:
  SyncEchoServer(uint16_t port, Network::IoContextWrapper io_ctx)
    : Server(port, io_ctx, [this](std::unique_ptr<Network::DualSocket> sock)
             { handle_client(std::move(sock)); })
  {
  }

  ~SyncEchoServer() override
  {
    std::vector<ClientConnection> to_join;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& c : _clients)
      {
        if (c.thread.joinable())
          to_join.push_back(std::move(c));
      }
      _clients.clear();
    }

    for (auto& c : to_join)
    {
      if (c.sock)
        c.sock->cancelSocket();
      if (c.thread.joinable())
        c.thread.join();
    }
  }
};
```

### Using Test Fixtures

The test fixtures provide convenient server/client setups:

```cpp
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_sync_client_server.h"

// Async test fixture
class MyAsyncTest : public Network::Test::AsyncClientServerFixture
{
protected:
  void SetUp() override
  {
    Network::Test::AsyncClientServerFixture::SetUp();
    // IoContextWrapper available via getIoContext()
  }
};

// Sync test fixture
class MySyncTest : public Network::Test::SyncClientServerFixture
{
protected:
  void SetUp() override
  {
    Network::Test::SyncClientServerFixture::SetUp();
    // IoContextWrapper available via getIoContext()
  }
};
```

### Helper Functions

```cpp
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_sync_client_server.h"

// Convert string to bytes
std::span<const std::byte> bytes = Network::Test::to_bytes("hello");

// Convert bytes back to string (with length)
std::string_view sv = Network::Test::to_string_view(buffer, bytes_read);
```

---

## Troubleshooting

### Connection Refused

```cpp
auto result = client.connect();
EXPECT_TRUE(!result.has_value());  // No server listening
```

### Invalid Host

```cpp
Network::Client client("invalid.host", port, io_ctx);
auto connect_future = asio::co_spawn(
  Network::detail::getExecutor(io_ctx),
  [&client]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
  {
    co_return co_await client.asyncConnect();
  },
  asio::use_future);
auto result = connect_future.get();
EXPECT_TRUE(!result.has_value());  // DNS resolution failed
```

### Server Already Stopped

```cpp
server.stop();
auto result = server.listen();  // Will return error
```

### Socket Cancelled

```cpp
socket->cancelSocket();  // All pending operations will complete with error
```

### Thread Safety

- Server client vectors use `std::mutex` for thread safety
- Client sockets are single-threaded (don't share across threads)
- Async coroutines run on the thread pool (no additional locking needed)

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `connection_refused` | No server listening | Start server before client |
| `dns_failure` | Invalid hostname | Check hostname spelling |
| `connection_timeout` | Server not accepting | Check server listen() call |
| `connection_lost` | Peer closed connection | Handle disconnection gracefully |

---

## Complete Examples

### Async Echo Server with Multiple Clients

```cpp
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "core/details/ContextDetail.h"
#include "server/Server.h"
#include "client/Client.h"

// EchoServer class (from above)

int main()
{
  Network::IoContextWrapper io_ctx;

  EchoServer server(8080, io_ctx);

  asio::co_spawn(
    Network::detail::getExecutor(io_ctx),
    server.asyncListen(),
    asio::detached);

  // Simulate multiple clients
  std::vector<std::thread> client_threads;
  for (int i = 0; i < 3; i++)
  {
    client_threads.emplace_back([i, &io_ctx]()
      {
        Network::IoContextWrapper client_io;
        Network::Client client("127.0.0.1", 8080, client_io);

        auto result = asio::co_spawn(
          Network::detail::getExecutor(client_io),
          [&client]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
          {
            co_return co_await client.asyncConnect();
          },
          asio::use_future).get();

        if (result)
        {
          auto socket = std::move(*result);
          // communicate with server...
        }
      });
  }

  for (auto& t : client_threads)
    t.join();

  server.stop();
  io_ctx.shutdown();

  return 0;
}
```

### Sync Echo Server with Multiple Clients

```cpp
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "server/Server.h"
#include "client/Client.h"

// SyncEchoServer class (from above)

int main()
{
  Network::IoContextWrapper io_ctx;

  SyncEchoServer server(8080, io_ctx);
  std::thread server_thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Create multiple clients
  std::vector<std::thread> client_threads;
  for (int i = 0; i < 3; i++)
  {
    client_threads.emplace_back([i, &io_ctx]()
      {
        Network::Client client("127.0.0.1", 8080, io_ctx);
        auto result = client.connect();
        if (result)
        {
          auto socket = std::move(*result);
          // communicate with server...
        }
      });
  }

  for (auto& t : client_threads)
    t.join();

  server.stop();
  server_thread.join();
  return 0;
}
```

---

## Best Practices

1. **Always track sockets**: Store `unique_ptr<DualSocket>` to ensure proper lifetime
2. **Cancel before close**: Call `cancelSocket()` to drain pending operations
3. **Join threads**: Sync servers must join all client threads before destruction
4. **Use fixtures**: Test fixtures handle IoContextWrapper and cleanup automatically
5. **Check results**: Always verify `std::expected` before using value
6. **Timeouts**: Specify connection timeouts (e.g., `std::chrono::seconds(10)`) for production code
7. **No detach**: Never detach client threads or let coroutines run after server stops
8. **Use `async*` for `co_await`**: Only `asyncReadSome`, `asyncWriteAll`, `asyncConnect`, `asyncListen` etc. are awaitable

---

## See Also

- `tests/test_async_client_server.cpp` - Complete async examples
- `tests/test_sync_client_server.cpp` - Complete sync examples
- `tests/test_async_tls.cpp` - Async TLS examples
- `tests/test_sync_tls.cpp` - Sync TLS examples
- `tests/fixtures/test_fixture_async_client_server.h` - Async fixture
- `tests/fixtures/test_fixture_sync_client_server.h` - Sync fixture
