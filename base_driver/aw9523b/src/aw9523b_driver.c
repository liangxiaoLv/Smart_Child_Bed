/*
 * AW9523B 16-bit GPIO Expander 驱动
 * - I2C 地址: 0x5B
 * - INTN (GPIO47) 下降沿触发
 * - RSTN (GPIO48) 低电平复位
 *
 * 设计原则:
 * - 所有 I2C 失败返回错误, 不 panic, 由调用者决定
 * - 中断上下文不调 I2C, 改用 xTaskNotify 通知任务读
 * - init 失败时重置 s_inited, 允许重试
 */

#include "aw9523b_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "aw9523b";

static i2c_master_dev_handle_t s_dev      = NULL;
static aw9523b_isr_t            s_cb       = NULL;
static TaskHandle_t             s_int_task = NULL;
static bool                     s_inited   = false;

/* 输出寄存器镜像 (P0, P1) */
static uint8_t s_out[2] = {0, 0};

/* ── 低层 I2C 读写 ────────────────────────────────────────── */
static esp_err_t writeReg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2cDriver_write(s_dev, buf, 2, 100);
}

static esp_err_t readReg(uint8_t reg, uint8_t *val)
{
    return i2cDriver_writeRead(s_dev, &reg, 1, val, 1, 100);
}

/* (上电时序在 init 头部按规范执行, 不再需要 hwReset 辅助函数) */

/* ── 中断处理任务 (读输入后回调) ────────────────────────── */
static void intnTask(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* 阻塞等 ISR 通知 */
        uint8_t p0 = 0, p1 = 0;
        if (readReg(AW_REG_INPUT_P0, &p0) == ESP_OK &&
            readReg(AW_REG_INPUT_P1, &p1) == ESP_OK) {
            uint16_t levels = ((uint16_t)p1 << 8) | p0;
            if (s_cb) s_cb(levels);
        }
    }
}

/* ── ISR: 仅发通知, 避免在中断里跑 I2C ─────────────────── */
static void IRAM_ATTR intn_isr(void *arg)
{
    (void)arg;
    if (s_int_task) {
        BaseType_t hp = pdFALSE;
        vTaskNotifyGiveFromISR(s_int_task, &hp);
        if (hp) portYIELD_FROM_ISR();
    }
}

/* ── 初始化 ───────────────────────────────────────────────── */
esp_err_t aw9523bDriver_init(i2c_master_bus_handle_t bus, aw9523b_isr_t cb)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_inited) return ESP_OK;

    s_cb = cb;

    /* 配 RSTN, 默认上拉 (无效)
     * AW9523B 上电时序:
     *   1. 芯片上电后 ≥100μs 才能拉高 RSTN
     *   2. RSTN 拉高后 ≥5ms 才能 I2C 通信
     */
    gpio_config_t rst = {
        .pin_bit_mask = (1ULL << AW9523B_RSTN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst));
    /* RSTN 先保持低 (默认上拉无效), 等 ≥100μs 让芯片完成上电 */
    gpio_set_level(AW9523B_RSTN_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));   /* 1ms >> 100μs, 留足裕量 */
    /* 再拉高 RSTN, 使芯片进入工作模式 */
    gpio_set_level(AW9523B_RSTN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  /* 10ms >> 5ms, 留足裕量 */

    /* 之后挂 I2C 设备, 读 ID 等 */

    /* 挂 I2C 设备 */
    esp_err_t err = i2cDriver_addDevice(bus, AW9523B_I2C_ADDR,
                                        I2C0_SPEED_HZ, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设备挂载失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 校验芯片 ID (AW9523B = 0x23) */
    uint8_t id = 0;
    err = readReg(AW9523B_REG_ID, &id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ID 读失败: %s", esp_err_to_name(err));
        return err;
    }
    if (id != 0x23) {
        ESP_LOGW(TAG, "ID 0x%02X (非 0x23, 继续)", id);
    }

    /* 默认所有 IO 为输入 (上拉由芯片内部提供)
     * P1_4/P1_5 除外 — 用于 UART 设备切换, 设为输出 */
    if ((err = writeReg(AW_REG_DIR_P0, 0xFF)) != ESP_OK) return err;
    if ((err = writeReg(AW_REG_DIR_P1, 0xCF)) != ESP_OK) return err;  /* bit4/5=输出 */
    /* 中断使能: 全部允许 */
    if ((err = writeReg(AW_REG_INT_EN_P0, 0xFF)) != ESP_OK) return err;
    if ((err = writeReg(AW_REG_INT_EN_P1, 0xFF)) != ESP_OK) return err;
    /* 初始输出清零 */
    if ((err = writeReg(AW_REG_OUTPUT_P0, 0x00)) != ESP_OK) return err;
    if ((err = writeReg(AW_REG_OUTPUT_P1, 0x00)) != ESP_OK) return err;
    s_out[0] = s_out[1] = 0;

    /* INTN 中断 (可选) */
    if (cb) {
        gpio_config_t intn = {
            .pin_bit_mask = (1ULL << AW9523B_INTN_PIN),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&intn));
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        ESP_ERROR_CHECK(gpio_isr_handler_add(AW9523B_INTN_PIN, intn_isr, NULL));

        xTaskCreate(intnTask, "aw9523b_int", 2048, NULL, 10, &s_int_task);
    }

    s_inited = true;
    ESP_LOGI(TAG, "AW9523B 初始化完成, ID=0x%02X, INTN=%s",
             id, cb ? "使能" : "关闭");
    return ESP_OK;
}

