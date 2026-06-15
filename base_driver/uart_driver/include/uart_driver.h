#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t uartDriver_init(int uart_num, int tx_pin, int rx_pin, int baud_rate, int rx_buf_size);
esp_err_t uartDriver_write(int uart_num, const uint8_t *data, size_t len);
int uartDriver_read(int uart_num, uint8_t *buf, size_t len, int timeout_ms);
esp_err_t uartDriver_deinit(int uart_num);

/**
 * 切换 UART 连接的设备 (通过 AW9523B 控制模拟开关)
 * @param uart_num  UART端口号 (1 或 2)
 * @param device    0 = DeviceB (低电平), 1 = DeviceA (高电平)
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t uartDriver_switch_device(int uart_num, int device);
