#pragma once

#include <system_error>
#include <string>

namespace Network
{
    // 1. Define the Enum
    // 0 is intentionally reserved for 'no error'
    enum class Error
    {
        NoError = 0,
        ConnectionRefused,
        ConnectionTimeout,
        ConnectionLost,
        DnsFailure,
        ProtocolError
    };

    // 2. Define the Error Category
    class ErrorCategory : public std::error_category
    {
    public:
        // Name of the domain
        const char *name() const noexcept override
        {
            return "network";
        }

        // Human-readable message for the error code
        std::string message(int ev) const override
        {
            switch (static_cast<Error>(ev))
            {
            case Error::NoError:
                return "No error";
            case Error::ConnectionRefused:
                return "Connection refused";
            case Error::ConnectionTimeout:
                return "Connection timed out";
            case Error::ConnectionLost:
                return "Connection lost";
            case Error::DnsFailure:
                return "DNS lookup failed";
            case Error::ProtocolError:
                return "Protocol error";
            default:
                return "Unknown network error";
            }
        }
    };

    // 3. Provide access to the singleton category instance
    inline const ErrorCategory &get_network_category()
    {
        static ErrorCategory instance;
        return instance;
    }

    // 4. Specialize std::is_error_code_enum
    // This allows implicit conversion: std::error_code ec = net::Error::timeout;

    // 5. Helper function (Optional, but good for clarity)
    inline std::error_code make_error_code(Network::Error err)
    {
        return std::error_code(static_cast<int>(err), get_network_category());
    }
}

namespace std
{
    template <>
    struct is_error_code_enum<Network::Error> : true_type
    {
    };
}