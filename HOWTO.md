# Network Stack - HOWTO

This guide explains how to use the network stack for synchronous and asynchronous client/server communication with proper lifetime management.

## Quick Start

```cpp
#include "core/Context.h"
#include "server/ServerAsync.h"
#include "client/ClientAsync.h"

// 1. Create io_context wrapper
Network::IoContextWrapper io_ctx;
io_ctx.start();

// 2. Start server (async)
MyAsyncServer server(8080, io_ctx);
asio::co_spawn(io_ctx, server.listen(), asio::detached);

// 3. Connect client (async)
ClientAsync client("127.0.0.1", 8080, io_ctx);
auto result = co_await client.connect({});
if (result) { /* use socket */ }

// 4. Cleanup
server.stop();
io_ctx.stop();
```

**Important:** Always track client connections in `ClientConnection` struct to prevent dangling pointers and thread leaks. Server destructor must cancel sockets and join all client coroutines/threads.

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
auto result = client.connect({});
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

Use `IoContextWrapper` for a managed `io_context` with background thread:

```cpp
#include "core/Context.h"

Network::IoContextWrapper io_ctx;
io_ctx.start();  // Start background thread

// ... your code ...

io_ctx.stop();  // Stop and join thread
```

**Important:** `IoContextWrapper` must outlive all async operations and client/server objects

---

## Lifetime Management

**Why do we need `ClientConnection`?** The examples use a `ClientConnection` struct to track active connections. Without it, you'd get **dangling pointers** and **thread leaks**:

### The Problem

```cpp
// ❌ BAD: Socket destroyed immediately after handler returns
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
// ✅ GOOD: Socket and thread tracked in struct
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

~Server()
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

Async servers use coroutines to handle multiple clients concurrently. The server accepts connections and spawns coroutines for each client.

#### Create a Custom Server

```cpp
#include "server/ServerAsync.h"
#include "socket/DualSocket.h"

class MyAsyncServer : public Network::ServerAsync
{
  struct ClientConnection
  {
    std::future<void> future;
    std::unique_ptr<Network::DualSocket> sock;
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;

  void handle_client_impl(asio::io_context& context, std::unique_ptr<Network::DualSocket> sock)
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
        context,
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
  MyAsyncServer(uint16_t port, asio::io_context& io_ctx)
    : ServerAsync(port, io_ctx, 
      [this, &io_ctx](std::unique_ptr<Network::DualSocket> sock)
      { handle_client_impl(io_ctx, std::move(sock)); })
  {
  }

  ~MyAsyncServer()
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
asio::io_context io_ctx;
Network::IoContextWrapper io_wrapper;  // or use existing io_context
MyAsyncServer server(8080, io_ctx);

asio::co_spawn(
  io_ctx,
  [&server]() -> asio::awaitable<void>
  {
    auto result = co_await server.listen();
    if (!result)
    {
      // handle error: result.error()
    }
  },
  asio::detached);  // Start listening
```

**Important:**
- Use `asio::co_spawn(..., asio::detached)` - never `co_await server.listen()` directly
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
#include "client/ClientAsync.h"

asio::io_context io_ctx;
Network::ClientAsync client("127.0.0.1", 8080, io_ctx);

auto result = co_await client.connect({});
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
// Write data
std::string msg = "Hello, Server!";
auto send_result = co_await socket->asyncWriteAll(
  std::as_bytes(std::span(msg)));
if (!send_result)
{
  // handle error
}

// Read data
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

Sync servers use threads to handle clients. Each client connection runs in its own thread.

#### Create a Custom Server

```cpp
#include "server/ServerSync.h"

class MySyncServer : public Network::ServerSync
{
  struct ClientConnection
  {
    unsigned int id;
    std::thread thread;              // Stores the thread running the client handler
    std::unique_ptr<Network::DualSocket> sock;  // Keeps socket alive, prevents dangling pointer
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;  // Track all active connections for lifetime management

  void handle_client(std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    // Why ClientConnection?
    // 1. We need to store the socket (unique_ptr) to prevent it from being destroyed
    // 2. We need the thread to join it later (can't join if detached)
    // Without this tracking, threads would be leaked and sockets would be dangling
    
    ClientConnection entry;
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    Network::DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
    }

