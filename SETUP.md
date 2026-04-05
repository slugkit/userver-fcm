# FCM Setup Guide

Complete guide to setting up Firebase Cloud Messaging, obtaining service
account credentials, and deploying to production.

## 1. Create a Firebase Project

1. Go to the [Firebase Console](https://console.firebase.google.com/).
2. Click **Add project** (or select an existing project).
3. Enter a project name and follow the setup wizard.
4. Note the **Project ID** shown in project settings — this is your
   `FCM_PROJECT_ID`.

## 2. Enable the FCM API

The FCM API is enabled by default for Firebase projects. To verify:

1. Go to the [Google Cloud Console](https://console.cloud.google.com/).
2. Select your Firebase project.
3. Navigate to **APIs & Services** → **Enabled APIs**.
4. Confirm **Firebase Cloud Messaging API** is listed and enabled.
5. If not, click **Enable APIs and Services**, search for
   "Firebase Cloud Messaging API", and enable it.

## 3. Create a Service Account Key

1. In the Firebase Console, go to **Project Settings** (gear icon) →
   **Service accounts**.
2. Click **Generate new private key**.
3. Confirm by clicking **Generate key**.
4. A JSON file downloads. It contains:

```json
{
    "type": "service_account",
    "project_id": "your-project-id",
    "private_key_id": "key-id",
    "private_key": "-----BEGIN PRIVATE KEY-----\nMIIEvQ...\n-----END PRIVATE KEY-----\n",
    "client_email": "firebase-adminsdk-xxxxx@your-project-id.iam.gserviceaccount.com",
    "client_id": "123456789",
    "auth_uri": "https://accounts.google.com/o/oauth2/auth",
    "token_uri": "https://oauth2.googleapis.com/token",
    ...
}
```

The three values you need are:

| Field | JSON key | Env Var |
|---|---|---|
| Project ID | `project_id` | `FCM_PROJECT_ID` |
| Client Email | `client_email` | `FCM_CLIENT_EMAIL` |
| Private Key | `private_key` | `FCM_PRIVATE_KEY_PEM` |

**Keep this file secure.** Anyone with the private key can send pushes to
your app's users.

## 4. Add Your App to Firebase

If you haven't already:

1. In the Firebase Console, click **Add app** and select your platform
   (Android, iOS, Web).
2. Follow the setup wizard to register your app.
3. For Android: download `google-services.json` and add it to your app.
4. For iOS: download `GoogleService-Info.plist` and add it to your Xcode
   project.

The app must integrate the Firebase SDK to obtain device registration tokens.

## 5. Summary of Credentials

| Value | Example | Env Var |
|---|---|---|
| Project ID | `my-firebase-project` | `FCM_PROJECT_ID` |
| Client Email | `firebase-adminsdk-xxxxx@my-project.iam.gserviceaccount.com` | `FCM_CLIENT_EMAIL` |
| Private Key PEM | `-----BEGIN PRIVATE KEY-----\nMIIEvQ...` | `FCM_PRIVATE_KEY_PEM` |

## 6. Environment Variables

All credentials are passed via environment variables. **Never bake
credentials into Docker images, config files checked into git, or build
artifacts.**

### Required

| Variable | Description |
|---|---|
| `FCM_PROJECT_ID` | Firebase project identifier |
| `FCM_CLIENT_EMAIL` | Service account email from the JSON key file |
| `FCM_PRIVATE_KEY_PEM` | Full PEM content of the RSA private key from the JSON key file, including `-----BEGIN/END PRIVATE KEY-----` lines |

### Extracting the private key from the JSON file

The `private_key` field in the service account JSON contains escaped
newlines (`\n`). Most secret management systems handle this natively. To
extract it manually:

```bash
# Using jq (preserves real newlines)
export FCM_PRIVATE_KEY_PEM="$(jq -r '.private_key' service-account.json)"

# Verify it looks right
echo "$FCM_PRIVATE_KEY_PEM"
# Should print:
# -----BEGIN PRIVATE KEY-----
# MIIEvQ...
# -----END PRIVATE KEY-----
```

Also extract the other values:

```bash
export FCM_PROJECT_ID="$(jq -r '.project_id' service-account.json)"
export FCM_CLIENT_EMAIL="$(jq -r '.client_email' service-account.json)"
```

## 7. Service Configuration

Add to your service's `static_config.yaml`:

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

And in `config_vars.yaml`:

```yaml
# No FCM-specific config vars needed — all credentials come from env vars.
```

## 8. Local Development

Create a `.env` file in the project root (already gitignored):

```bash
# .env — local development only, never commit this file
FCM_PROJECT_ID=my-firebase-project
FCM_CLIENT_EMAIL=firebase-adminsdk-xxxxx@my-project.iam.gserviceaccount.com
FCM_PRIVATE_KEY_PEM="-----BEGIN PRIVATE KEY-----
MIIEvQ...
-----END PRIVATE KEY-----"
```

Then `source .env` before running the service, or pass it via docker compose:

```yaml
services:
  your-service:
    env_file:
      - .env
```

## 9. Production Deployment

### fly.io

```bash
fly secrets set FCM_PROJECT_ID="my-firebase-project"
fly secrets set FCM_CLIENT_EMAIL="firebase-adminsdk-xxxxx@my-project.iam.gserviceaccount.com"
fly secrets set FCM_PRIVATE_KEY_PEM="$(jq -r '.private_key' service-account.json)"
```

### Docker Compose (production)

```yaml
services:
  api:
    image: your-registry/pokeme-api:latest
    env_file:
      - /run/secrets/fcm.env
```

### Kubernetes

```bash
kubectl create secret generic fcm-credentials \
    --from-literal=FCM_PROJECT_ID=my-firebase-project \
    --from-literal=FCM_CLIENT_EMAIL=firebase-adminsdk-xxxxx@my-project.iam.gserviceaccount.com \
    --from-file=FCM_PRIVATE_KEY_PEM=<(jq -r '.private_key' service-account.json)
```

```yaml
containers:
  - name: api
    envFrom:
      - secretRef:
          name: fcm-credentials
```

### AWS ECS

```json
{
    "containerDefinitions": [{
        "secrets": [
            { "name": "FCM_PROJECT_ID", "valueFrom": "arn:aws:ssm:region:account:parameter/fcm/project-id" },
            { "name": "FCM_CLIENT_EMAIL", "valueFrom": "arn:aws:ssm:region:account:parameter/fcm/client-email" },
            { "name": "FCM_PRIVATE_KEY_PEM", "valueFrom": "arn:aws:secretsmanager:region:account:secret:fcm-key" }
        ]
    }]
}
```

### Google Cloud (GKE, Cloud Run)

On Google Cloud, you can use **Workload Identity** instead of a downloaded
key file. The component still needs the project ID and client email, but
the token exchange happens automatically via the metadata server. This is
not yet supported by this library — use a service account key for now.

## 10. Key Rotation

Service account keys do not expire automatically, but you should rotate them
periodically:

1. In Firebase Console → Project Settings → Service accounts, generate a
   new private key.
2. Update `FCM_PRIVATE_KEY_PEM` and optionally `FCM_CLIENT_EMAIL` in your
   secrets store.
3. Deploy. The component picks up new env vars on restart and obtains a
   fresh OAuth2 token.
4. Delete the old key in Google Cloud Console → IAM & Admin → Service
   Accounts → Keys.

## 11. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| HTTP 401 `UNAUTHENTICATED` | Expired or invalid OAuth2 token | Component auto-refreshes; if persistent, verify credentials |
| HTTP 403 `PERMISSION_DENIED` | Service account lacks FCM permissions | Grant "Firebase Cloud Messaging API Admin" role |
| HTTP 404 `UNREGISTERED` | Device token is no longer valid | Remove the token from your device registry |
| HTTP 429 `RESOURCE_EXHAUSTED` | FCM rate limit exceeded | Implement backoff; check quota in Cloud Console |
| HTTP 400 `INVALID_ARGUMENT` | Malformed request or bad device token | Validate payload structure and token format |
| `status_code = 0` in `SendResult` | Network/TLS/timeout error | Check connectivity to `fcm.googleapis.com:443` |
| Startup crash: `project-id is not configured` | `FCM_PROJECT_ID` env var missing | Check secrets config |
| Startup crash: `invalid private-key-pem` | Not a valid PKCS#8 RSA PEM key | Verify extracted key starts with `-----BEGIN PRIVATE KEY-----` |
| Startup crash: `OAuth2 token exchange failed` | Bad credentials or network error | Verify all three credential values match the service account JSON |
