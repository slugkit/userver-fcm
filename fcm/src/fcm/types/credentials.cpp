#include <fcm/types/credentials.hpp>

namespace fcm {

auto Credentials::FromConfig(const userver::components::ComponentConfig& config) -> Credentials {
    return Credentials{
        .project_id = config["project-id"].As<std::string>(),
        .client_email = config["client-email"].As<std::string>(),
        .private_key_pem = config["private-key-pem"].As<std::string>(),
    };
}

}  // namespace fcm
