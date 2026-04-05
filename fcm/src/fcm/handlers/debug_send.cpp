#include <fcm/handlers/debug_send.hpp>

#include <fcm/components/client.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace fcm::handlers {

namespace uhandlers = userver::server::handlers;
namespace json = userver::formats::json;

struct DebugSend::Impl {
    const Client& fcm_client;

    Impl(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context)
        : fcm_client(context.FindComponent<Client>(config["fcm-client"].As<std::string>("fcm-client"))) {}
};

DebugSend::DebugSend(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : BaseType{config, context}
    , impl_{config, context} {}

DebugSend::~DebugSend() = default;

auto DebugSend::HandleRequestJsonThrow(
    [[maybe_unused]] const userver::server::http::HttpRequest& request,
    const json::Value& request_json,
    [[maybe_unused]] userver::server::request::RequestContext& context
) const -> json::Value {
    if (!request_json.HasMember("device_token")) {
        throw uhandlers::ClientError(
            uhandlers::InternalMessage{"Missing required field: device_token"},
            uhandlers::ExternalBody{R"({"error":"missing field: device_token"})"}
        );
    }
    if (!request_json.HasMember("payload")) {
        throw uhandlers::ClientError(
            uhandlers::InternalMessage{"Missing required field: payload"},
            uhandlers::ExternalBody{R"({"error":"missing field: payload"})"}
        );
    }

    Notification notification;
    notification.device_token = request_json["device_token"].As<std::string>();
    notification.payload = json::ToString(request_json["payload"]);

    LOG_INFO() << "Debug FCM send to device_token=" << notification.device_token;

    auto result = impl_->fcm_client.Send(notification);

    json::ValueBuilder response;
    response["status_code"] = result.status_code;
    response["message_name"] = result.message_name;
    response["error_code"] = result.error_code;
    response["error_message"] = result.error_message;

    if (result.status_code != 200 && result.status_code != 0) {
        auto& http_response = request.GetHttpResponse();
        http_response.SetStatus(static_cast<userver::server::http::HttpStatus>(result.status_code));
    } else if (result.status_code == 0) {
        auto& http_response = request.GetHttpResponse();
        http_response.SetStatus(userver::server::http::HttpStatus::kBadGateway);
    }

    return response.ExtractValue();
}

auto DebugSend::GetStaticConfigSchema() -> userver::yaml_config::Schema {
    return userver::yaml_config::MergeSchemas<BaseType>(R"(
type: object
description: Debug handler for sending FCM push notifications
additionalProperties: false
properties:
    fcm-client:
        type: string
        description: Component name for the FCM client
        defaultDescription: fcm-client
    )");
}

}  // namespace fcm::handlers
