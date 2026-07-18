#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

struct ProbeResult {
    const char *name;
    bool api_supported;
    uint32_t submitted;
    uint32_t completed;
    esp_err_t last_error;
};

namespace {

constexpr uint32_t kWifiTxCount = 3;
constexpr uint32_t kBleProcedureCount = 3;
constexpr uint32_t kWaitStepMs = 10;
constexpr uint32_t kWaitTimeoutMs = 5000;
constexpr uint8_t kExtAdvInstance = 0;
constexpr uint8_t kCoexistAdvInstance = 1;

SemaphoreHandle_t g_ble_sync;
ProbeResult *g_active_wifi_probe;
ProbeResult *g_legacy_probe;
ProbeResult *g_ext_probe;
ProbeResult *g_coexist_probe;
bool g_coexist_running;
uint8_t g_ble_addr_type;
const char *g_coexist_error_domain = "none";

// OpenDroneID service data: UUID 0xFFFA, counter, and one 25-byte Basic ID message.
constexpr std::array<uint8_t, 31> kRidAdvData = {
    0x02, 0x01, 0x06,
    0x1b, 0x16, 0xfa, 0xff, 0x00,
    0x00, 0x10, 'R', 'I', 'D', '-', 'P', 'R', 'O', 'B', 'E', '-', '0', '0', '0', '0', '0', '0', '0', '0', '0', '1', 0x00,
};

std::array<uint8_t, 42> make_beacon(const std::array<uint8_t, 6> &mac) {
    std::array<uint8_t, 42> frame = {
        0x80, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,
        0x00, 0x00,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x64, 0x00, 0x01, 0x04,
        0x07, 0x00,
        0x01, 0x01, 0x8c,
    };
    std::memcpy(frame.data() + 10, mac.data(), mac.size());
    std::memcpy(frame.data() + 16, mac.data(), mac.size());
    return frame;
}

std::array<uint8_t, 30> make_nan_action(const std::array<uint8_t, 6> &mac) {
    std::array<uint8_t, 30> frame = {
        0xd0, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0, 0, 0,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00,
        0x04, 0x09, 0x50, 0x6f, 0x9a, 0x13,
    };
    std::memcpy(frame.data() + 10, mac.data(), mac.size());
    return frame;
}

void set_nimble_error(ProbeResult *result, int rc) {
    if (result != nullptr && rc != 0) {
        result->last_error = static_cast<esp_err_t>(rc);
        if (result == g_coexist_probe) {
            g_coexist_error_domain = "nimble";
        }
    }
}

bool wait_for_count(const uint32_t *count, uint32_t target, uint32_t timeout_ms) {
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed += kWaitStepMs) {
        if (__atomic_load_n(count, __ATOMIC_RELAXED) >= target) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kWaitStepMs));
    }
    return __atomic_load_n(count, __ATOMIC_RELAXED) >= target;
}

void wifi_tx_done(const esp_80211_tx_info_t *tx_info) {
    ProbeResult *result = g_active_wifi_probe;
    if (result == nullptr || tx_info == nullptr) {
        return;
    }
    if (tx_info->tx_status == WIFI_SEND_SUCCESS) {
        __atomic_add_fetch(&result->completed, 1U, __ATOMIC_RELAXED);
    } else {
        result->last_error = ESP_FAIL;
    }
}

int start_legacy_cycle();

int legacy_gap_event(ble_gap_event *event, void *) {
    if (event->type != BLE_GAP_EVENT_ADV_COMPLETE || g_legacy_probe == nullptr) {
        return 0;
    }
    if (event->adv_complete.reason != BLE_HS_ETIMEOUT) {
        set_nimble_error(g_legacy_probe, event->adv_complete.reason);
        return 0;
    }
    const uint32_t completed = __atomic_add_fetch(&g_legacy_probe->completed, 1U, __ATOMIC_RELAXED);
    if (completed < kBleProcedureCount) {
        set_nimble_error(g_legacy_probe, start_legacy_cycle());
    }
    return 0;
}

int start_legacy_cycle() {
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    const int rc = ble_gap_adv_start(g_ble_addr_type, nullptr, 80, &params, legacy_gap_event, nullptr);
    if (rc == 0 && g_legacy_probe != nullptr) {
        __atomic_add_fetch(&g_legacy_probe->submitted, 1U, __ATOMIC_RELAXED);
    }
    return rc;
}

int ext_gap_event(ble_gap_event *event, void *arg) {
    auto *result = static_cast<ProbeResult *>(arg);
    if (event->type != BLE_GAP_EVENT_ADV_COMPLETE || result == nullptr) {
        return 0;
    }
    if (result == g_coexist_probe && !g_coexist_running) {
        return 0;
    }
    if (event->adv_complete.reason != BLE_HS_ETIMEOUT) {
        set_nimble_error(result, event->adv_complete.reason);
        return 0;
    }
    __atomic_add_fetch(&result->completed,
                       static_cast<uint32_t>(event->adv_complete.num_ext_adv_events),
                       __ATOMIC_RELAXED);

    if (result == g_coexist_probe && g_coexist_running) {
        const int rc = ble_gap_ext_adv_start(kCoexistAdvInstance, 0, 1);
        if (rc == 0) {
            __atomic_add_fetch(&result->submitted, 1U, __ATOMIC_RELAXED);
        } else {
            set_nimble_error(result, rc);
        }
    }
    return 0;
}

