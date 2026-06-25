#pragma once

#include <string>
#include <string_view>

#include <userver/clients/http/client.hpp>

namespace fcm::oauth {

struct AccessToken {
    std::string token;
    std::chrono::seconds expires_in;
};

auto ObtainAccessToken(
    userver::clients::http::Client& http_client,
    std::string_view client_email,
    std::string_view private_key_pem,
    std::string_view token_url
) -> AccessToken;

}  // namespace fcm::oauth
