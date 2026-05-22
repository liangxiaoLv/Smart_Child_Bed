#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t uartDriver_init(int uart_num, int tx_pin, int rx_pin, int baud_rate, int rx_buf_size);
esp_err_t uartDriver_write(int uart_num, const uint8_t *data, size_t len);
int uartDriver_read(int uart_num, uint8_t *buf, size_t len, int timeout_ms);
esp_err_t uartDriver_deinit(int uart_num);