void ble_on_sync() {
    const int rc = ble_hs_id_infer_auto(0, &g_ble_addr_type);
    if (rc == 0) {
        xSemaphoreGive(g_ble_sync);
    }
}

void nimble_host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool init_wifi(std::array<uint8_t, 6> *mac, esp_err_t *last_error) {
    auto check = [last_error](esp_err_t err) {
        if (err != ESP_OK) {
            *last_error = err;
            return false;
        }
        return true;
    };

    if (!check(esp_netif_init())) return false;
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        *last_error = err;
        return false;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (!check(esp_wifi_init(&config))) return false;
    if (!check(esp_wifi_set_storage(WIFI_STORAGE_RAM))) return false;
    if (!check(esp_wifi_set_mode(WIFI_MODE_STA))) return false;

    wifi_country_t country{};
    std::memcpy(country.cc, "CN", 3);
    country.schan = 1;
    country.nchan = 13;
    country.max_tx_power = 8;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    country.wifi_5g_channel_mask = WIFI_CHANNEL_149;
#endif
    if (!check(esp_wifi_set_country(&country))) return false;
    if (!check(esp_wifi_start())) return false;
    if (!check(esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO))) return false;
    if (!check(esp_wifi_set_max_tx_power(8))) return false;
    if (!check(esp_wifi_get_mac(WIFI_IF_STA, mac->data()))) return false;
    return check(esp_wifi_register_80211_tx_cb(wifi_tx_done));
}

bool init_ble(esp_err_t *last_error) {
    g_ble_sync = xSemaphoreCreateBinary();
    if (g_ble_sync == nullptr) {
        *last_error = ESP_ERR_NO_MEM;
        return false;
    }
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        *last_error = err;
        return false;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(nimble_host_task);
    if (xSemaphoreTake(g_ble_sync, pdMS_TO_TICKS(kWaitTimeoutMs)) != pdTRUE) {
        *last_error = ESP_ERR_TIMEOUT;
        return false;
    }
    return true;
}

template <size_t N>
void run_wifi_probe(ProbeResult *result, uint8_t channel, const std::array<uint8_t, N> &frame) {
    result->api_supported = true;
    result->last_error = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (result->last_error != ESP_OK) return;
    g_active_wifi_probe = result;
    for (uint32_t i = 0; i < kWifiTxCount; ++i) {
        result->last_error = esp_wifi_80211_tx(WIFI_IF_STA, frame.data(), frame.size(), true);
        if (result->last_error != ESP_OK) break;
        ++result->submitted;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!wait_for_count(&result->completed, result->submitted, kWaitTimeoutMs) &&
        result->last_error == ESP_OK) {
        result->last_error = ESP_ERR_TIMEOUT;
    }
    g_active_wifi_probe = nullptr;
}

int configure_ext_adv(uint8_t instance, ProbeResult *result) {
    ble_gap_ext_adv_params params{};
    params.own_addr_type = g_ble_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127;
    params.sid = instance;
    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    int rc = ble_gap_ext_adv_configure(instance, &params, nullptr, ext_gap_event, result);
    if (rc != 0) return rc;
    os_mbuf *data = os_msys_get_pkthdr(kRidAdvData.size(), 0);
    if (data == nullptr) return BLE_HS_ENOMEM;
    rc = os_mbuf_append(data, kRidAdvData.data(), kRidAdvData.size());
    if (rc != 0) {
        os_mbuf_free_chain(data);
        return rc;
    }
    return ble_gap_ext_adv_set_data(instance, data);
}

void run_legacy_probe(ProbeResult *result) {
    result->api_supported = true;
    g_legacy_probe = result;
    int rc = ble_gap_adv_set_data(kRidAdvData.data(), kRidAdvData.size());
    if (rc == 0) rc = start_legacy_cycle();
    set_nimble_error(result, rc);
    if (!wait_for_count(&result->completed, kBleProcedureCount, kWaitTimeoutMs) &&
        result->last_error == ESP_OK) {
        result->last_error = ESP_ERR_TIMEOUT;
    }
    g_legacy_probe = nullptr;
}

void run_ext_probe(ProbeResult *result) {
    result->api_supported = true;
    g_ext_probe = result;
    int rc = configure_ext_adv(kExtAdvInstance, result);
    if (rc == 0) rc = ble_gap_ext_adv_start(kExtAdvInstance, 0, kBleProcedureCount);
    if (rc == 0) ++result->submitted;
    set_nimble_error(result, rc);
    if (!wait_for_count(&result->completed, kBleProcedureCount, kWaitTimeoutMs) &&
        result->last_error == ESP_OK) {
        result->last_error = ESP_ERR_TIMEOUT;
    }
    g_ext_probe = nullptr;
}

