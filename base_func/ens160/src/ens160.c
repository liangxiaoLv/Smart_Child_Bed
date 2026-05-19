#include "ens160.h"
#include "pin_map.h"
#include "i2c_driver.h"
#include "trans_2_cloud.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define ENS160_PART_ID          0x00
#define ENS160_OPMODE           0x10
#define ENS160_TEMP_IN          0x13
#define ENS160_RH_IN            0x15
#define ENS160_DEVICE_STATUS    0x20
#define ENS160_DATA_AQI         0x21
#define ENS160_DATA_TVOC        0x22
#define ENS160_DATA_ECO2        0x24
#define ENS160_DATA_T           0x30
#define ENS160_DATA_RH          0x32
#define ENS160_DATA_MISR        0x38

static const char *TAG = "ens160";

static i2c_master_dev_handle_t ens160_dev;
static uint8_t s_latest_aqi;

uint8_t ens160_getLatestAQI(void) { return s_latest_aqi; }

static esp_err_t writeReg(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[len + 1];
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return i2cDriver_write(ens160_dev, buf, len + 1, 100);
}

static esp_err_t readReg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2cDriver_writeRead(ens160_dev, &reg, 1, data, len, 100);
}

esp_err_t ens160_set_temp(float temp_c)
{
    uint16_t raw = (uint16_t)((temp_c + 273.15f) * 64.0f);
    uint8_t buf[] = {raw & 0xFF, raw >> 8};
    return writeReg(ENS160_TEMP_IN, buf, sizeof(buf));
}

esp_err_t ens160_set_rh(float rh_pct)
{
    uint16_t raw = (uint16_t)(rh_pct * 512.0f);
    uint8_t buf[] = {raw & 0xFF, raw >> 8};
    return writeReg(ENS160_RH_IN, buf, sizeof(buf));
}

static const char *aqiLabel(uint8_t aqi)
{
    switch (aqi) {
    case 1: return "优";
    case 2: return "良";
    case 3: return "中等";
    case 4: return "差";
    case 5: return "劣";
    default: return "未知";
    }
}

static const char *validityLabel(uint8_t v)
{
    switch (v) {
    case 0: return "正常";
    case 1: return "预热";
    case 2: return "启动";
    case 3: return "无效";
    default: return "?";
    }
}

static void ens160Task(void *arg)
{
    for (;;) {
        uint8_t status;
        if (readReg(ENS160_DEVICE_STATUS, &status, 1) != ESP_OK) {
            ESP_LOGE(TAG, "读状态失败");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!(status & 0x02)) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t validity = (status >> 2) & 0x03;

        uint8_t aqi;
        readReg(ENS160_DATA_AQI, &aqi, 1);

        uint8_t tvoc[2];
        readReg(ENS160_DATA_TVOC, tvoc, sizeof(tvoc));

        uint8_t eco2[2];
        readReg(ENS160_DATA_ECO2, eco2, sizeof(eco2));

        uint8_t dt[2];
        readReg(ENS160_DATA_T, dt, sizeof(dt));

        uint8_t drh[2];
        readReg(ENS160_DATA_RH, drh, sizeof(drh));

        uint16_t tvoc_ppb = (uint16_t)tvoc[0] | ((uint16_t)tvoc[1] << 8);
        uint16_t eco2_ppm = (uint16_t)eco2[0] | ((uint16_t)eco2[1] << 8);
        aqi &= 0x07;
        s_latest_aqi = aqi;

        uint16_t t_raw = (uint16_t)dt[0]  | ((uint16_t)dt[1]  << 8);
        uint16_t h_raw = (uint16_t)drh[0] | ((uint16_t)drh[1] << 8);
        float t_c = (float)t_raw / 64.0f - 273.15f;
        float rh  = (float)h_raw / 512.0f;

        ESP_LOGI(TAG, "AQI:%d(%s) TVOC:%d ppb eCO2:%d ppm T:%.1f°C RH:%.1f%% [%s]",
                 aqi, aqiLabel(aqi), tvoc_ppb, eco2_ppm, t_c, rh,
                 validityLabel(validity));

        /* 同步到云端上报模块 */
        trans2cloud_updateAir(aqi, tvoc_ppb, eco2_ppm);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t ens160_info(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = i2cDriver_addDevice(bus, ENS160_I2C_ADDR, I2C1_SPEED_HZ, &ens160_dev);
    if (ret != ESP_OK) return ret;

    uint8_t id[2];
    ret = readReg(ENS160_PART_ID, id, sizeof(id));
    if (ret == ESP_OK) {
        uint16_t part = (uint16_t)id[0] | ((uint16_t)id[1] << 8);
        ESP_LOGI(TAG, "PART_ID: 0x%04X", part);
    } else {
        ESP_LOGW(TAG, "PART_ID 读取失败: %s", esp_err_to_name(ret));
    }

    uint8_t opmode = 0x02;
    ret = writeReg(ENS160_OPMODE, &opmode, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OPMODE 设置失败");
        return ret;
    }

    xTaskCreate(ens160Task, "ens160", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "采集任务已启动");
    return ESP_OK;
}
