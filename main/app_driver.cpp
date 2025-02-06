/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <device.h>
#include <esp_matter.h>
#include <led_driver.h>

#include <app_priv.h>
#include "driver/ledc.h"
#include <app_reset.h>
#include <iot_button.h>
#include "driver/gpio.h"
#include "soc/gpio_num.h"

using namespace chip::app::Clusters;
using namespace chip::app::Clusters::FanControl;
using namespace esp_matter;

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz
#define LEDC_DUTY_MAX           8192

// PWM-Konfiguration für den Lüfter
#define MAX_LEDS 2


typedef struct {
    gpio_num_t gpio;
    ledc_channel_t channel;
    ledc_timer_t timer;
    bool power_state;
    uint32_t speed;
} led_config_t;

led_config_t fan_configs[MAX_LEDS] = {
    {
        .gpio = (gpio_num_t)CONFIG_EXAMPLE_FAN_GPIO,
        .channel = LEDC_CHANNEL_0,
        .timer = LEDC_TIMER_0,
        .power_state = false,
        .speed = 0
    },
    {
        .gpio = (gpio_num_t)CONFIG_EXAMPLE_FAN2_GPIO,
        .channel = LEDC_CHANNEL_1,
        .timer = LEDC_TIMER_1,
        .power_state = false,
        .speed = 0
    }
};

static const char *TAG = "app_driver";
extern uint16_t fan_endpoint_id;

static void get_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    node_t *node = node::get();
    endpoint_t *endpoint = endpoint::get(node, endpoint_id);
    cluster_t *cluster = cluster::get(endpoint, cluster_id);
    attribute_t *attribute = attribute::get(cluster, attribute_id);

    attribute::get_val(attribute, val);
}

