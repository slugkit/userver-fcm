#include <fcm/components/client.hpp>

#include "../oauth/token.hpp"

#include <userver/clients/http/client.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/crypto/signers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace fcm {

namespace {

constexpr std::string_view kDefaultBaseUrl = "https://fcm.googleapis.com";
constexpr std::string_view kDefaultTokenUrl = "https://oauth2.googleapis.com/token";
constexpr auto kDefaultRefreshMargin = std::chrono::minutes{5};
constexpr auto kDefaultRequestTimeout = std::chrono::seconds{10};

auto FcmUrl(std::string_view base_url, std::string_view project_id) -> std::string {
    return fmt::format("{}/v1/projects/{}/messages:send", base_url, project_id);
}

auto DoSend(
    userver::clients::http::Client& http_client,
    std::string_view url,
    std::string_view access_token,
    const Notification& notification,
    std::chrono::milliseconds timeout
) -> SendResult {
    if (notification.device_token.empty()) {
        return SendResult{
            .status_code = 400, .message_name = {}, .error_code = {}, .error_message = "Empty device_token"
        };
    }

    namespace json = userver::formats::json;

    json::ValueBuilder message_builder = json::FromString(notification.payload);
    message_builder["token"] = notification.device_token;

    json::ValueBuilder envelope;
    envelope["message"] = message_builder.ExtractValue();
    auto body = json::ToString(envelope.ExtractValue());

    std::shared_ptr<userver::clients::http::Response> response;
    try {
        response = http_client.CreateRequest()
                       .post(std::string{url}, body)
                       .headers({
                           {"Authorization", fmt::format("Bearer {}", access_token)},
                           {"Content-Type", "application/json"},
                       })
                       .timeout(timeout)
                       .perform();
    } catch (const std::exception& e) {
        LOG_ERROR() << "FCM request failed: " << e.what();
        return SendResult{.status_code = 0, .message_name = {}, .error_code = {}, .error_message = e.what()};
    }

    auto status = static_cast<std::int32_t>(response->status_code());
    auto response_body = response->body();

    if (status == 200) {
        try {
            auto json_resp = json::FromString(response_body);
            return SendResult{
                .status_code = 200,
                .message_name = json_resp["name"].As<std::string>(""),
                .error_code = {},
                .error_message = {},
            };
        } catch (const std::exception&) {
            return SendResult{.status_code = 200, .message_name = {}, .error_code = {}, .error_message = {}};
        }
    }

    LOG_WARNING() << "FCM error: status=" << status << ", body=" << response_body;

    std::string error_code;
    std::string error_message;
    try {
        auto json_resp = json::FromString(response_body);
        auto error = json_resp["error"];
        error_code = error["status"].As<std::string>("");
        error_message = error["message"].As<std::string>("");
    } catch (const std::exception&) {
        error_message = response_body;
    }

    return SendResult{
        .status_code = status,
        .message_name = {},
        .error_code = std::move(error_code),
        .error_message = std::move(error_message),
    };
}

}  // namespace

struct Client::Impl {
    userver::components::HttpClient& http_client;
    Credentials credentials;
    std::string base_url;
    std::string token_url;
    std::string fcm_url;
    std::chrono::milliseconds request_timeout;
    std::chrono::seconds refresh_margin;

    userver::rcu::Variable<std::string> access_token;
    userver::utils::PeriodicTask refresh_task;

