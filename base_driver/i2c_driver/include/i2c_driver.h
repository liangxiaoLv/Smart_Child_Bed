#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* 初始化 I2C 主机总线，port 取值 I2C_NUM_0 / I2C_NUM_1。
   必须在系统启动阶段单线程调用，不可在多个任务中并发调用。 */
esp_err_t i2cDriver_initBus(int port,
                             int sda,
                             int scl,
                             i2c_master_bus_handle_t *bus_out);

/* 在总线上挂载从设备，addr 为 7-bit 设备地址 */
esp_err_t i2cDriver_addDevice(i2c_master_bus_handle_t bus,
                               uint16_t addr,
                               uint32_t speed_hz,
                               i2c_master_dev_handle_t *dev_out);

/* 从总线上移除从设备 */
esp_err_t i2cDriver_removeDevice(i2c_master_dev_handle_t dev);

/* 向设备写入 len 字节 */
esp_err_t i2cDriver_write(i2c_master_dev_handle_t dev,
                           const uint8_t *data,
                           size_t len,
                           uint32_t timeout_ms);

/* 从设备读取 len 字节 */
esp_err_t i2cDriver_read(i2c_master_dev_handle_t dev,
                          uint8_t *buf,
                          size_t len,
                          uint32_t timeout_ms);

/* 先写后读，常用于写寄存器地址再读寄存器值 */
esp_err_t i2cDriver_writeRead(i2c_master_dev_handle_t dev,
                               const uint8_t *wr_data,
                               size_t wr_len,
                               uint8_t *rd_buf,
                               size_t rd_len,
                               uint32_t timeout_ms);

/* 探测总线上某个 7-bit 地址是否有从机应答 (timeout_ms 内) */
esp_err_t i2cDriver_probe(i2c_master_bus_handle_t bus,
                          uint8_t addr,
                          uint32_t timeout_ms);
