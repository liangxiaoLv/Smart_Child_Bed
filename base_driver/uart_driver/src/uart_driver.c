#include "uart_driver.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "uart_driver";

esp_err_t uartDriver_init(int uart_num, int tx_pin, int rx_pin, int baud_rate)
{
    uart_config_t cfg = {
        .baud_rate  = baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(uart_num, 1024, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d 驱动安装失败: %s", uart_num, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(uart_num, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d 参数配置失败: %s", uart_num, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(uart_num, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d 引脚设置失败: %s", uart_num, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d 初始化完成 baud=%d", uart_num, baud_rate);
    return ESP_OK;
}

esp_err_t uartDriver_write(int uart_num, const uint8_t *data, size_t len)
{
    int sent = uart_write_bytes(uart_num, data, len);
    if (sent < 0) {
        ESP_LOGE(TAG, "UART%d 发送失败", uart_num);
        return ESP_FAIL;
    }
    return ESP_OK;
}

int uartDriver_read(int uart_num, uint8_t *buf, size_t len, int timeout_ms)
{
    int rx = uart_read_bytes(uart_num, buf, len,
                             pdMS_TO_TICKS(timeout_ms));
    if (rx < 0) {
        ESP_LOGE(TAG, "UART%d 读取失败", uart_num);
        return -1;
    }
    return rx;  /* 0 = 超时无数据, >0 = 实际读取字节数 */
}

esp_err_t uartDriver_deinit(int uart_num)
{
    return uart_driver_delete(uart_num);
}
