#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/**
 * 初始化一条 I2C 主机总线。
 *
 * @param port      I2C 端口号（I2C_NUM_0 / I2C_NUM_1）
 * @param sda       SDA GPIO
 * @param scl       SCL GPIO
 * @param bus_out   输出总线句柄，供后续 addDevice 使用
 */
esp_err_t i2cDriver_initBus(int port,
                             int sda,
                             int scl,
                             i2c_master_bus_handle_t *bus_out);

/**
 * 在已有总线上挂载一个从设备。
 *
 * @param bus        总线句柄
 * @param addr       7-bit 设备地址
 * @param speed_hz   SCL 频率（Hz），通常 100000 或 400000
 * @param dev_out    输出设备句柄
 */
esp_err_t i2cDriver_addDevice(i2c_master_bus_handle_t bus,
                               uint16_t addr,
                               uint32_t speed_hz,
                               i2c_master_dev_handle_t *dev_out);

/**
 * 向设备写入数据。
 * @param dev      设备句柄
 * @param data     写缓冲区
 * @param len      字节数
 * @param timeout_ms  超时（毫秒）
 */
esp_err_t i2cDriver_write(i2c_master_dev_handle_t dev,
                           const uint8_t *data,
                           size_t len,
                           int timeout_ms);

/**
 * 从设备读取数据。
 * @param dev      设备句柄
 * @param buf      读缓冲区
 * @param len      字节数
 * @param timeout_ms  超时（毫秒）
 */
esp_err_t i2cDriver_read(i2c_master_dev_handle_t dev,
                          uint8_t *buf,
                          size_t len,
                          int timeout_ms);

/**
 * 先写后读（寄存器读取常用）。
 * @param dev       设备句柄
 * @param wr_data   写缓冲区（通常为寄存器地址）
 * @param wr_len    写字节数
 * @param rd_buf    读缓冲区
 * @param rd_len    读字节数
 * @param timeout_ms  超时（毫秒）
 */
esp_err_t i2cDriver_writeRead(i2c_master_dev_handle_t dev,
                               const uint8_t *wr_data,
                               size_t wr_len,
                               uint8_t *rd_buf,
                               size_t rd_len,
                               int timeout_ms);
