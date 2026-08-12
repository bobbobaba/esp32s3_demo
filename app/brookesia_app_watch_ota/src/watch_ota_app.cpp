#include "brookesia/app_watch_ota.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boost/json.hpp"
#include "boost/system/error_code.hpp"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esp_brookesia::app::watch_ota {
namespace {

static constexpr const char *TAG = BROOKESIA_APP_WATCH_OTA_LOG_TAG;
static constexpr const char *APP_ID = "bo.watch.ota";
static constexpr const char *APP_NAME = "OTA Update 0.1.21";
static constexpr const char *APP_NAME_ZH_CN = "系统更新 0.1.21";
static constexpr const char *APP_ICON_ID = "launcher_icon";
static constexpr const char *APP_ICON_PATH = "res/images/index.json";
static constexpr const char *GUI_ROOT = "res/root.json";
static constexpr const char *FLOW_ID = "watch_ota";
static constexpr const char *LOCALE_EN = "en";
static constexpr const char *LOCALE_ZH_CN = "zh_CN";

static constexpr const char *PROJECT_ID = "esp32-s3-watch";
static constexpr const char *HW_MODEL = "waveshare-esp32-s3-touch-amoled-2.06";
static constexpr const char *LOGIN_URL = "<PRIVATE_OTA_BASE_URL>/api/v1/auth/login";
static constexpr const char *DEVICE_CONFIG_URL = "<PRIVATE_OTA_BASE_URL>/api/v1/device-config";
static constexpr const char *DEVICE_STATUS_URL = "<PRIVATE_OTA_BASE_URL>/api/v1/device-status";
static constexpr const char *USERNAME = "<PRIVATE_USER>";
static constexpr const char *PASSWORD = "<PRIVATE_PASSWORD>";
static constexpr const char *USER_AGENT = "esp32-s3-watch-ota";

static constexpr int HTTP_TIMEOUT_MS = 15000;
static constexpr size_t MAX_JSON_RESPONSE_BYTES = 32 * 1024;
static constexpr size_t OTA_BUFFER_BYTES = 8 * 1024;
static constexpr size_t APP_DESC_OFFSET = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
static constexpr size_t APP_DESC_HEADER_BYTES = APP_DESC_OFFSET + sizeof(esp_app_desc_t);
static constexpr int OTA_TASK_STACK_SIZE = 24 * 1024;
static constexpr UBaseType_t OTA_TASK_PRIORITY = 4;
static constexpr int OTA_STATUS_REPORT_PERCENT_STEP = 5;
static constexpr int64_t OTA_STATUS_REPORT_INTERVAL_MS = 10000;
static constexpr uint8_t ENABLED_OPACITY = 255;
static constexpr uint8_t DISABLED_OPACITY = 110;
static constexpr uint8_t ESP_APP_IMAGE_MAGIC = 0xE9;

static constexpr const char *STATUS_PATH = "/ota/page/header/status";
static constexpr const char *CURRENT_VERSION_PATH = "/ota/page/info/current/value";
static constexpr const char *REMOTE_VERSION_PATH = "/ota/page/info/remote/value";
static constexpr const char *LOG_PATH = "/ota/page/info/log";
static constexpr const char *PARTITION_PATH = "/ota/page/info/partition/value";
static constexpr const char *DOWNLOAD_LABEL_PATH = "/ota/page/progress/download_label";
static constexpr const char *DOWNLOAD_BAR_PATH = "/ota/page/progress/download_bar";
static constexpr const char *WRITE_LABEL_PATH = "/ota/page/progress/write_label";
static constexpr const char *WRITE_BAR_PATH = "/ota/page/progress/write_bar";
static constexpr const char *CHECK_BUTTON_PATH = "/ota/page/actions/check";
static constexpr const char *CONFIRM_BUTTON_PATH = "/ota/page/actions/confirm";
static constexpr const char *ACTION_CHECK = "watch_ota.check";
static constexpr const char *ACTION_CONFIRM = "watch_ota.confirm";

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

struct OtaConfig {
    bool ota_enabled = false;
    std::string firmware_version;
    std::string firmware_url;
    std::string rollback_firmware_version;
    std::string ota_log;
};

struct TaskRequest {
    WatchOtaApp *app = nullptr;
    WatchOtaApp::Operation operation = WatchOtaApp::Operation::Check;
};

void add_binding(
    std::vector<gui::BindingValueUpdate> &updates,
    std::string absolute_path,
    std::string key,
    std::string value
)
{
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = std::move(absolute_path),
        .key = std::move(key),
        .value = std::move(value),
    });
}

