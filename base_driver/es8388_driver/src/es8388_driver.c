#include "es8388_driver.h"
#include "i2c_driver.h"
#include "pin_map.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es8388";

static i2c_master_dev_handle_t s_dev;
static bool s_inited;

static esp_err_t writeReg(uint8_t reg, uint8_t val)
{
    return i2cDriver_write(s_dev, (uint8_t[]){reg, val}, 2, 100);
}

esp_err_t es8388Driver_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_inited) return ESP_OK;

    esp_err_t ret = i2cDriver_addDevice(bus, ES8388_I2C_ADDR, I2C0_SPEED_HZ, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 设备添加失败");
        return ret;
    }

    /* 软复位 */
    writeReg(0x00, 0x80);
    writeReg(0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 电源管理: 模拟/数字模块上电 */
    writeReg(0x01, 0x58);
    writeReg(0x01, 0x50);
    writeReg(0x02, 0xF3);
    writeReg(0x02, 0xF0);

    writeReg(0x03, 0x09);       /* 关闭麦克风偏置 */
    writeReg(0x00, 0x06);       /* 使能参考电压 */
    writeReg(0x04, 0x3C);       /* DAC 输出使能: LOUT1 + LOUT2 */
    writeReg(0x08, 0x00);       /* MCLK 不分频 */
    writeReg(0x2B, 0x80);       /* DACLRC 与 ADCLRC 相同 */

    /* DAC 通路: 16bit, MCLK/fs=256 */
    writeReg(0x17, 0x18);       /* DAC 数据 16bit, I2S 格式 */
    writeReg(0x18, 0x02);       /* MCLK/采样率 = 256 */
    writeReg(0x1A, 0x00);       /* DAC 左音量不衰减 */
    writeReg(0x1B, 0x00);       /* DAC 右音量不衰减 */

    /* 混频器: DACL → LOUT1, DACR → ROUT1 */
    writeReg(0x27, 0xB8);
    writeReg(0x2A, 0xB8);

    /* 喇叭音量默认值 (0=最小 33=最大) */
    writeReg(0x30, 33);
    writeReg(0x31, 33);

    /* DAC 上电, ADC 关闭 */
    writeReg(0x02, 0x0A);

    vTaskDelay(pdMS_TO_TICKS(100));

    s_inited = true;
    ESP_LOGI(TAG, "ES8388 初始化完成");
    return ESP_OK;
}

esp_err_t es8388Driver_deinit(void)
{
    if (!s_inited) return ESP_OK;
    writeReg(0x02, 0xFF);       /* 复位并暂停 */
    s_inited = false;
    ESP_LOGI(TAG, "ES8388 已复位");
    return ESP_OK;
}

esp_err_t es8388Driver_setVolume(uint8_t vol)
{
    if (vol > 33) vol = 33;
    writeReg(0x30, vol);
    writeReg(0x31, vol);
    return ESP_OK;
}

esp_err_t es8388Driver_setHPVolume(uint8_t vol)
{
    if (vol > 33) vol = 33;
    writeReg(0x2E, vol);
    writeReg(0x2F, vol);
    return ESP_OK;
}
