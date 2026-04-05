#include "token.hpp"

#include <userver/clients/http/client.hpp>
#include <userver/crypto/base64.hpp>
#include <userver/crypto/signers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/datetime.hpp>

namespace fcm::oauth {

namespace json = userver::formats::json;

namespace {

constexpr std::string_view kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr std::string_view kScope = "https://www.googleapis.com/auth/firebase.messaging";
constexpr auto kJwtLifetime = std::chrono::seconds{3600};

auto Base64UrlEncode(std::string_view data) -> std::string {
    return userver::crypto::base64::Base64UrlEncode(data, userver::crypto::base64::Pad::kWithout);
}

auto BuildSignedJwt(std::string_view client_email, std::string_view private_key_pem) -> std::string {
    json::ValueBuilder header_builder;
    header_builder["alg"] = "RS256";
    header_builder["typ"] = "JWT";
    auto header = json::ToString(header_builder.ExtractValue());

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   userver::utils::datetime::Now().time_since_epoch()
    )
                   .count();

    json::ValueBuilder claims_builder;
    claims_builder["iss"] = client_email;
    claims_builder["scope"] = kScope;
    claims_builder["aud"] = kTokenUrl;
    claims_builder["iat"] = now;
    claims_builder["exp"] = now + kJwtLifetime.count();
    auto claims = json::ToString(claims_builder.ExtractValue());

    auto encoded_header = Base64UrlEncode(header);
    auto encoded_claims = Base64UrlEncode(claims);
    auto signing_input = fmt::format("{}.{}", encoded_header, encoded_claims);

    userver::crypto::SignerRs256 signer{std::string{private_key_pem}};
    auto signature_raw = signer.Sign({signing_input});
    auto encoded_signature = Base64UrlEncode(signature_raw);

    return fmt::format("{}.{}", signing_input, encoded_signature);
}

}  // namespace

auto ObtainAccessToken(
    userver::clients::http::Client& http_client,
    std::string_view client_email,
    std::string_view private_key_pem
) -> AccessToken {
    auto jwt = BuildSignedJwt(client_email, private_key_pem);

    auto body = fmt::format(
        "grant_type={}&assertion={}",
        "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer",
        jwt
    );

    auto response = http_client.CreateRequest()
                        .post(std::string{kTokenUrl}, body)
                        .headers({{"Content-Type", "application/x-www-form-urlencoded"}})
                        .timeout(std::chrono::seconds{10})
                        .perform();

    auto status = static_cast<std::int32_t>(response->status_code());
    auto response_body = response->body();

    if (status != 200) {
        LOG_ERROR() << "OAuth2 token exchange failed: status=" << status << ", body=" << response_body;
        throw std::runtime_error(fmt::format("OAuth2 token exchange failed: HTTP {}", status));
    }

    auto json_response = json::FromString(response_body);
    auto access_token = json_response["access_token"].As<std::string>();
    auto expires_in = std::chrono::seconds{json_response["expires_in"].As<std::int64_t>()};

    return AccessToken{
        .token = std::move(access_token),
        .expires_in = expires_in,
    };
}

}  // namespace fcm::oauth
