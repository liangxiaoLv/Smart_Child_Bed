#include "i2c_driver.h"
#include "esp_log.h"

static const char *TAG = "i2c_driver";

esp_err_t i2cDriver_initBus(int port,
                             int sda,
                             int scl,
                             i2c_master_bus_handle_t *bus_out)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port          = port,
        .sda_io_num        = sda,
        .scl_io_num        = scl,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&cfg, bus_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C%d 总线初始化失败: %s", port, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t i2cDriver_addDevice(i2c_master_bus_handle_t bus,
                               uint16_t addr,
                               uint32_t speed_hz,
                               i2c_master_dev_handle_t *dev_out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = speed_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, dev_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备 0x%02X 添加失败: %s", addr, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t i2cDriver_write(i2c_master_dev_handle_t dev,
                           const uint8_t *data,
                           size_t len,
                           int timeout_ms)
{
    return i2c_master_transmit(dev, data, len, timeout_ms);
}

esp_err_t i2cDriver_read(i2c_master_dev_handle_t dev,
                          uint8_t *buf,
                          size_t len,
                          int timeout_ms)
{
    return i2c_master_receive(dev, buf, len, timeout_ms);
}

esp_err_t i2cDriver_writeRead(i2c_master_dev_handle_t dev,
                               const uint8_t *wr_data,
                               size_t wr_len,
                               uint8_t *rd_buf,
                               size_t rd_len,
                               int timeout_ms)
{
    return i2c_master_transmit_receive(dev, wr_data, wr_len,
                                       rd_buf, rd_len, timeout_ms);
}
