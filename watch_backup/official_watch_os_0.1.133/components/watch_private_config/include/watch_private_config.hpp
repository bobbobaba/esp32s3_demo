#pragma once

// Public, safe defaults. Do not put real server URLs, IPs, passwords, tokens or
// private key paths in this file.
//
// For local builds, create this ignored file next to the example:
//   components/watch_private_config/include/watch_private_config_private.hpp
//
// The private file may define:
//   WATCH_PRIVATE_SERVER_BASE_URL
//   WATCH_PRIVATE_SERVER_HOST
//   WATCH_PRIVATE_USERNAME
//   WATCH_PRIVATE_PASSWORD

#if __has_include("watch_private_config_private.hpp")
#   include "watch_private_config_private.hpp"
#endif

#ifndef WATCH_PRIVATE_SERVER_BASE_URL
#   define WATCH_PRIVATE_SERVER_BASE_URL "http://<OTA_SERVER>"
#endif

#ifndef WATCH_PRIVATE_SERVER_HOST
#   define WATCH_PRIVATE_SERVER_HOST "<OTA_SERVER>"
#endif

#ifndef WATCH_PRIVATE_USERNAME
#   define WATCH_PRIVATE_USERNAME "admin"
#endif

#ifndef WATCH_PRIVATE_PASSWORD
#   define WATCH_PRIVATE_PASSWORD "<OTA_PASSWORD>"
#endif

#ifndef WATCH_PRIVATE_DUDUSERVER_BASE_URL
#   define WATCH_PRIVATE_DUDUSERVER_BASE_URL WATCH_PRIVATE_SERVER_BASE_URL "/DUDUSERVER"
#endif

#ifndef WATCH_PRIVATE_DUDUSERVER_API_BASE_URL
#   define WATCH_PRIVATE_DUDUSERVER_API_BASE_URL WATCH_PRIVATE_DUDUSERVER_BASE_URL "/api/v1"
#endif

#ifndef WATCH_PRIVATE_LOGIN_BODY
#   define WATCH_PRIVATE_LOGIN_BODY "{\"username\":\"" WATCH_PRIVATE_USERNAME "\",\"password\":\"" WATCH_PRIVATE_PASSWORD "\"}"
#endif
