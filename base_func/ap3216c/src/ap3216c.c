#include "ap3216c.h"
#include "pin_map.h"
#include "i2c_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define AP3216C_REG_SYS_CONF   0x00
#define AP3216C_REG_ALS_LOW    0x0C
#define AP3216C_REG_ALS_HIGH   0x0D

/* 系统配置: bit[2:0]=001 → ALS 连续模式 */
#define SYS_CONF_ALS_ACTIVE    0x01

#define POLL_INTERVAL_MS       300

static const char *TAG = "ap3216c";
static i2c_master_dev_handle_t s_dev;
static uint16_t s_als;

uint16_t ap3216c_getAls(void)
{
    return s_als;
}

static void ap3216cTask(void *arg)
{
    for (;;) {
        uint8_t buf[2];
        esp_err_t ret = i2cDriver_writeRead(s_dev,
                                            (uint8_t[]){AP3216C_REG_ALS_LOW}, 1,
                                            buf, sizeof(buf), 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "读取 ALS 失败");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint16_t als = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        s_als = als;

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t ap3216c_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = i2cDriver_addDevice(bus, AP3216C_I2C_ADDR, I2C0_SPEED_HZ, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "挂载 AP3216C 失败");
        return ret;
    }

    /* 启动 ALS 连续模式 */
    ret = i2cDriver_write(s_dev, (uint8_t[]){AP3216C_REG_SYS_CONF, SYS_CONF_ALS_ACTIVE}, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 ALS 失败");
        return ret;
    }

    /* 等待首次转换完成 */
    vTaskDelay(pdMS_TO_TICKS(100));

    xTaskCreate(ap3216cTask, "ap3216c", 2560, NULL, 3, NULL);
    ESP_LOGI(TAG, "AP3216C 采集任务已启动");
    return ESP_OK;
}
