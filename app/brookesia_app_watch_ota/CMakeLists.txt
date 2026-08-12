set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENT_SRC_DIR ${COMPONENT_DIR}/src)
set(COMPONENT_INCLUDE_DIRS ${COMPONENT_DIR}/include)
set(COMPONENT_PRIVATE_INCLUDE_DIRS ${COMPONENT_SRC_DIR})
set(COMPONENT_REQUIRES
    brookesia_system_core
    esp_http_client
    app_update
    esp_wifi
    esp_timer
    esp_system
)
set(COMPONENT_SRCS_CPP "")
file(GLOB_RECURSE APP_WATCH_OTA_SRCS_CPP ${COMPONENT_SRC_DIR}/*.cpp)
list(APPEND COMPONENT_SRCS_CPP ${APP_WATCH_OTA_SRCS_CPP})

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_platform.cmake)
else()
    message(FATAL_ERROR "brookesia_app_watch_ota currently supports ESP-IDF builds only")
endif()
