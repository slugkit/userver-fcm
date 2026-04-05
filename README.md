# userver-fcm

A userver component for sending push notifications via Google Firebase Cloud
Messaging (FCM) HTTP v1 API. Designed to be plugged into any userver-based
project as a library.

## Overview

The library provides:

- **`fcm::Client`** — a userver component that handles FCM OAuth2
  authentication and push delivery.
- **`fcm::Credentials`** — credential set that can be loaded from component
  config or constructed programmatically (e.g. from a database for
  multi-tenant setups).
- **`fcm::handlers::DebugSend`** — an optional HTTP handler for sending
  test pushes using the component's default credentials.

For Firebase project setup, service account configuration, and deployment
see [SETUP.md](SETUP.md).

## Firebase Cloud Messaging HTTP v1 API

### API

Every push is a single HTTP POST:

```
POST https://fcm.googleapis.com/v1/projects/{project_id}/messages:send
```

**Required headers:**

| Header | Description |
|---|---|
| `Authorization` | `Bearer <oauth2_access_token>` |
| `Content-Type` | `application/json` |

**Request body:**

```json
{
    "message": {
        "token": "device_registration_token",
        "notification": {
            "title": "Title",
            "body": "Message body"
        },
        "data": {
            "key1": "value1"
        }
    }
}
```