static esp_err_t set_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t val)
{
    node_t *node = node::get();
    endpoint_t *endpoint = endpoint::get(node, endpoint_id);
    cluster_t *cluster = cluster::get(endpoint, cluster_id);
    attribute_t *attribute = attribute::get(cluster, attribute_id);

    return attribute::set_val(attribute, &val);
    //return attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

static bool check_if_mode_percent_match(uint8_t fan_mode, uint8_t percent)
{
    switch (fan_mode) {
        case chip::to_underlying(FanModeEnum::kHigh): {
            if (percent < HIGH_MODE_PERCENT_MIN) {
                return false;
            }
            break;
        }
        case chip::to_underlying(FanModeEnum::kMedium): {
            if ((percent < MED_MODE_PERCENT_MIN) || (percent > MED_MODE_PERCENT_MAX)) {
               return false;
            }
            break;
        }
        case chip::to_underlying(FanModeEnum::kLow): {
            if ((percent < LOW_MODE_PERCENT_MIN) || (percent > LOW_MODE_PERCENT_MAX)) {
                return false;
            }
            break;
        }
        default:
            return false;
    }

    return true;
}

static esp_err_t set_fan_speed(led_config_t *fan_config, uint8_t percent)
{
    if (fan_config == NULL) {
        ESP_LOGE(TAG, "Invalid fan config");
        return ESP_ERR_INVALID_ARG;
    }

    if (percent > 100) {
        percent = 100;
    }

    // Calculate duty cycle for 13-bit resolution (0-8191)
    uint32_t duty = 0;
    if (percent > 0) {
        duty = ((uint32_t)percent * (LEDC_DUTY_MAX - 1)) / 100;
    }

    ESP_LOGI(TAG, "duty=%lu", duty);
    ESP_LOGI(TAG, "Setting fan speed: GPIO=%d, Channel=%d, Percent=%d%%, Duty=%lu", 
             fan_config->gpio, fan_config->channel, percent, duty);

    // Store speed in config
    fan_config->speed = duty;

    // Set PWM duty cycle
    esp_err_t err = ledc_set_duty(LEDC_MODE, fan_config->channel, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s (channel: %d)", esp_err_to_name(err), fan_config->channel);
        return err;
    }

    // Update PWM duty cycle
    err = ledc_update_duty(LEDC_MODE, fan_config->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s (channel: %d)", esp_err_to_name(err), fan_config->channel);
        return err;
    }

    ESP_LOGI(TAG, "Successfully set fan speed to %d%% (duty: %lu)", percent, duty);
    return ESP_OK;
}

static void app_driver_fan_set_percent(led_driver_handle_t handle, esp_matter_attr_val_t val)
{
    /*this is just used to simulate fan driver*/
    set_attribute(fan_endpoint_id, FanControl::Id, Attributes::PercentCurrent::Id, val);
    ESP_LOGI(TAG, "Call app_driver_fan_set_percent");
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Enpoint id %d", endpoint_id);
    ESP_LOGI(TAG, "Custer id %lu", cluster_id);
    led_config_t *fan_configs = (led_config_t *)driver_handle;
    int fan_index = endpoint_id - 1;

    // if (fan_index < 0 || fan_index >= MAX_LEDS) {
    //     ESP_LOGE(TAG, "Invalid fan index: %d", fan_index);
    //     return ESP_ERR_INVALID_ARG;
    // }

    ESP_LOGI(TAG, "if Enpoint id %d", endpoint_id);
    if (cluster_id == FanControl::Id) {
        ESP_LOGI(TAG, "if Custer id %lu", cluster_id);
        if (attribute_id == FanControl::Attributes::FanMode::Id) {
            ESP_LOGI(TAG, "if attribute id %lu", attribute_id);
            ESP_LOGI(TAG, "fan index: %d", fan_index);
            esp_matter_attr_val_t val_a = esp_matter_invalid(NULL);
            get_attribute(endpoint_id, FanControl::Id, Attributes::PercentSetting::Id, &val_a);
            /* When FanMode attribute change , should check the persent setting value, if this value not match
               FanMode, need write the persent setting value to corresponding value. Now we set it to the max
               value of the FanMode*/
            if (!check_if_mode_percent_match(val->val.u8, val_a.val.u8)) {
                switch (val->val.u8) {
                    case chip::to_underlying(FanModeEnum::kHigh): {
                        ESP_LOGI(TAG, "fan index: %d", fan_index);
                        val_a.val.u8 = HIGH_MODE_PERCENT_MAX;
                        set_attribute(endpoint_id, FanControl::Id, Attributes::PercentSetting::Id, val_a);
                        err = set_fan_speed(&fan_configs[fan_index], HIGH_MODE_PERCENT_MAX);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to set HIGH mode");
                            return err;
                        }
                        ESP_LOGI(TAG, "HIGH MODE");
                        break;
                    }
                    case chip::to_underlying(FanModeEnum::kMedium): {
                        ESP_LOGI(TAG, "fan index: %d", fan_index);
                        set_attribute(endpoint_id, FanControl::Id, Attributes::PercentSetting::Id, val_a);
                        val_a.val.u8 = MED_MODE_PERCENT_MAX;
                        err = set_fan_speed(&fan_configs[fan_index], MED_MODE_PERCENT_MAX);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to set MEDIUM mode");
                            return err;
                        }
                        ESP_LOGI(TAG, "MEDIUM MODE");
                        break;
                    }
                    case chip::to_underlying(FanModeEnum::kLow): {
                        ESP_LOGI(TAG, "fan index: %d", fan_index);
                        set_attribute(endpoint_id, FanControl::Id, Attributes::PercentSetting::Id, val_a);
                        val_a.val.u8 = LOW_MODE_PERCENT_MAX;
                        err = set_fan_speed(&fan_configs[fan_index], LOW_MODE_PERCENT_MAX);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to set LOW mode");
                            return err;
                        }
                        ESP_LOGI(TAG, "LOW MODE");
                        break;
                    }
                    // case chip::to_underlying(FanModeEnum::kAuto): {
                    //     /* add auto mode driver for auto logic */
                    //     break;
                    // }
                    case chip::to_underlying(FanModeEnum::kOff): {
                        set_attribute(endpoint_id, FanControl::Id, Attributes::PercentSetting::Id, val_a);
                        val_a.val.u8 = 0;
                        set_fan_speed(&fan_configs[fan_index], 0);
                        ESP_LOGI(TAG, "OFF MODE");
                        break;
                    }
                    default:
                        break;
                }
            }
            // set_attribute(endpoint_id, FanControl::Id, FanControl::Attributes::PercentCurrent::Id, val_a);
        } else if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
            /* When the Percent setting attribute change, should check the FanMode value, if not match, need write
               the FanMode value to the corresponding FanMode.*/
            esp_matter_attr_val_t val_b = esp_matter_invalid(NULL);
            get_attribute(endpoint_id, FanControl::Id, Attributes::FanMode::Id, &val_b);
            if (!check_if_mode_percent_match(val_b.val.u8, val->val.u8)) {
                if (val->val.u8 >= HIGH_MODE_PERCENT_MIN) {
                    /* set high mode */
                    val_b.val.u8 = chip::to_underlying(FanModeEnum::kHigh);
                    set_attribute(endpoint_id, FanControl::Id, Attributes::FanMode::Id, val_b);
                } else if (val->val.u8 >= MED_MODE_PERCENT_MIN) {
                    /* set med mode */
                    val_b.val.u8 = chip::to_underlying(FanModeEnum::kMedium);
                    set_attribute(endpoint_id, FanControl::Id, Attributes::FanMode::Id, val_b);
                } else if (val->val.u8 >= LOW_MODE_PERCENT_MIN) {
                    /* set low mode */
                    val_b.val.u8 = chip::to_underlying(FanModeEnum::kLow);
                    set_attribute(endpoint_id, FanControl::Id, Attributes::FanMode::Id, val_b);
                }

            }
            // set_attribute(endpoint_id, FanControl::Id, FanControl::Attributes::PercentCurrent::Id, *val);
            /* add percent setting driver */
            // app_driver_fan_set_percent(*val);
        }
    }
    return err;
}

// app_driver_handle_t app_driver_button_init()
// {
//     /* Initialize button */
//     button_config_t config = button_driver_get_config();
// }

app_driver_handle_t app_driver_fan_init()
{
    for (int i = 0; i < MAX_LEDS; i++) {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .timer_num = fan_configs[i].timer,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_channel = {
            .gpio_num = fan_configs[i].gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = fan_configs[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = fan_configs[i].timer,
            .duty = 0,
            .hpoint = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }

    return (app_driver_handle_t)fan_configs;
}

app_driver_handle_t app_driver_button_init(gpio_num_t * reset_gpio)
{
    /* Initialize button */
    *reset_gpio = (gpio_num_t)CONFIG_BUTTON_PIN;
    app_driver_handle_t reset_handle = NULL;

    button_config_t config = {

        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 5000,
        .gpio_button_config = {
            .gpio_num = CONFIG_BUTTON_PIN,
            .active_level = 0,
        }
    };
    reset_handle = (app_driver_handle_t)iot_button_create(&config);
    return reset_handle;
}

void led_init()
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << (gpio_num_t)CONFIG_LED_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Schalten Sie die LED standardmäßig aus
    gpio_set_level((gpio_num_t)CONFIG_LED_PIN, 0);
}
