/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app_priv.h>
#include <app_reset.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include "shared.h"
#include "driver/gpio.h"

#define PRODUCT_NAME "Lüfter-Device"
#define NVS_NAMESPACE "storage"
#define COMMISSIONED_KEY "commissioned"

static const char *TAG = "app_main";
uint16_t fan_endpoint_id = 0;
uint16_t fan_endpoint_id_1 = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA
//
// factory reset button
static gpio_num_t reset_gpio = gpio_num_t::GPIO_NUM_NC;
bool commissioned = false;

// Funktion zum Speichern des Kommissionierungsstatus
void save_commissioned_status(bool status) {
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, COMMISSIONED_KEY, status ? 1 : 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
    }
}

// Funktion zum Laden des Kommissionierungsstatus
bool load_commissioned_status() {
    nvs_handle_t nvs_handle;
    uint8_t commissioned_status = 0; // Standardwert: nicht kommissioniert

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, COMMISSIONED_KEY, &commissioned_status);
        nvs_close(nvs_handle);
    } else {
        ESP_LOGW(TAG, "Failed to open NVS for reading");
    }

    return commissioned_status == 1;
}
///////////////////////////////////////////// led
void led_blink_task(void *pvParameter)
{
    while (true) {

        if (!commissioned) {
            // Blinken: LED an
            gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms warten

            // Blinken: LED aus
            gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms warten
        } else {
            gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
///////////////////////////////////////////// led

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        commissioned = true;
        save_commissioned_status(commissioned);
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver */
    commissioned = load_commissioned_status();

    led_init();
    // app_driver_handle_t button_handle = app_driver_button_init();
    // app_reset_button_register(button_handle);
    /* Can ininialize Fan driver here */
    app_driver_handle_t fan_handle = app_driver_fan_init();

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    snprintf(node_config.root_node.basic_information.node_label, sizeof(node_config.root_node.basic_information.node_label),"%s", PRODUCT_NAME);
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);

    fan::config_t fan_config;
    fan_config.fan_control.fan_mode_sequence = FAN_MODE_SEQUEBCE_VALUE;
    fan_config.fan_control.percent_current = 0;
    fan_config.fan_control.percent_setting = static_cast<uint8_t>(0);

    endpoint_t *endpoint = fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, fan_handle);

    fan_endpoint_id = endpoint::get_id(endpoint);

    endpoint_t *endpoint_1 = fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, fan_handle);
    fan_endpoint_id_1 = endpoint::get_id(endpoint_1);

    ESP_LOGI(TAG, "Light created with endpoint_id %d", fan_endpoint_id);
    ESP_LOGI(TAG, "Light created with endpoint_id %d", fan_endpoint_id_1);


    /* These node and endpoint handles can be used to create/add other endpoints and clusters. */
    if (!node || !endpoint) {
        ESP_LOGE(TAG, "Matter node creation failed");
    }

    ESP_LOGI(TAG, "Fan created with endpoint_id %d", fan_endpoint_id);

    xTaskCreate(led_blink_task, "blink_led_task", 2048, NULL, 5, NULL);


    // Initialize factory reset button
    app_driver_handle_t button_handle = app_driver_button_init(&reset_gpio);
    if (button_handle) {
        app_reset_button_register(button_handle);
    }
    //


#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed: %d", err);
    }

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err);
    }
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::init();
#endif
}
