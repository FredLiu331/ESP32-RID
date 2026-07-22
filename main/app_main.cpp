#include <array>
#include <memory>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#include "rid/nvs_store.hpp"
#include "rid/radio_coordinator.hpp"
#include "rid/ble_transport.hpp"
#include "rid/runtime.hpp"
#include "rid/wifi_transport.hpp"

namespace {
constexpr char kTag[] = "rid";
constexpr size_t kMaxWifiPollsPerTick = 8;
SemaphoreHandle_t g_nimble_sync;
uint8_t g_nimble_address_type;

void nimble_sync() {
    if (ble_hs_id_infer_auto(0, &g_nimble_address_type) == 0 && g_nimble_sync != nullptr) {
        xSemaphoreGive(g_nimble_sync);
    }
}

void nimble_host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool start_nimble() {
    g_nimble_sync = xSemaphoreCreateBinary();
    if (g_nimble_sync == nullptr) return false;
    if (nimble_port_init() != ESP_OK) {
        vSemaphoreDelete(g_nimble_sync);
        g_nimble_sync = nullptr;
        return false;
    }
    ble_hs_cfg.sync_cb = nimble_sync;
    nimble_port_freertos_init(nimble_host_task);
    const bool synced = xSemaphoreTake(g_nimble_sync, pdMS_TO_TICKS(5000)) == pdTRUE;
    vSemaphoreDelete(g_nimble_sync);
    g_nimble_sync = nullptr;
    if (!synced) {
        nimble_port_stop();
        nimble_port_deinit();
    }
    return synced;
}

class RadioSink final : public rid::RuntimeSink {
public:
    RadioSink(rid::RadioCoordinator &wifi, rid::BleTransport &ble) : wifi_(wifi), ble_(ble) {}

    bool submit(const rid::ScheduledPayload &payload) override {
        if (payload.transport == rid::Transport::Ble4 || payload.transport == rid::Transport::Ble5) {
            return ble_.submit(payload) == rid::BleTransportError::None;
        }
        return wifi_.submit(payload) == ESP_OK;
    }

private:
    rid::RadioCoordinator &wifi_;
    rid::BleTransport &ble_;
};

struct RuntimeContext {
    rid::EspWifiBackend wifi_backend;
    rid::RadioCoordinator wifi{wifi_backend};
    rid::NimbleBleGapBackend ble_backend{g_nimble_address_type};
    rid::BleTransport ble{ble_backend};
    std::unique_ptr<RadioSink> sink;
    std::unique_ptr<rid::Runtime> runtime;
};

RuntimeContext *g_context;

void runtime_task(void *) {
    while (true) {
        const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
        if (g_context->runtime != nullptr) {
            g_context->runtime->tick(now);
            for (size_t attempt = 0; attempt < kMaxWifiPollsPerTick; ++attempt) {
                const size_t pending = g_context->wifi.depth(rid::Transport::Wifi24) +
                                       g_context->wifi.depth(rid::Transport::Wifi58);
                if (pending == 0 || g_context->wifi.poll(now) != ESP_OK) break;
            }
            g_context->ble.poll(now);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error == ESP_ERR_NVS_NO_FREE_PAGES || nvs_error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_error = nvs_flash_erase();
        if (nvs_error == ESP_OK) nvs_error = nvs_flash_init();
    }
    if (nvs_error != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(nvs_error));
        return;
    }

    std::array<uint8_t, 6> mac{};
    if (esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(kTag, "failed to read Wi-Fi MAC");
        return;
    }
    if (!start_nimble()) {
        ESP_LOGE(kTag, "NimBLE initialization failed");
        return;
    }
    auto context = std::make_unique<RuntimeContext>();
    if (context->wifi_backend.set_country_cn() != ESP_OK) {
        ESP_LOGE(kTag, "Wi-Fi initialization failed");
        return;
    }
    context->sink = std::make_unique<RadioSink>(context->wifi, context->ble);

    rid::NvsConfigStore store;
    const auto loaded = store.load();
    if (loaded.ok()) {
        context->runtime = std::make_unique<rid::Runtime>(rid::DeviceId{mac}, *context->sink);
        const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
        const auto result = context->runtime->apply(loaded.value->config, now);
        if (result == rid::RuntimeError::None) {
            ESP_LOGI(kTag, "auto-start generation=%lu aircraft=%u",
                     static_cast<unsigned long>(loaded.value->generation),
                     static_cast<unsigned>(context->runtime->aircraft_count()));
        } else {
            ESP_LOGE(kTag, "stored configuration rejected; staying idle");
            context->runtime.reset();
        }
    } else {
        ESP_LOGW(kTag, "no valid stored configuration; staying idle");
    }
    g_context = context.get();
    if (xTaskCreate(runtime_task, "rid_runtime", 12288, nullptr, 5, nullptr) != pdPASS) {
        g_context = nullptr;
        ESP_LOGE(kTag, "failed to create RID runtime task");
        return;
    }
    context.release();
}
