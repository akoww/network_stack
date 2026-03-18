#pragma once

#include "FileTransfer.h"

#include <expected>
#include <memory>

namespace Network {

class FtpFileTransfer : public IAbstractFileTransfer {
public:
  struct Options {
    bool use_passive = false;
  };

private:
};

std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port);
} // namespace Network