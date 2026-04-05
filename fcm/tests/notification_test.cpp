#include <fcm/types/credentials.hpp>
#include <fcm/types/notification.hpp>
#include <fcm/types/result.hpp>

#include <userver/utest/utest.hpp>

UTEST(FcmNotification, DefaultValues) {
    fcm::Notification n;
    EXPECT_TRUE(n.device_token.empty());
    EXPECT_TRUE(n.payload.empty());
}

UTEST(FcmCredentials, DefaultValues) {
    fcm::Credentials c;
    EXPECT_TRUE(c.project_id.empty());
    EXPECT_TRUE(c.client_email.empty());
    EXPECT_TRUE(c.private_key_pem.empty());
}

UTEST(FcmSendResult, Fields) {
    fcm::SendResult r{
        .status_code = 200,
        .message_name = "projects/test/messages/123",
        .error_code = {},
        .error_message = {},
    };
    EXPECT_EQ(r.status_code, 200);
    EXPECT_EQ(r.message_name, "projects/test/messages/123");
    EXPECT_TRUE(r.error_code.empty());
    EXPECT_TRUE(r.error_message.empty());
}