    // Start thread to handle client (NOT detached - server must join it)
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
  MySyncServer(uint16_t port, asio::io_context& io_ctx)
    : ServerSync(port, io_ctx, [this](std::unique_ptr<Network::DualSocket> sock)
                 { handle_client(std::move(sock)); })
  {
  }

  ~MySyncServer()
  {
    // Clean up all client connections before server is destroyed
    // 1. Cancel all sockets to stop pending operations
    // 2. Move clients to temp vector to release lock quickly
    // 3. Join all threads to wait for them to finish
    
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
        c.thread.join();  // Wait for thread to finish (blocks until done)
    }
  }
};
```

#### Start the Server

```cpp
asio::io_context io_ctx;
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
#include "client/ClientSync.h"

asio::io_context io_ctx;
Network::ClientSync client("127.0.0.1", 8080, io_ctx);

auto result = client.connect({});
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

```cpp
// Async server
server.getSslContext()->use_certificate_chain_file("path/to/server.crt");
server.getSslContext()->use_private_key_file("path/to/server.key", asio::ssl::context::pem);
asio::co_spawn(io_ctx, server.listen_tls(), asio::detached);

// Sync server
server.getSslContext()->use_certificate_chain_file("path/to/server.crt");
server.getSslContext()->use_private_key_file("path/to/server.key", asio::ssl::context::pem);
server.listen_tls();  // in thread
```

### Client TLS Configuration

```cpp
// Async client
ClientAsync client("host", port, io_ctx);
client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
auto result = co_await client.connect_tls({});

// Sync client
ClientSync client("host", port, io_ctx);
client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
auto result = client.connect_tls({});
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
#include "server/ServerAsync.h"
#include "socket/DualSocket.h"

class EchoServer : public ServerAsync
{
  struct ClientConnection
  {
    std::future<void> future;        // Stores the coroutine future to join it later
    std::unique_ptr<DualSocket> sock;  // Keeps socket alive, prevents dangling pointer
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;  // Track all active connections for lifetime management

  void handle_client_impl(asio::io_context& context, std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    // Why ClientConnection?
    // 1. We need to store the socket (unique_ptr) to prevent it from being destroyed
    // 2. We need the future to wait for the coroutine to finish before server destructs
    // Without this tracking, sockets would be destroyed while coroutines still run -> dangling pointers
    
    ClientConnection entry{std::future<void>{}, std::move(sock)};
    DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
      auto& client_ref = _clients.back();

      // Spawn coroutine and store its future for later joining
      client_ref.future = asio::co_spawn(
        context,
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
  EchoServer(uint16_t port, asio::io_context& io_ctx)
    : ServerAsync(port, io_ctx, 
      [this, &io_ctx](std::unique_ptr<DualSocket> sock)
      { handle_client_impl(io_ctx, std::move(sock)); })
  {
  }

  ~EchoServer()
  {
    // Clean up all client connections before server is destroyed
    // 1. Cancel all sockets to stop pending operations
    // 2. Move clients to temp vector to release lock
    // 3. Join all futures to wait for coroutines to finish
    
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
        c.future.get();  // Wait for coroutine to finish (blocks until done)
    }
  }
};
```

#### Sync Echo Server

```cpp
#include "server/ServerSync.h"
#include "socket/DualSocket.h"

class EchoServer : public ServerSync
{
  struct ClientConnection
  {
    unsigned int id;
    std::thread thread;              // Stores the thread running the client handler
    std::unique_ptr<DualSocket> sock;  // Keeps socket alive, prevents dangling pointer
  };

  mutable std::mutex _mutex;
  std::vector<ClientConnection> _clients;  // Track all active connections for lifetime management

  void handle_client(std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    // Why ClientConnection?
    // 1. We need to store the socket (unique_ptr) to prevent it from being destroyed
    // 2. We need the thread to join it later (can't join if detached)
    // Without this tracking, threads would be leaked and sockets would be dangling
    
    ClientConnection entry;
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _clients.push_back(std::move(entry));
    }

    // Start thread to handle client (NOT detached - server must join it)
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
  EchoServer(uint16_t port, asio::io_context& io_ctx)
    : ServerSync(port, io_ctx, [this](std::unique_ptr<DualSocket> sock)
                 { handle_client(std::move(sock)); })
  {
  }

  ~EchoServer()
  {
    // Clean up all client connections before server is destroyed
    // 1. Cancel all sockets to stop pending operations
    // 2. Move clients to temp vector to release lock quickly
    // 3. Join all threads to wait for them to finish
    
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
        c.thread.join();  // Wait for thread to finish (blocks until done)
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
    // Server created automatically, io_context started
  }
};

// Sync test fixture
class MySyncTest : public Network::Test::SyncClientServerFixture
{
protected:
  void SetUp() override
  {
    Network::Test::SyncClientServerFixture::SetUp();
    // Server created automatically, io_context started
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
auto result = client.connect({});
EXPECT_TRUE(!result.has_value());  // No server listening
```

### Invalid Host

```cpp
ClientAsync client("invalid.host", port, io_ctx);
auto result = co_await client.connect({});
EXPECT_TRUE(!result.has_value());  // DNS resolution failed
```

### Server Already Stopped

```cpp
server.stop();
auto result = co_await server.listen();  // Will return error
```

### Socket Cancelled

```cpp
socket->cancelSocket();  // All pending operations will complete with error
```

### Thread Safety

- Server client vectors use `std::mutex` for thread safety
- Client sockets are single-threaded (don't share across threads)
- Async coroutines run on io_context thread (no additional locking needed)

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
#include "server/ServerAsync.h"
#include "client/ClientAsync.h"
#include "core/Context.h"

// Server (from above)
class EchoServer : public ServerAsync { /* ... */ };

int main()
{
  asio::io_context io_ctx;
  Network::IoContextWrapper io_wrapper;  // For background thread

  EchoServer server(8080, io_ctx);
  
  asio::co_spawn(
    io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto result = co_await server.listen();
      if (!result)
      {
        std::cerr << "Server failed: " << result.error().message() << std::endl;
        return;
      }
    },
    asio::detached);

  // Simulate multiple clients
  // Note: In production code, track client threads in a vector and join them
  std::vector<std::thread> client_threads;
  for (int i = 0; i < 3; i++)
  {
    client_threads.emplace_back([i, &io_ctx]()
      {
        asio::io_context client_ctx;
        Network::ClientAsync client("127.0.0.1", 8080, client_ctx);
        
        auto result = asio::co_spawn(
          client_ctx,
          [&]() -> asio::awaitable<std::expected<std::unique_ptr<Network::DualSocket>, std::error_code>>
          {
            co_return co_await client.connect({});
          },
          asio::use_future).get();

        if (result)
        {
            auto socket = std::move(*result);
          // communicate with server...
        }
      });
  }

  io_ctx.run();  // Run async operations

  // Join all client threads before exiting
  for (auto& t : client_threads)
    t.join();
  
  return 0;
}
```

### Sync Echo Server with Multiple Clients

```cpp
#include "server/ServerSync.h"
#include "client/ClientSync.h"
#include "core/Context.h"

class EchoServer : public ServerSync { /* ... */ };

int main()
{
  asio::io_context io_ctx;

  EchoServer server(8080, io_ctx);
  std::thread server_thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Create multiple clients
  std::vector<std::thread> client_threads;
  for (int i = 0; i < 3; i++)
  {
    client_threads.emplace_back([i, &io_ctx]()
      {
        Network::ClientSync client("127.0.0.1", 8080, io_ctx);
        auto result = client.connect({});
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

1. **Always track sockets**: Store `unique_ptr<Socket>` to ensure proper lifetime
2. **Cancel before close**: Call `cancelSocket()` to drain pending operations
3. **Join threads**: Sync servers must join all client threads before destruction
4. **Use fixtures**: Test fixtures handle io_context and cleanup automatically
5. **Check results**: Always verify `std::expected` before using value
6. **Timeouts**: Specify connection timeouts to prevent hanging
7. **No_DETACH**: Never detach client threads or let coroutines run after server stops

---

## See Also

- `tests/test_async_client_server.cpp` - Complete async examples
- `tests/test_sync_client_server.cpp` - Complete sync examples
- `tests/test_async_tls.cpp` - Async TLS examples
- `tests/test_sync_tls.cpp` - Sync TLS examples
- `tests/fixtures/test_fixture_async_client_server.h` - Async fixture
- `tests/fixtures/test_fixture_sync_client_server.h` - Sync fixture
