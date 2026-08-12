#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "brookesia/lib_utils/signal.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::watch_ota {

class WatchOtaApp final: public system::core::IApp {
public:
    WatchOtaApp();
    ~WatchOtaApp() override;

    system::core::AppManifest get_manifest() const override;
    system::core::AppGuiDescriptor get_gui_descriptor() const override;
    std::expected<void, std::string> on_start(system::core::AppContext &context) override;
    std::expected<void, std::string> on_stop(system::core::AppContext &context) override;
    std::expected<void, std::string> on_action(
        system::core::AppContext &context,
        std::string_view action
    ) override;

    enum class Operation {
        Check,
        Update,
    };

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::expected<void, std::string> subscribe_actions(system::core::AppContext &context);
    void start_operation(system::core::AppContext &context, Operation operation);
    void run_operation(Operation operation);
};

class WatchOtaAppProvider final: public system::core::IAppProvider {
public:
    system::core::AppManifest get_manifest() const override;
    std::shared_ptr<system::core::IApp> create_app() override;
};

} // namespace esp_brookesia::app::watch_ota