std::string bool_value(bool value)
{
    return value ? "true" : "false";
}

std::string int_value(int value)
{
    return std::to_string(value);
}

void add_button_state_bindings(
    std::vector<gui::BindingValueUpdate> &updates,
    bool check_enabled,
    bool confirm_enabled
)
{
    add_binding(updates, CHECK_BUTTON_PATH, "disabled", bool_value(!check_enabled));
    add_binding(updates, CHECK_BUTTON_PATH, "opacity", int_value(check_enabled ? ENABLED_OPACITY : DISABLED_OPACITY));
    add_binding(updates, CONFIRM_BUTTON_PATH, "disabled", bool_value(!confirm_enabled));
    add_binding(updates, CONFIRM_BUTTON_PATH, "opacity", int_value(confirm_enabled ? ENABLED_OPACITY : DISABLED_OPACITY));
}

std::string current_firmware_version()
{
    const auto *app = esp_app_get_description();
    return (app != nullptr && app->version[0] != '\0') ? app->version : "unknown";
}

std::string json_escape(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8] = {};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
                out += escaped;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

std::string device_id()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "watch-unknown";
    }
    char id[32] = {};
    std::snprintf(
        id, sizeof(id), "watch-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    return id;
}

const char *reset_reason_string()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

bool wifi_connected(int *rssi = nullptr)
{
    wifi_ap_record_t ap_info = {};
    const bool connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (connected && rssi != nullptr) {
        *rssi = ap_info.rssi;
    }
    return connected;
}

std::string running_partition_label()
{
    const esp_partition_t *partition = esp_ota_get_running_partition();
    return partition != nullptr ? partition->label : "unknown";
}

std::vector<int> parse_version_numbers(std::string_view version)
{
    std::vector<int> numbers;
    int current = 0;
    bool in_number = false;
    for (char c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current = current * 10 + (c - '0');
            in_number = true;
        } else if (in_number) {
            numbers.push_back(current);
            current = 0;
            in_number = false;
        }
    }
    if (in_number) {
        numbers.push_back(current);
    }
    return numbers;
}

bool version_is_newer(std::string_view remote, std::string_view local)
{
    auto r = parse_version_numbers(remote);
    auto l = parse_version_numbers(local);
    const size_t count = std::max(r.size(), l.size());
    r.resize(count);
    l.resize(count);
    for (size_t i = 0; i < count; ++i) {
        if (r[i] > l[i]) {
            return true;
        }
        if (r[i] < l[i]) {
            return false;
        }
    }
    return false;
}

bool firmware_url_supported(std::string_view url)
{
    return url.starts_with("http://");
}

esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    auto *body = static_cast<std::string *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body != nullptr && event->data != nullptr && event->data_len > 0) {
        if (body->size() + static_cast<size_t>(event->data_len) > MAX_JSON_RESPONSE_BYTES) {
            ESP_LOGW(TAG, "HTTP JSON response too large");
            return ESP_FAIL;
        }
        body->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

bool http_request(
    esp_http_client_method_t method,
    const char *url,
    const std::string &token,
    const std::string &body,
    HttpResponse &response
)
{
    response = {};

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event_handler;
    config.user_data = &response.body;
    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "HTTP init failed: %s", url);
        return false;
    }

    esp_http_client_set_method(client, method);
    if (!token.empty()) {
        const std::string authorization = "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }
    if (!body.empty()) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));
    }

    const esp_err_t err = esp_http_client_perform(client);
    response.status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s, url=%s", esp_err_to_name(err), url);
        return false;
    }
    return response.status_code >= 200 && response.status_code < 300;
}

std::optional<std::string> json_string(const boost::json::object &object, std::string_view key)
{
    auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        return std::nullopt;
    }
    return std::string(it->value().as_string().c_str());
}

bool json_bool(const boost::json::object &object, std::string_view key)
{
    auto it = object.find(key);
    return it != object.end() && it->value().is_bool() && it->value().as_bool();
}

bool parse_login_token(const std::string &body, std::string &token)
{
    boost::system::error_code ec;
    const auto value = boost::json::parse(body, ec);
    if (ec || !value.is_object()) {
        return false;
    }
    auto parsed = json_string(value.as_object(), "access_token");
    if (!parsed || parsed->empty()) {
        return false;
    }
    token = std::move(*parsed);
    return true;
}