/* ── API ──────────────────────────────────────────────────── */
esp_err_t aw9523bDriver_readAll(uint16_t *levels)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!levels)   return ESP_ERR_INVALID_ARG;
    uint8_t p0 = 0, p1 = 0;
    esp_err_t err;
    if ((err = readReg(AW_REG_INPUT_P0, &p0)) != ESP_OK) return err;
    if ((err = readReg(AW_REG_INPUT_P1, &p1)) != ESP_OK) return err;
    *levels = ((uint16_t)p1 << 8) | p0;
    return ESP_OK;
}

esp_err_t aw9523bDriver_writePort(aw9523b_port_t port, uint8_t value)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    uint8_t reg = (port == AW9523B_PORT0) ? AW_REG_OUTPUT_P0 : AW_REG_OUTPUT_P1;
    s_out[port] = value;
    return writeReg(reg, value);
}

esp_err_t aw9523bDriver_readPort(aw9523b_port_t port, uint8_t *value)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!value)   return ESP_ERR_INVALID_ARG;
    uint8_t reg = (port == AW9523B_PORT0) ? AW_REG_INPUT_P0 : AW_REG_INPUT_P1;
    return readReg(reg, value);
}

esp_err_t aw9523bDriver_setDir(uint16_t pin_mask, bool input)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    uint8_t p0_mask = (uint8_t)(pin_mask & 0xFF);
    uint8_t p1_mask = (uint8_t)((pin_mask >> 8) & 0xFF);
    esp_err_t err;
    uint8_t d0, d1;
    if ((err = readReg(AW_REG_DIR_P0, &d0)) != ESP_OK) return err;
    if ((err = readReg(AW_REG_DIR_P1, &d1)) != ESP_OK) return err;
    if (input) { d0 |= p0_mask; d1 |= p1_mask; }
    else       { d0 &= ~p0_mask; d1 &= ~p1_mask; }
    if ((err = writeReg(AW_REG_DIR_P0, d0)) != ESP_OK) return err;
    return writeReg(AW_REG_DIR_P1, d1);
}

esp_err_t aw9523bDriver_setPin(uint16_t pin_mask, bool level)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    uint8_t p0_mask = (uint8_t)(pin_mask & 0xFF);
    uint8_t p1_mask = (uint8_t)((pin_mask >> 8) & 0xFF);
    if (level) { s_out[0] |= p0_mask; s_out[1] |= p1_mask; }
    else       { s_out[0] &= ~p0_mask; s_out[1] &= ~p1_mask; }
    esp_err_t err;
    if ((err = writeReg(AW_REG_OUTPUT_P0, s_out[0])) != ESP_OK) return err;
    return writeReg(AW_REG_OUTPUT_P1, s_out[1]);
}
