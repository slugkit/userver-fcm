#include <fcm/types/credentials.hpp>

namespace fcm {

auto Credentials::FromConfig(const userver::components::ComponentConfig& config) -> Credentials {
    // All fields default to empty so the client can be registered with no
    // default credential (per-credential Send only). The Client constructor
    // enforces all-or-nothing when any of them is set.
    return Credentials{
        .project_id = config["project-id"].As<std::string>(""),
        .client_email = config["client-email"].As<std::string>(""),
        .private_key_pem = config["private-key-pem"].As<std::string>(""),
    };
}

}  // namespace fcm