void run_coexist_probe(ProbeResult *result) {
    result->api_supported = true;
    g_coexist_probe = result;
    int rc = configure_ext_adv(kCoexistAdvInstance, result);
    if (rc == 0) {
        g_coexist_running = true;
        rc = ble_gap_ext_adv_start(kCoexistAdvInstance, 0, 1);
    }
    if (rc == 0) ++result->submitted;
    set_nimble_error(result, rc);
    const uint32_t before = __atomic_load_n(&result->completed, __ATOMIC_RELAXED);
    unsigned consecutive_errors = 0;
    bool fatal_switch_error = false;
    for (unsigned cycle = 0; !fatal_switch_error && cycle < 100; ++cycle) {
        for (uint8_t channel : {6, 149, 6}) {
            const esp_err_t channel_error = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            if (channel_error == ESP_OK) {
                consecutive_errors = 0;
            } else if (++consecutive_errors >= 2) {
                result->last_error = channel_error;
                g_coexist_error_domain = "esp";
                fatal_switch_error = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    g_coexist_running = false;
    const int stop_rc = ble_gap_ext_adv_stop(kCoexistAdvInstance);
    if (stop_rc != 0 && stop_rc != BLE_HS_EALREADY) set_nimble_error(result, stop_rc);
    if (__atomic_load_n(&result->completed, __ATOMIC_RELAXED) <= before && result->last_error == ESP_OK) {
        result->last_error = ESP_ERR_TIMEOUT;
        g_coexist_error_domain = "probe";
    }
    g_coexist_probe = nullptr;
}

bool passed(const ProbeResult &result, uint32_t minimum_completed) {
    return result.api_supported && result.last_error == ESP_OK && result.submitted > 0 &&
           result.completed >= minimum_completed;
}

void print_result(const ProbeResult &result, uint32_t minimum_completed) {
    const bool ok = passed(result, minimum_completed);
    const char *domain = result.name[0] == 'b' ? "nimble" : "esp";
    if (std::strcmp(result.name, "coexist_hop") == 0) domain = g_coexist_error_domain;
    std::printf("PROBE %s %s\n", result.name, ok ? "PASS" : "FAIL");
    std::printf("PROBE_DETAIL %s supported=%u submitted=%lu completed=%lu error=%ld domain=%s\n",
                result.name, result.api_supported ? 1U : 0U,
                static_cast<unsigned long>(result.submitted),
                static_cast<unsigned long>(result.completed),
                static_cast<long>(result.last_error),
                domain);
}

}  // namespace

extern "C" void app_main() {
    esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error == ESP_ERR_NVS_NO_FREE_PAGES || nvs_error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_error = nvs_flash_erase();
        if (nvs_error == ESP_OK) nvs_error = nvs_flash_init();
    }

    std::array<uint8_t, 6> mac{};
    esp_err_t wifi_error = nvs_error;
    esp_err_t ble_error = nvs_error;
    const bool wifi_ready = nvs_error == ESP_OK && init_wifi(&mac, &wifi_error);
    const bool ble_ready = nvs_error == ESP_OK && init_ble(&ble_error);

    ProbeResult beacon6{"wifi_beacon_ch6", wifi_ready, 0, 0, wifi_error};
    ProbeResult beacon149{"wifi_beacon_ch149", wifi_ready, 0, 0, wifi_error};
    ProbeResult nan6{"wifi_nan_ch6", wifi_ready, 0, 0, wifi_error};
    ProbeResult nan149{"wifi_nan_ch149", wifi_ready, 0, 0, wifi_error};
    ProbeResult ble4{"ble4", ble_ready, 0, 0, ble_error};
    ProbeResult ble5{"ble5", ble_ready, 0, 0, ble_error};
    ProbeResult coexist{"coexist_hop", wifi_ready && ble_ready, 0, 0,
                        wifi_ready ? ble_error : wifi_error};
    if (!wifi_ready) g_coexist_error_domain = "esp";
    else if (!ble_ready) g_coexist_error_domain = "nimble";

    if (wifi_ready) {
        auto beacon = make_beacon(mac);
        auto nan = make_nan_action(mac);
        run_wifi_probe(&beacon6, 6, beacon);
        run_wifi_probe(&beacon149, 149, beacon);
        run_wifi_probe(&nan6, 6, nan);
        run_wifi_probe(&nan149, 149, nan);
    }
    if (ble_ready) {
        run_legacy_probe(&ble4);
        run_ext_probe(&ble5);
        run_coexist_probe(&coexist);
    }

    print_result(beacon6, kWifiTxCount);
    print_result(beacon149, kWifiTxCount);
    print_result(nan6, kWifiTxCount);
    print_result(nan149, kWifiTxCount);
    print_result(ble4, kBleProcedureCount);
    print_result(ble5, kBleProcedureCount);
    print_result(coexist, 1);
}