bool parse_device_config(const std::string &body, OtaConfig &config)
{
    boost::system::error_code ec;
    const auto value = boost::json::parse(body, ec);
    if (ec || !value.is_object()) {
        return false;
    }
    const auto &object = value.as_object();
    config.ota_enabled = json_bool(object, "ota_enabled");
    if (auto field = json_string(object, "firmware_version"); field) {
        config.firmware_version = std::move(*field);
    }
    if (auto field = json_string(object, "firmware_url"); field) {
        config.firmware_url = std::move(*field);
    }
    if (auto field = json_string(object, "rollback_firmware_version"); field) {
        config.rollback_firmware_version = std::move(*field);
    }
    if (auto field = json_string(object, "ota_log"); field) {
        config.ota_log = std::move(*field);
    }
    return true;
}

bool login(std::string &token)
{
    const std::string body = std::string("{\"username\":\"") + json_escape(USERNAME) +
                             "\",\"password\":\"" + json_escape(PASSWORD) + "\"}";
    HttpResponse response;
    if (!http_request(HTTP_METHOD_POST, LOGIN_URL, "", body, response)) {
        ESP_LOGW(TAG, "Login failed, status=%d", response.status_code);
        return false;
    }
    if (!parse_login_token(response.body, token)) {
        ESP_LOGW(TAG, "Login response has no access_token");
        return false;
    }
    return true;
}

bool fetch_device_config(const std::string &token, OtaConfig &config, int &status_code)
{
    HttpResponse response;
    const bool ok = http_request(HTTP_METHOD_GET, DEVICE_CONFIG_URL, token, "", response);
    status_code = response.status_code;
    if (!ok) {
        return false;
    }
    return parse_device_config(response.body, config);
}

bool report_status(
    const std::string &token,
    std::string_view ota_state,
    std::string_view ota_target,
    std::string_view ota_partition = {},
    int ota_progress = -1,
    int64_t ota_bytes_received = -1,
    int64_t ota_bytes_total = -1
)
{
    if (token.empty()) {
        return false;
    }

    int rssi = 0;
    const bool connected = wifi_connected(&rssi);
    std::string body = "{";
    body += "\"project\":\"" + std::string(PROJECT_ID) + "\",";
    body += "\"device_id\":\"" + json_escape(device_id()) + "\",";
    body += "\"hw_model\":\"" + std::string(HW_MODEL) + "\",";
    body += "\"firmware\":\"" + json_escape(current_firmware_version()) + "\",";
    body += "\"firmware_version\":\"" + json_escape(current_firmware_version()) + "\",";
    body += "\"ota_state\":\"" + json_escape(ota_state) + "\",";
    body += "\"ota_target\":\"" + json_escape(ota_target) + "\",";
    body += "\"ota_partition\":\"" + json_escape(ota_partition.empty() ? running_partition_label() : ota_partition) + "\",";
    if (ota_progress >= 0) {
        body += "\"ota_progress\":" + std::to_string(std::clamp(ota_progress, 0, 100)) + ",";
    }
    if (ota_bytes_received >= 0) {
        body += "\"ota_bytes_received\":" + std::to_string(ota_bytes_received) + ",";
    }
    if (ota_bytes_total >= 0) {
        body += "\"ota_bytes_total\":" + std::to_string(ota_bytes_total) + ",";
    }
    body += "\"rssi\":" + std::to_string(connected ? rssi : 0) + ",";
    body += "\"uptime_ms\":" + std::to_string(static_cast<uint64_t>(esp_timer_get_time() / 1000)) + ",";
    body += "\"heap_free_kb\":" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) + ",";
    body += "\"reset_reason\":\"" + std::string(reset_reason_string()) + "\",";
    body += "\"wifi_connected\":";
    body += connected ? "true" : "false";
    body += "}";

    HttpResponse response;
    const bool ok = http_request(HTTP_METHOD_PUT, DEVICE_STATUS_URL, token, body, response);
    if (!ok) {
        ESP_LOGW(TAG, "Status report failed, status=%d", response.status_code);
    }
    return ok;
}

std::string partition_label(const esp_partition_t *partition)
{
    return partition != nullptr ? partition->label : "unknown";
}

std::string make_partition_summary()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    return std::string("当前 ") + partition_label(running) + " / 更新写入 " + partition_label(next);
}

