cmake_minimum_required(VERSION 3.12...3.31)
project(fcm_client CXX)

set(FCM_SRC
    include/fcm/types/credentials.hpp
    include/fcm/types/notification.hpp
    include/fcm/types/result.hpp

    include/fcm/components/client.hpp

    include/fcm/handlers/debug_send.hpp

    src/fcm/types/credentials.cpp

    src/fcm/oauth/token.hpp
    src/fcm/oauth/token.cpp

    src/fcm/components/client.cpp

    src/fcm/handlers/debug_send.cpp
)

add_library(fcm_client OBJECT ${FCM_SRC})
target_link_libraries(fcm_client PUBLIC userver::core)
target_include_directories(fcm_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(fcm_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(FCM_BUILD_TESTS)
    add_executable(fcm_unittest
        tests/oauth_test.cpp
        tests/notification_test.cpp
    )
    target_link_libraries(fcm_unittest PRIVATE fcm_client userver::utest)
    target_include_directories(fcm_unittest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_google_tests(fcm_unittest)
endif()