    Impl(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context)
        : http_client(context.FindComponent<userver::components::HttpClient>())
        , credentials(Credentials::FromConfig(config))
        , base_url(config["base-url"].As<std::string>(std::string{kDefaultBaseUrl}))
        , token_url(config["oauth-token-url"].As<std::string>(std::string{kDefaultTokenUrl}))
        , fcm_url(FcmUrl(base_url, credentials.project_id))
        , request_timeout(config["request-timeout"].As<std::chrono::milliseconds>(kDefaultRequestTimeout))
        , refresh_margin(config["token-refresh-margin"].As<std::chrono::seconds>(kDefaultRefreshMargin)) {
        // The default credential is optional: a consumer that only ever calls
        // the per-credential Send(creds, ...) overload (e.g. a multi-tenant
        // dispatcher) can register the client with no project-id/client-email/
        // private-key-pem and skip the start-up token refresh entirely.
        const bool has_default_credential = !credentials.project_id.empty() || !credentials.client_email.empty() ||
                                            !credentials.private_key_pem.empty();
        if (!has_default_credential) {
            LOG_INFO() << "fcm-client: no default credential configured; per-credential Send only";
            return;
        }

        // When any default field is set, all three are required.
        if (credentials.project_id.empty()) {
            throw std::runtime_error("fcm-client: project-id is not configured (set FCM_PROJECT_ID)");
        }
        if (credentials.client_email.empty()) {
            throw std::runtime_error("fcm-client: client-email is not configured (set FCM_CLIENT_EMAIL)");
        }
        if (credentials.private_key_pem.empty()) {
            throw std::runtime_error("fcm-client: private-key-pem is not configured (set FCM_PRIVATE_KEY_PEM)");
        }
        try {
            userver::crypto::SignerRs256{credentials.private_key_pem};
        } catch (const std::exception& e) {
            throw std::runtime_error(fmt::format("fcm-client: invalid private-key-pem: {}", e.what()));
        }

        RefreshToken();
    }

    ~Impl() {
        refresh_task.Stop();
    }

    void RefreshToken() {
        auto result = oauth::ObtainAccessToken(
            http_client.GetHttpClient(), credentials.client_email, credentials.private_key_pem, token_url
        );
        access_token.Assign(std::move(result.token));

        auto next_refresh = result.expires_in - refresh_margin;
        if (next_refresh.count() <= 0) {
            next_refresh = std::chrono::seconds{60};
        }
        LOG_INFO() << "FCM OAuth2 token refreshed, next refresh in " << next_refresh.count() << "s";

        refresh_task.Start(
            "fcm-token-refresh",
            userver::utils::PeriodicTask::Settings{std::chrono::duration_cast<std::chrono::milliseconds>(next_refresh)},
            [this] { RefreshToken(); }
        );
    }

    auto Send(const Notification& notification) const -> SendResult {
        auto token = access_token.Read();
        return DoSend(http_client.GetHttpClient(), fcm_url, *token, notification, request_timeout);
    }

    auto Send(const Credentials& creds, const Notification& notification) const -> SendResult {
        auto result =
            oauth::ObtainAccessToken(http_client.GetHttpClient(), creds.client_email, creds.private_key_pem, token_url);
        return DoSend(
            http_client.GetHttpClient(), FcmUrl(base_url, creds.project_id), result.token, notification, request_timeout
        );
    }
};

Client::Client(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context)
    : userver::components::ComponentBase(config, context)
    , impl_{config, context} {
}

Client::~Client() = default;

auto Client::GetStaticConfigSchema() -> userver::yaml_config::Schema {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(R"(
type: object
description: Google Firebase Cloud Messaging client
additionalProperties: false
properties:
    project-id:
        type: string
        description: Firebase project ID
    client-email:
        type: string
        description: Service account email
    private-key-pem:
        type: string
        description: RSA private key from service account JSON
    base-url:
        type: string
        description: FCM API base URL (override for testing / proxying)
        defaultDescription: https://fcm.googleapis.com
    oauth-token-url:
        type: string
        description: OAuth2 token endpoint URL (override for testing / proxying)
        defaultDescription: https://oauth2.googleapis.com/token
    token-refresh-margin:
        type: string
        description: Refresh token this long before expiry
        defaultDescription: 5m
    request-timeout:
        type: string
        description: HTTP request timeout
        defaultDescription: 10s
    )");
}

auto Client::Send(const Notification& notification) const -> SendResult {
    return impl_->Send(notification);
}

auto Client::Send(const Credentials& credentials, const Notification& notification) const -> SendResult {
    return impl_->Send(credentials, notification);
}

}  // namespace fcm
