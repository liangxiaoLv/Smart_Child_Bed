#include "xl9555_driver.h"
#include "i2c_driver.h"
#include "esp_check.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "xl9555_driver";

static i2c_master_dev_handle_t s_dev = NULL;

/* 输出寄存器镜像，避免读-改-写时序问题 */
static uint8_t s_out[2] = { 0x0C, 0x0F };  /* Port0: SPK_EN|BEEP=H; Port1: LCD_BL|CT_RST|SLCD_RST|SLCD_PWR=H */

static esp_err_t writeReg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2cDriver_write(s_dev, buf, 2, 100);
}

static esp_err_t readReg(uint8_t reg, uint8_t *val)
{
    return i2cDriver_writeRead(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t xl9555Driver_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    static bool s_inited = false;
    if (s_inited) return ESP_OK;
    s_inited = true;

    ESP_RETURN_ON_ERROR(
        i2cDriver_addDevice(bus, XL9555_I2C_ADDR, I2C0_SPEED_HZ, &s_dev),
        TAG, "XL9555 设备添加失败");

    /* 方向寄存器：Port-0 P0.0/P0.1 输入，其余输出 */
    ESP_RETURN_ON_ERROR(writeReg(XL9555_REG_CFG0, 0x03), TAG, "CFG0 写入失败");
    /* 方向寄存器：Port-1 P1.7-P1.4 输入，其余输出 */
    ESP_RETURN_ON_ERROR(writeReg(XL9555_REG_CFG1, 0xF0), TAG, "CFG1 写入失败");

    /* 初始输出：SPK_EN(P0.2)=H 关闭扬声器，BEEP(P0.3)=H 关闭蜂鸣器 */
    ESP_RETURN_ON_ERROR(writeReg(XL9555_REG_OUTPUT0, s_out[0]), TAG, "OUT0 写入失败");
    ESP_RETURN_ON_ERROR(writeReg(XL9555_REG_OUTPUT1, s_out[1]), TAG, "OUT1 写入失败");

    ESP_LOGI(TAG, "XL9555 初始化完成");
    return ESP_OK;
}

esp_err_t xl9555Driver_writePort(xl9555_port_t port, uint8_t value)
{
    uint8_t reg = (port == XL9555_PORT0) ? XL9555_REG_OUTPUT0 : XL9555_REG_OUTPUT1;
    s_out[port] = value;
    return writeReg(reg, value);
}

esp_err_t xl9555Driver_readPort(xl9555_port_t port, uint8_t *value)
{
    uint8_t reg = (port == XL9555_PORT0) ? XL9555_REG_INPUT0 : XL9555_REG_INPUT1;
    return readReg(reg, value);
}

esp_err_t xl9555Driver_setPin(xl9555_port_t port, uint8_t mask, bool level)
{
    if (level) {
        s_out[port] |= mask;
    } else {
        s_out[port] &= ~mask;
    }
    uint8_t reg = (port == XL9555_PORT0) ? XL9555_REG_OUTPUT0 : XL9555_REG_OUTPUT1;
    return writeReg(reg, s_out[port]);
}

bool xl9555Driver_getPin(xl9555_port_t port, uint8_t mask)
{
    uint8_t val = 0xFF;
    xl9555Driver_readPort(port, &val);
    return (val & mask) != 0;
}