The `message` object supports additional platform-specific fields:
`android`, `apns`, `webpush`. It also supports `topic` and `condition`
targeting instead of a device `token`. The full schema is documented in
[FCM REST API reference](https://firebase.google.com/docs/reference/fcm/rest/v1/projects.messages).

**Payload limit:** 4096 bytes.

**Success response** (HTTP 200):

```json
{
    "name": "projects/my-project/messages/0:1500415314455276%31bd1c9631bd1c96"
}
```

**Error response** (HTTP 400-503):

```json
{
    "error": {
        "code": 400,
        "status": "INVALID_ARGUMENT",
        "message": "The registration token is not a valid FCM registration token"
    }
}
```

**Error codes:**

| Status | Code | Meaning |
|---|---|---|
| 400 | `INVALID_ARGUMENT` | Malformed request or invalid registration token |
| 401 | `UNAUTHENTICATED` | Invalid or expired OAuth2 token |
| 403 | `PERMISSION_DENIED` | Sender ID mismatch or missing permissions |
| 404 | `UNREGISTERED` | Token no longer valid — remove from device list |
| 429 | `RESOURCE_EXHAUSTED` | Rate limit exceeded — retry with backoff |
| 500 | `INTERNAL` | FCM server error — retry with backoff |
| 503 | `UNAVAILABLE` | FCM overloaded — retry with backoff |

### Authentication

FCM HTTP v1 uses **OAuth2 with service account credentials**:

1. Build a JWT signed with **RS256** using the service account's RSA private
   key. Claims include `iss` (client email), `scope`
   (`https://www.googleapis.com/auth/firebase.messaging`), `aud`
   (Google token endpoint), `iat`, and `exp`.
2. Exchange the JWT for an access token via POST to
   `https://oauth2.googleapis.com/token`.
3. Use the access token as `Authorization: Bearer <token>` on push requests.

Access tokens are valid for approximately **1 hour** (3600 seconds). The
component refreshes them automatically in a background task, scheduled to
run before expiry (configurable margin, default 5 minutes before).

**Credentials required:**

1. **Project ID** — Firebase project identifier.
2. **Client Email** — service account email address.
3. **Private Key** — RSA private key from the service account JSON file.

## Component Design

### `fcm::Credentials`

```cpp
namespace fcm {

struct Credentials {
    std::string project_id;
    std::string client_email;
    std::string private_key_pem;

    static auto FromConfig(const userver::components::ComponentConfig& config)
        -> Credentials;
};

}  // namespace fcm
```

### `fcm::Client`

A `userver::components::ComponentBase` that owns the default credential set,
manages OAuth2 token refresh in a background periodic task, and exposes
`Send()`.

**Dependencies:**

- `userver::components::HttpClient` — used for both the OAuth2 token
  exchange and the FCM push requests.

```cpp
namespace fcm {

struct Notification {
    std::string device_token;
    std::string payload;        // JSON string — the contents of the "message" object
};

struct SendResult {
    std::int32_t status_code;   // 200 = success, 400-503 = FCM error, 0 = transport error
    std::string message_name;   // "projects/.../messages/..." on success
    std::string error_code;     // "UNREGISTERED", "INVALID_ARGUMENT", etc.
    std::string error_message;  // Human-readable error description
};

class Client final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "fcm-client";

    Client(const ComponentConfig&, const ComponentContext&);
    ~Client();

    static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

    /// Send using the component's default credentials.
    /// Uses a cached OAuth2 token refreshed in the background.
    [[nodiscard]] auto Send(const Notification& notification) const -> SendResult;

    /// Send using explicit credentials (for multi-tenant setups).
    /// Obtains a fresh OAuth2 token per call.
    [[nodiscard]] auto Send(
        const Credentials& credentials,
        const Notification& notification
    ) const -> SendResult;

private:
    struct Impl;
    userver::utils::FastPimpl<Impl, kImplSize, kImplAlign> impl_;
};

}  // namespace fcm
```

The `payload` field contains the raw JSON for the `message` object's content
(notification, data, android, etc.). The component wraps it in the
`{"message": {..., "token": "..."}}` envelope automatically — the caller
provides the notification content, the component adds the device token and
envelope.

### `fcm::handlers::DebugSend`

An optional HTTP handler for testing pushes. Uses the component's default
credentials — no credentials are accepted via the HTTP request.

**Request** — `POST` with JSON body:

```json
{
    "device_token": "registration_token",
    "payload": {
        "notification": {
            "title": "Test",
            "body": "Hello from debug handler"
        }
    }
}
```

**Response:**

```json
{
    "status_code": 200,
    "message_name": "projects/my-project/messages/...",
    "error_code": "",
    "error_message": ""
}
```

**Static config:**

```yaml
fcm-handler-debug-send:
    path: /fcm/debug/send
    method: POST
    task_processor: main-task-processor
    fcm-client: fcm-client
```

### Internal Architecture

```
Client
├── Impl
│   ├── http_client_       → userver HttpClient
│   ├── credentials_       → default Credentials
│   ├── fcm_url_           → https://fcm.googleapis.com/v1/projects/{id}/messages:send
│   ├── request_timeout_   → configurable (default: 10s)
│   ├── refresh_margin_    → configurable (default: 5m)
│   ├── access_token       → rcu::Variable<string> (lock-free reads)
│   └── refresh_task       → PeriodicTask (background token refresh)
│
├── Send(notification)                       → reads cached token via RCU
│   ├── access_token.Read()
│   ├── POST .../messages:send
│   └── Parse response
│
├── Send(credentials, notification)          → fresh token per call
│   ├── ObtainAccessToken()
│   ├── POST .../messages:send
│   └── Parse response
│
└── RefreshToken()                           → called by PeriodicTask
    ├── ObtainAccessToken() from Google
    ├── access_token.Assign(new_token)
    └── Reschedule refresh_task (expires_in - margin)
```

**Token management:**

- The access token is stored in an `rcu::Variable<std::string>` for lock-free
  reads from concurrent `Send()` calls.
- A `PeriodicTask` runs `RefreshToken()` which obtains a new token from
  Google's OAuth2 endpoint and updates the RCU variable.
- The refresh interval is derived from the token's `expires_in` minus the
  configured margin (default 5 minutes). If Google returns a 3600s token,
  the refresh happens at ~3300s.
- The initial token is obtained synchronously in the constructor. If the
  exchange fails (bad credentials, network), the component fails to start.
- After each successful refresh, the periodic task is restarted with the
  new interval based on the fresh token's `expires_in`.

### Configuration

All credentials are passed via environment variables:

```yaml
fcm-client:
    project-id: ""
    project-id#env: FCM_PROJECT_ID
    client-email: ""
    client-email#env: FCM_CLIENT_EMAIL
    private-key-pem: ""
    private-key-pem#env: FCM_PRIVATE_KEY_PEM
    token-refresh-margin: 5m
    request-timeout: 10s
```

The `private-key-pem` accepts the raw PEM content of the RSA private key
from the service account JSON file (the `private_key` field, which starts
with `-----BEGIN PRIVATE KEY-----`).

### Service Integration

**CMakeLists.txt:**

```cmake
add_subdirectory(third-party/userver-fcm/fcm)
target_link_libraries(your-service PRIVATE fcm_client)
```

**main.cpp:**

```cpp
#include <fcm/components/client.hpp>
#include <fcm/handlers/debug_send.hpp>      // optional

auto component_list = userver::components::MinimalServerComponentList()
    .Append<userver::components::HttpClient>()
    .Append<userver::clients::dns::Component>()
    .Append<fcm::Client>()
    .Append<fcm::handlers::DebugSend>()            // optional
    ;
```

**Sending with default credentials:**

```cpp
auto& client = context.FindComponent<fcm::Client>();

fcm::Notification notification;
notification.device_token = "registration_token";
notification.payload = R"({"notification":{"title":"Hi","body":"Hello"}})";

auto result = client.Send(notification);
```

**Sending with per-tenant credentials:**

```cpp
fcm::Credentials creds;
creds.project_id = tenant.fcm_project_id;
creds.client_email = tenant.fcm_client_email;
creds.private_key_pem = tenant.fcm_private_key;

auto result = client.Send(creds, notification);
```

## Library Structure

```
fcm/
├── CMakeLists.txt
├── include/fcm/
│   ├── components/
│   │   └── client.hpp
│   ├── handlers/
│   │   └── debug_send.hpp
│   └── types/
│       ├── credentials.hpp
│       ├── notification.hpp
│       └── result.hpp
├── src/fcm/
│   ├── components/
│   │   └── client.cpp
│   ├── handlers/
│   │   └── debug_send.cpp
│   └── oauth/
│       ├── token.hpp
│       └── token.cpp
└── tests/
    ├── oauth_test.cpp
    └── notification_test.cpp
```

## Dependencies

- **userver** (core, universal) — HTTP client, crypto (RS256), RCU, periodic
  tasks, JSON, logging
- **No external dependencies** — OAuth2 token exchange uses userver's HTTP
  client, JWT signing uses userver's built-in RSA signer

## License

Apache License 2.0
