#pragma once

#include <cstdint>
#include <string>

namespace fcm {

struct SendResult {
    /// HTTP status from FCM (200 on success, 400-503 on error),
    /// or 0 if the request never reached FCM (network/TLS/timeout).
    std::int32_t status_code;
    std::string message_name;
    std::string error_code;
    std::string error_message;
};

}  // namespace fcm
