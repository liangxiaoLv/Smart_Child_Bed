#include "gpio_driver.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "gpio_driver";

static bool s_isr_service_installed = false;

static esp_err_t ensureIsrService(void)
{
    if (!s_isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "ISR service 安装失败: %s", esp_err_to_name(ret));
            return ret;
        }
        s_isr_service_installed = true;
    }
    return ESP_OK;
}

esp_err_t gpioDriver_initOutput(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) gpio_set_level(pin, 0);
    return ret;
}

esp_err_t gpioDriver_initInput(gpio_num_t pin, bool pull_up)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t gpioDriver_initInterrupt(gpio_num_t pin,
                                   gpio_int_type_t intr_type,
                                   gpio_isr_t handler,
                                   void *arg)
{
    ESP_RETURN_ON_ERROR(ensureIsrService(), TAG, "ISR service 失败");

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = intr_type,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "GPIO config 失败");
    return gpio_isr_handler_add(pin, handler, arg);
}

esp_err_t gpioDriver_set(gpio_num_t pin, uint32_t level)
{
    return gpio_set_level(pin, level);
}

int gpioDriver_get(gpio_num_t pin)
{
    return gpio_get_level(pin);
}
