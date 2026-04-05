#pragma once

#include <string>

#include <userver/components/component_config.hpp>

namespace fcm {

struct Credentials {
    std::string project_id;
    std::string client_email;
    std::string private_key_pem;

    static auto FromConfig(const userver::components::ComponentConfig& config) -> Credentials;
};

}  // namespace fcm
