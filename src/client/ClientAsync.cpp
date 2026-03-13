
#include "client/ClientAsync.h"


namespace Network 
{

    
    ClientAsync::ClientAsync(std::string_view host, uint16_t port) : ClientBase(host, port) {}
    
    asio::awaitable<std::expected<AsioTcpSocket, std::error_code>> ClientAsync::connect([[maybe_unused]]Options opts)
    {
    }
    
}