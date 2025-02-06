#include "shared.h"
#include <esp_log.h>
#include <esp_matter.h>
#include "iot_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "app_reset";
static bool perform_factory_reset = false;

static void button_factory_reset_pressed_cb(void *arg, void *data)
{
    if (!perform_factory_reset) {
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        ESP_LOGI(TAG, "ES WIRD WIRKLICH MEINE FUNKTION AUSGELöST");
        perform_factory_reset = true;

        // LED zum leuchten bringen
        gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 1);
    }
}

static void button_factory_reset_released_cb(void *arg, void *data)
{
    if (perform_factory_reset) {
        ESP_LOGI(TAG, "Starting factory reset");

        // LED zum leuchten bringen
        gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 0);

        commissioned = false;
        save_commissioned_status(commissioned);


        esp_matter::factory_reset();
        perform_factory_reset = false;
    }
}

esp_err_t app_reset_button_register(void *handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Handle cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    button_handle_t button_handle = (button_handle_t)handle;
    esp_err_t err = ESP_OK;
    err |= iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_HOLD, button_factory_reset_pressed_cb, NULL);
    err |= iot_button_register_cb(button_handle, BUTTON_PRESS_UP, button_factory_reset_released_cb, NULL);
    return err;
}

