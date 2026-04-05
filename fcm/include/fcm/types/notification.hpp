#pragma once

#include <string>

namespace fcm {

struct Notification {
    std::string device_token;
    std::string payload;
};

}  // namespace fcm
