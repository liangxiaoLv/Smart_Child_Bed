#include "ap3216c_driver.h"
#include "i2c_driver.h"
#include "pin_map.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ap3216c";

#define REG_SYS_CONF        0x00
#define REG_ALS_DATA_LOW    0x0C
#define REG_ALS_DATA_HIGH   0x0D

#define REG_PS_DATA_LOW     0x0E
#define REG_PS_DATA_HIGH    0x0F
#define REG_IR_DATA_LOW     0x10
#define REG_IR_DATA_HIGH    0x11

static i2c_master_dev_handle_t s_dev = NULL;

static uint16_t readReg16(uint8_t regLow)
{
    if (!s_dev) return 0;

    uint8_t data[2];
    if (i2cDriver_writeRead(s_dev, &regLow, 1, data, 2, 100) != ESP_OK) {
        ESP_LOGW(TAG, "寄存器 0x%02X 读取失败", regLow);
        return 0;
    }

    return ((uint16_t)data[1] << 8) | data[0];
}

esp_err_t ap3216cDriver_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    static bool s_inited = false;
    if (s_inited) return ESP_OK;
    s_inited = true;

    ESP_RETURN_ON_ERROR(
        i2cDriver_addDevice(bus, AP3216C_I2C_ADDR, I2C0_SPEED_HZ, &s_dev),
        TAG, "AP3216C 设备添加失败");

    /* 使能 ALS + PS + IR (bits[1:0] = 11) */
    uint8_t buf[2] = { REG_SYS_CONF, 0x03 };
    ESP_RETURN_ON_ERROR(
        i2cDriver_write(s_dev, buf, 2, 100),
        TAG, "AP3216C 配置写入失败");

    ESP_LOGI(TAG, "AP3216C 初始化完成");
    return ESP_OK;
}

uint16_t ap3216cDriver_readALS(void)
{
    return readReg16(REG_ALS_DATA_LOW);
}

uint16_t ap3216cDriver_readPS(void)
{
    return readReg16(REG_PS_DATA_LOW);
}

uint16_t ap3216cDriver_readIR(void)
{
    return readReg16(REG_IR_DATA_LOW);
}