std::string format_transfer_summary(int64_t total_read, int64_t content_length)
{
    std::string summary = "已收 " + std::to_string(total_read) + "B";
    if (content_length > 0) {
        summary += "/" + std::to_string(content_length) + "B";
    } else {
        summary += "/未知长度";
    }
    return summary;
}

bool try_parse_app_desc_from_header(
    const std::array<uint8_t, APP_DESC_HEADER_BYTES> &header,
    size_t header_bytes,
    esp_app_desc_t &app_desc
)
{
    if (header_bytes < APP_DESC_HEADER_BYTES) {
        return false;
    }
    std::memcpy(&app_desc, header.data() + APP_DESC_OFFSET, sizeof(app_desc));
    return app_desc.magic_word == ESP_APP_DESC_MAGIC_WORD;
}

} // namespace

struct WatchOtaApp::Impl {
    std::mutex mutex;
    system::core::AppContext *context = nullptr;
    OtaConfig latest_config;
    std::string token;
    bool update_available = false;
    bool operation_running = false;
};

WatchOtaApp::WatchOtaApp()
    : impl_(std::make_unique<Impl>())
{
}

WatchOtaApp::~WatchOtaApp() = default;

system::core::AppManifest WatchOtaApp::get_manifest() const
{
    return {
        .id = APP_ID,
        .name = APP_NAME,
        .localized_names = {
            {LOCALE_EN, APP_NAME},
            {LOCALE_ZH_CN, APP_NAME_ZH_CN},
        },
        .version = "0.1.0",
        .kind = system::core::AppKind::Native,
        .visible = true,
        .preload_dom = false,
        .icon_id = APP_ICON_ID,
        .supported_systems = {},
        .icon_path = APP_ICON_PATH,
        .runtime_type = runtime::BackendType::Unknown,
        .app_path = "",
        .entry = "",
        .resource_dir = BROOKESIA_APP_WATCH_OTA_RESOURCE_DIR,
        .arguments = {},
    };
}

