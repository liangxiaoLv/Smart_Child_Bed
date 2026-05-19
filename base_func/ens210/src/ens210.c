#include "ens210.h"
#include "ens160.h"
#include "pin_map.h"
#include "i2c_driver.h"
#include "trans_2_cloud.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define ENS210_PART_ID      0x00
#define ENS210_SYS_CTRL     0x10
#define ENS210_SYS_STAT     0x11
#define ENS210_SENS_START   0x22
#define ENS210_T_VAL        0x30

#define ENS210_CONV_MS      130

/* CRC-7: polynomial x^7+x^3+1 (0x89), initial vector 0x7F, over 17-bit payload */
#define CRC7_WIDTH  7
#define CRC7_POLY   0x89
#define CRC7_IVEC   0x7F
#define DATA7_WIDTH 17
#define DATA7_MASK  ((1UL << DATA7_WIDTH) - 1)
#define DATA7_MSB   (1UL << (DATA7_WIDTH - 1))

static const char *TAG = "ens210";

static i2c_master_dev_handle_t ens210_dev;

static uint32_t crc7(uint32_t val)
{
    uint32_t pol = CRC7_POLY;
    pol  = pol << (DATA7_WIDTH - CRC7_WIDTH - 1);
    uint32_t bit = DATA7_MSB;
    val  = val << CRC7_WIDTH;
    bit  = bit  << CRC7_WIDTH;
    pol  = pol  << CRC7_WIDTH;
    val |= CRC7_IVEC;
    while (bit & (DATA7_MASK << CRC7_WIDTH)) {
        if (bit & val) val ^= pol;
        bit >>= 1;
        pol >>= 1;
    }
    return val;
}

static void ens210Task(void *arg)
{
    for (;;) {
        uint8_t wbuf[] = {ENS210_SENS_START, 0x03};
        esp_err_t ret = i2cDriver_write(ens210_dev, wbuf, sizeof(wbuf), 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "触发测量失败");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(ENS210_CONV_MS));

        uint8_t rbuf[6];
        ret = i2cDriver_writeRead(ens210_dev,
                                  (uint8_t[]){ENS210_T_VAL}, 1,
                                  rbuf, sizeof(rbuf), 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "读取数据失败");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint32_t t_val = (uint32_t)rbuf[0]
                       | ((uint32_t)rbuf[1] << 8)
                       | ((uint32_t)rbuf[2] << 16);
        uint32_t h_val = (uint32_t)rbuf[3]
                       | ((uint32_t)rbuf[4] << 8)
                       | ((uint32_t)rbuf[5] << 16);

        uint32_t t_data  = (t_val >> 0)  & 0xFFFF;
        uint32_t t_valid = (t_val >> 16) & 0x1;
        uint32_t t_crc   = (t_val >> 17) & 0x7F;
        bool t_crc_ok = crc7(t_val & 0x1FFFF) == t_crc;

        uint32_t h_data  = (h_val >> 0)  & 0xFFFF;
        uint32_t h_valid = (h_val >> 16) & 0x1;
        uint32_t h_crc   = (h_val >> 17) & 0x7F;
        bool h_crc_ok = crc7(h_val & 0x1FFFF) == h_crc;

        float temp_c = (float)t_data / 64.0f - 273.15f;
        float hum    = (float)h_data / 512.0f;

        ESP_LOGI(TAG, "温度: %.2f °C (valid=%d crc=%d)  湿度: %.2f %% (valid=%d crc=%d)",
                 temp_c, t_valid, t_crc_ok, hum, h_valid, h_crc_ok);

        /* ENS160 温湿度补偿 */
        ens160_set_temp(temp_c);
        ens160_set_rh(hum);

        /* 同步到云端上报模块 */
        trans2cloud_updateEnv(temp_c, hum);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t ens210_temp_info(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = i2cDriver_addDevice(bus, ENS210_I2C_ADDR, I2C1_SPEED_HZ, &ens210_dev);
    if (ret != ESP_OK) return ret;

    /* 读 PART_ID 需要 active 状态: 先关低功耗 → 等 SYS_ACTIVE → 读 → 恢复低功耗 */
    uint8_t cmd;
    cmd = 0x00;
    i2cDriver_write(ens210_dev, (uint8_t[]){ENS210_SYS_CTRL, cmd}, 2, 100);
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t stat;
        if (i2cDriver_writeRead(ens210_dev,
                                (uint8_t[]){ENS210_SYS_STAT}, 1,
                                &stat, 1, 100) == ESP_OK && (stat & 0x01))
            break;
    }
    uint8_t id[2];
    ret = i2cDriver_writeRead(ens210_dev,
                              (uint8_t[]){ENS210_PART_ID}, 1,
                              id, sizeof(id), 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PART_ID: 0x%02X%02X", id[1], id[0]);
    } else {
        ESP_LOGW(TAG, "读取 PART_ID 失败: %s", esp_err_to_name(ret));
    }
    cmd = 0x01;
    i2cDriver_write(ens210_dev, (uint8_t[]){ENS210_SYS_CTRL, cmd}, 2, 100);

    xTaskCreate(ens210Task, "ens210", 3584, NULL, 2, NULL);
    ESP_LOGI(TAG, "采集任务已启动");
    return ESP_OK;
}
