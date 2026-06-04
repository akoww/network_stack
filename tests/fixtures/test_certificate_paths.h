#pragma once

#include <filesystem>

#ifdef SOURCE_DIR_CERT
  #define XSTR(s) #s
  #define STR(s) XSTR(s)
#else
  #error "SOURCE_DIR_CERT must be defined via CMake compile definition"
#endif

namespace Network::Test
{

inline std::filesystem::path CertDirectory()
{
  return SOURCE_DIR_CERT;
}

inline std::filesystem::path ServerCertPath()
{
  return CertDirectory() / "server.crt";
}

inline std::filesystem::path ServerKeyPath()
{
  return CertDirectory() / "server.key";
}

inline std::filesystem::path ClientCertPath()
{
  return CertDirectory() / "client.crt";
}

inline std::filesystem::path ClientKeyPath()
{
  return CertDirectory() / "client.key";
}

inline std::filesystem::path CaCertPath()
{
  return CertDirectory() / "ca.crt";
}

inline std::filesystem::path SelfSignedCertPath()
{
  return CertDirectory() / "self_signed.crt";
}

inline std::filesystem::path SelfSignedKeyPath()
{
  return CertDirectory() / "self_signed.key";
}

}  // namespace Network::Test