system::core::AppGuiDescriptor WatchOtaApp::get_gui_descriptor() const
{
    return {
        .root_kind = system::core::GuiRootKind::File,
        .root = GUI_ROOT,
        .resources = {},
        .screen_flows = {
            system::core::GuiScreenFlowEntry{
                .screen_flow = FLOW_ID,
                .layer = system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> WatchOtaApp::on_start(system::core::AppContext &context)
{
    ESP_LOGI(TAG, "OTA app start");
    {
        std::lock_guard lock(impl_->mutex);
        impl_->context = &context;
    }

    auto result = subscribe_actions(context);
    if (!result) {
        return result;
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding(updates, STATUS_PATH, "status", "点击检测更新；联网请先进入设置");
    add_binding(updates, CURRENT_VERSION_PATH, "value", current_firmware_version());
    add_binding(updates, REMOTE_VERSION_PATH, "value", "--");
    add_binding(updates, PARTITION_PATH, "value", make_partition_summary());
    add_binding(updates, LOG_PATH, "log", std::string("服务器：") + DEVICE_CONFIG_URL);
    add_binding(updates, DOWNLOAD_LABEL_PATH, "text", "下载进度 0%");
    add_binding(updates, DOWNLOAD_BAR_PATH, "value", "0");
    add_binding(updates, WRITE_LABEL_PATH, "text", "写入进度 0%");
    add_binding(updates, WRITE_BAR_PATH, "value", "0");
    add_button_state_bindings(updates, true, impl_->update_available);
    auto binding_result = context.gui().set_binding_values(updates);
    if (!binding_result) {
        ESP_LOGW(TAG, "OTA app initial UI binding failed: %s", binding_result.error().c_str());
    }
    return {};
}

std::expected<void, std::string> WatchOtaApp::on_stop(system::core::AppContext &)
{
    ESP_LOGI(TAG, "OTA app stop");
    std::lock_guard lock(impl_->mutex);
    impl_->context = nullptr;
    return {};
}

std::expected<void, std::string> WatchOtaApp::subscribe_actions(system::core::AppContext &context)
{
    return context.gui().subscribe_actions(std::vector<std::string> {
        ACTION_CHECK,
        ACTION_CONFIRM,
    });
}

std::expected<void, std::string> WatchOtaApp::on_action(
    system::core::AppContext &context,
    std::string_view action
)
{
    if (action == ACTION_CHECK) {
        start_operation(context, Operation::Check);
        return {};
    }
    if (action == ACTION_CONFIRM) {
        start_operation(context, Operation::Update);
        return {};
    }
    return {};
}

void WatchOtaApp::start_operation(system::core::AppContext &context, Operation operation)
{
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->operation_running) {
            (void)context.gui().set_binding_value(STATUS_PATH, "status", "任务正在执行，请等待");
            return;
        }
        impl_->operation_running = true;
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding(updates, STATUS_PATH, "status", operation == Operation::Check ? "正在检测更新..." : "准备 OTA 更新...");
    add_button_state_bindings(updates, false, false);
    (void)context.gui().set_binding_values(updates);

    auto *request = new TaskRequest{
        .app = this,
        .operation = operation,
    };
    const BaseType_t ok = xTaskCreate(
        [](void *raw) {
            std::unique_ptr<TaskRequest> request(static_cast<TaskRequest *>(raw));
            if (request->app != nullptr) {
                request->app->run_operation(request->operation);
            }
            vTaskDelete(nullptr);
        },
        operation == Operation::Check ? "ota_check" : "ota_update",
        OTA_TASK_STACK_SIZE,
        request,
        OTA_TASK_PRIORITY,
        nullptr
    );
    if (ok != pdPASS) {
        delete request;
        std::lock_guard lock(impl_->mutex);
        impl_->operation_running = false;
        std::vector<gui::BindingValueUpdate> failure_updates;
        add_binding(failure_updates, STATUS_PATH, "status", "任务启动失败");
        add_button_state_bindings(failure_updates, true, operation == Operation::Update);
        (void)context.gui().set_binding_values(failure_updates);
    }
}

void WatchOtaApp::run_operation(Operation operation)
{
    auto push_updates = [this](const std::vector<gui::BindingValueUpdate> &updates) {
        std::lock_guard lock(impl_->mutex);
        if (impl_->context != nullptr) {
            auto result = impl_->context->gui().set_binding_values(updates);
            if (!result) {
                ESP_LOGW(TAG, "Failed to update OTA UI: %s", result.error().c_str());
            }
        }
    };
    auto push_status = [&push_updates](std::string status) {
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, STATUS_PATH, "status", std::move(status));
        push_updates(updates);
    };
    auto finish_buttons = [this, &push_updates]() {
        bool update_available = false;
        {
            std::lock_guard lock(impl_->mutex);
            update_available = impl_->update_available;
            impl_->operation_running = false;
        }
        std::vector<gui::BindingValueUpdate> updates;
        add_button_state_bindings(updates, true, update_available);
        push_updates(updates);
    };

    if (!wifi_connected()) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->operation_running = false;
        }
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, STATUS_PATH, "status", "Wi-Fi 未连接，请先进入设置配网");
        add_button_state_bindings(updates, true, false);
        push_updates(updates);
        return;
    }

    auto get_token = [this, &push_status](bool force_login, std::string &token) -> bool {
        {
            std::lock_guard lock(impl_->mutex);
            if (!force_login) {
                token = impl_->token;
            }
        }
        if (!token.empty() && !force_login) {
            return true;
        }
        push_status("正在登录 OTA 服务器...");
        if (!login(token)) {
            push_status("登录失败，请检查网络/服务器");
            return false;
        }
        std::lock_guard lock(impl_->mutex);
        impl_->token = token;
        return true;
    };

    auto fetch_config_with_retry = [this, &get_token, &push_status](
                                       OtaConfig &config,
                                       std::string &token,
                                       int &status_code
                                   ) -> bool {
        if (!get_token(false, token)) {
            return false;
        }
        if (fetch_device_config(token, config, status_code)) {
            return true;
        }
        if (status_code != 401 && status_code != 403) {
            return false;
        }
        {
            std::lock_guard lock(impl_->mutex);
            impl_->token.clear();
        }
        token.clear();
        push_status("登录已过期，重新登录...");
        return get_token(true, token) && fetch_device_config(token, config, status_code);
    };

    std::string token;

    if (operation == Operation::Check) {
        push_status("正在拉取远程版本...");
        OtaConfig config;
        int status_code = 0;
        if (!fetch_config_with_retry(config, token, status_code)) {
            report_status(token, "配置拉取失败", "");
            push_status("配置拉取失败 HTTP " + std::to_string(status_code));
            finish_buttons();
            return;
        }

        const bool available = config.ota_enabled &&
                               !config.firmware_version.empty() &&
                               !config.firmware_url.empty() &&
                               firmware_url_supported(config.firmware_url) &&
                               version_is_newer(config.firmware_version, current_firmware_version());
        {
            std::lock_guard lock(impl_->mutex);
            impl_->latest_config = config;
            impl_->update_available = available;
        }

        report_status(token, available ? "发现新版本" : "无更新", config.firmware_version);
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, REMOTE_VERSION_PATH, "value", config.firmware_version.empty() ? "--" : config.firmware_version);
        add_binding(updates, PARTITION_PATH, "value", make_partition_summary());
        add_binding(updates, LOG_PATH, "log", config.ota_log.empty() ? "无更新说明" : config.ota_log);
        if (!config.ota_enabled) {
            add_binding(updates, STATUS_PATH, "status", "服务器 OTA 未开启");
        } else if (!firmware_url_supported(config.firmware_url) && !config.firmware_url.empty()) {
            add_binding(updates, STATUS_PATH, "status", "固件地址不是 http://，当前不支持");
        } else if (available) {
            add_binding(updates, STATUS_PATH, "status", "发现新版本，确认后开始更新");
        } else {
            add_binding(updates, STATUS_PATH, "status", "当前已是最新版本");
        }
        push_updates(updates);
        finish_buttons();
        return;
    }

    push_status("更新前重新确认服务器配置...");
    OtaConfig config;
    int status_code = 0;
    if (!fetch_config_with_retry(config, token, status_code)) {
        report_status(token, "配置拉取失败", "");
        push_status("配置拉取失败 HTTP " + std::to_string(status_code));
        finish_buttons();
        return;
    }
    const bool available = config.ota_enabled &&
                           !config.firmware_version.empty() &&
                           !config.firmware_url.empty() &&
                           firmware_url_supported(config.firmware_url) &&
                           version_is_newer(config.firmware_version, current_firmware_version());
    {
        std::lock_guard lock(impl_->mutex);
        impl_->latest_config = config;
        impl_->update_available = available;
    }
    if (!config.ota_enabled || config.firmware_url.empty() ||
            !version_is_newer(config.firmware_version, current_firmware_version())) {
        push_status("没有可更新的版本，请先检测更新");
        finish_buttons();
        return;
    }
    if (!firmware_url_supported(config.firmware_url)) {
        report_status(token, "OTA失败：固件URL不是HTTP", config.firmware_version);
        push_status("固件地址不是 http://，已取消");
        finish_buttons();
        return;
    }

    const esp_partition_t *next_partition = esp_ota_get_next_update_partition(nullptr);
    const std::string target_partition_label = partition_label(next_partition);
    if (next_partition == nullptr) {
        report_status(token, "OTA失败：无可用分区", config.firmware_version);
        push_status("没有可用 OTA 分区");
        finish_buttons();
        return;
    }

    report_status(token, "OTA开始", config.firmware_version, target_partition_label, 0, 0, -1);
    {
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, STATUS_PATH, "status", std::string("开始下载 ") + config.firmware_version);
        add_binding(updates, REMOTE_VERSION_PATH, "value", config.firmware_version);
        add_binding(updates, PARTITION_PATH, "value", make_partition_summary());
        add_binding(updates, LOG_PATH, "log", config.ota_log.empty() ? config.firmware_url : config.ota_log);
        add_binding(updates, DOWNLOAD_LABEL_PATH, "text", "下载进度 0%");
        add_binding(updates, DOWNLOAD_BAR_PATH, "value", "0");
        add_binding(updates, WRITE_LABEL_PATH, "text", "写入进度 0%");
        add_binding(updates, WRITE_BAR_PATH, "value", "0");
        push_updates(updates);
    }

    esp_http_client_config_t http_config = {};
    http_config.url = config.firmware_url.c_str();
    http_config.timeout_ms = 30000;
    http_config.user_agent = USER_AGENT;
    http_config.keep_alive_enable = false;

    esp_http_client_handle_t http_client = esp_http_client_init(&http_config);
    if (http_client == nullptr) {
        report_status(token, "OTA失败：HTTP初始化失败", config.firmware_version, target_partition_label);
        push_status("HTTP 初始化失败");
        finish_buttons();
        return;
    }

    auto cleanup_http_client = [&]() {
        if (http_client != nullptr) {
            esp_http_client_cleanup(http_client);
            http_client = nullptr;
        }
    };

    esp_http_client_set_method(http_client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        cleanup_http_client();
        report_status(token, std::string("OTA失败：固件连接失败：") + esp_err_to_name(err),
                      config.firmware_version, target_partition_label);
        push_status(std::string("固件连接失败：") + esp_err_to_name(err));
        finish_buttons();
        return;
    }

    const int64_t content_length = esp_http_client_fetch_headers(http_client);
    const int firmware_status_code = esp_http_client_get_status_code(http_client);
    if (firmware_status_code < 200 || firmware_status_code >= 300) {
        cleanup_http_client();
        report_status(token, "OTA失败：固件HTTP错误", config.firmware_version, target_partition_label);
        push_status("固件下载 HTTP " + std::to_string(firmware_status_code));
        finish_buttons();
        return;
    }

    if (content_length > 0 && static_cast<uint64_t>(content_length) > next_partition->size) {
        cleanup_http_client();
        report_status(token, "OTA失败：固件超过分区大小", config.firmware_version, target_partition_label);
        push_status("固件太大，超过 OTA 分区");
        finish_buttons();
        return;
    }

    std::vector<uint8_t> ota_buffer(OTA_BUFFER_BYTES);
    if (ota_buffer.empty()) {
        cleanup_http_client();
        report_status(token, "OTA失败：内存不足", config.firmware_version, target_partition_label);
        push_status("OTA 缓冲区申请失败");
        finish_buttons();
        return;
    }

    esp_ota_handle_t update_handle = 0;
    bool ota_begin_done = false;
    bool app_desc_checked = false;
    int64_t total_read = 0;
    int last_percent = -1;
    int last_reported_percent = -1;
    int64_t last_reported_ms = 0;
    auto publish_progress = [&]() {
        const int64_t safe_total = total_read > 0 ? total_read : 0;
        int percent = 0;
        if (content_length > 0) {
            percent = static_cast<int>((safe_total * 100) / content_length);
            percent = std::clamp(percent, 0, 100);
        }
        if (percent != last_percent || content_length <= 0) {
            last_percent = percent;
            std::vector<gui::BindingValueUpdate> updates;
            add_binding(updates, DOWNLOAD_LABEL_PATH, "text", content_length > 0 ?
                        ("下载进度 " + std::to_string(percent) + "%") :
                        ("已下载 " + std::to_string(safe_total / 1024) + " KiB"));
            add_binding(updates, DOWNLOAD_BAR_PATH, "value", int_value(percent));
            add_binding(updates, WRITE_LABEL_PATH, "text", content_length > 0 ?
                        ("写入进度 " + std::to_string(percent) + "%") :
                        ("已写入 " + std::to_string(safe_total / 1024) + " KiB"));
            add_binding(updates, WRITE_BAR_PATH, "value", int_value(percent));
            add_button_state_bindings(updates, false, false);
            push_updates(updates);
        }

        const int64_t now_ms = esp_timer_get_time() / 1000;
        const bool percent_due = content_length > 0 &&
                                 (last_reported_percent < 0 ||
                                  percent >= 100 ||
                                  percent - last_reported_percent >= OTA_STATUS_REPORT_PERCENT_STEP);
        const bool time_due = last_reported_ms == 0 ||
                              now_ms - last_reported_ms >= OTA_STATUS_REPORT_INTERVAL_MS;
        if (percent_due || time_due) {
            last_reported_percent = percent;
            last_reported_ms = now_ms;
            report_status(
                token,
                std::string("OTA进度 ") + std::to_string(percent) + "%",
                config.firmware_version,
                target_partition_label,
                content_length > 0 ? percent : -1,
                safe_total,
                content_length
            );
        }
    };

    publish_progress();
    while (true) {
        const int data_read = esp_http_client_read(
            http_client,
            reinterpret_cast<char *>(ota_buffer.data()),
            static_cast<int>(ota_buffer.size())
        );
        if (data_read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (data_read == 0) {
            if (esp_http_client_is_complete_data_received(http_client)) {
                err = ESP_OK;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!app_desc_checked) {
            if (static_cast<size_t>(data_read) <= APP_DESC_HEADER_BYTES) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (ota_buffer[0] != ESP_APP_IMAGE_MAGIC) {
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
            std::array<uint8_t, APP_DESC_HEADER_BYTES> header_bytes = {};
            std::memcpy(header_bytes.data(), ota_buffer.data(), header_bytes.size());
            esp_app_desc_t new_app_info = {};
            if (!try_parse_app_desc_from_header(header_bytes, header_bytes.size(), new_app_info)) {
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
            if (!config.firmware_version.empty() && config.firmware_version != new_app_info.version) {
                report_status(token, "OTA失败：服务器版本与固件版本不一致",
                              config.firmware_version, target_partition_label);
                push_status(std::string("固件版本不一致：") + new_app_info.version);
                err = ESP_ERR_INVALID_VERSION;
                break;
            }

            err = esp_ota_begin(next_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
            if (err != ESP_OK) {
                break;
            }
            ota_begin_done = true;
            app_desc_checked = true;
        }

        err = esp_ota_write(update_handle, ota_buffer.data(), static_cast<size_t>(data_read));
        if (err != ESP_OK) {
            break;
        }
        total_read += data_read;
        publish_progress();
    }

    if (err != ESP_OK) {
        if (ota_begin_done) {
            esp_ota_abort(update_handle);
        }
        cleanup_http_client();
        const std::string detail = std::string("下载/写入失败：") + esp_err_to_name(err) + "，" +
                                   format_transfer_summary(total_read, content_length);
        report_status(token, std::string("OTA失败：") + detail, config.firmware_version, target_partition_label);
        push_status(detail);
        finish_buttons();
        return;
    }
    if (!app_desc_checked || !ota_begin_done) {
        cleanup_http_client();
        report_status(token, "OTA失败：未收到有效固件头", config.firmware_version, target_partition_label);
        push_status("未收到有效固件头");
        finish_buttons();
        return;
    }
    if (!esp_http_client_is_complete_data_received(http_client) || (content_length > 0 && total_read != content_length)) {
        const std::string detail = "固件长度不完整：" + format_transfer_summary(total_read, content_length);
        esp_ota_abort(update_handle);
        cleanup_http_client();
        report_status(token, std::string("OTA失败：") + detail, config.firmware_version, target_partition_label);
        push_status(detail);
        finish_buttons();
        return;
    }

    report_status(token, "OTA校验并切换启动分区", config.firmware_version, target_partition_label);
    {
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, STATUS_PATH, "status", "下载完成，正在执行官方镜像校验");
        add_binding(updates, DOWNLOAD_LABEL_PATH, "text", "下载进度 100%");
        add_binding(updates, DOWNLOAD_BAR_PATH, "value", "100");
        add_binding(updates, WRITE_LABEL_PATH, "text", "写入进度 100%");
        add_binding(updates, WRITE_BAR_PATH, "value", "100");
        add_button_state_bindings(updates, false, false);
        push_updates(updates);
    }
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        cleanup_http_client();
        const std::string detail = std::string("官方镜像校验失败：") + esp_err_to_name(err) + "，" +
                                   format_transfer_summary(total_read, content_length);
        report_status(token, std::string("OTA失败：") + detail, config.firmware_version, target_partition_label);
        push_status(detail);
        finish_buttons();
        return;
    }

    err = esp_ota_set_boot_partition(next_partition);
    if (err != ESP_OK) {
        cleanup_http_client();
        report_status(token, std::string("OTA失败：切换启动分区失败：") + esp_err_to_name(err),
                      config.firmware_version, target_partition_label);
        push_status(std::string("切换启动分区失败：") + esp_err_to_name(err));
        finish_buttons();
        return;
    }
    cleanup_http_client();

    report_status(token, "OTA完成", config.firmware_version, target_partition_label);
    {
        std::vector<gui::BindingValueUpdate> updates;
        add_binding(updates, STATUS_PATH, "status", "OTA 完成，正在重启...");
        add_binding(updates, DOWNLOAD_LABEL_PATH, "text", "下载进度 100%");
        add_binding(updates, DOWNLOAD_BAR_PATH, "value", "100");
        add_binding(updates, WRITE_LABEL_PATH, "text", "写入进度 100%");
        add_binding(updates, WRITE_BAR_PATH, "value", "100");
        add_button_state_bindings(updates, false, false);
        push_updates(updates);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

system::core::AppManifest WatchOtaAppProvider::get_manifest() const
{
    return WatchOtaApp().get_manifest();
}

std::shared_ptr<system::core::IApp> WatchOtaAppProvider::create_app()
{
    return std::make_shared<WatchOtaApp>();
}

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    WatchOtaAppProvider,
    APP_ID,
    app_watch_ota_provider_symbol
);

} // namespace esp_brookesia::app::watch_ota
