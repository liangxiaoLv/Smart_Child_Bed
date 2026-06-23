/*
 * aw883xx_driver.c — AW883xx ESP32 主驱动 (adapted from AWINIC aw883xx.c)
 *
 * 职责：
 *     1. I2C/GPIO 硬件抽象适配层
 *     2. 全局单例状态管理
 *     3. 对外 API (aw883xx_init / start / stop / setVolume / testWrites…)
 *     4. DSP 寄存器访问封装
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_map.h"
#include "i2c_driver.h"
#include "aw883xx_driver.h"
#include "aw883xx_base.h"
#include "aw883xx_pid_2183_reg.h"
#include "aw883xx_internal.h"
#include "aw_profile_process.h"
#include "aw_params.h"

/* ═══════════════════════════════════════════════════════════════
 * 驱动版本
 * ═══════════════════════════════════════════════════════════════ */
#define AW883XX_DRIVER_VERSION "v0.5.1-esp32"

/* ═══════════════════════════════════════════════════════════════
 * I2C 重试参数
 * ═══════════════════════════════════════════════════════════════ */
#define AW_I2C_RETRIES          5
#define AW_I2C_RETRY_DELAY_MS   5
#define AW_READ_CHIPID_RETRIES  5
#define AW_CHIPID_RETRY_DELAY   5

/* ─── 全局单例 ─── */
static struct aw883xx_driver s_drv;
#define DRV (&s_drv)

/* ═══════════════════════════════════════════════════════════════
 * I2C GPIO 适配层 (STM32 HAL → ESP-IDF)
 * ═══════════════════════════════════════════════════════════════ */

static int _i2c_write_raw(uint16_t dev_addr, uint8_t reg_addr,
                           uint8_t *pdata, uint16_t len)
{
    (void)dev_addr;

    uint8_t buf[129]; /* 1 reg + max 128 data */
    buf[0] = reg_addr;
    if (len > 128) return -EINVAL;
    memcpy(&buf[1], pdata, len);

    esp_err_t ret = i2cDriver_write(DRV->i2c_dev, buf, len + 1, 100);
    return (ret == ESP_OK) ? 0 : -EIO;
}

static int _i2c_read_raw(uint16_t dev_addr, uint8_t reg_addr,
                          uint8_t *pdata, uint16_t len)
{
    (void)dev_addr;

    esp_err_t ret = i2cDriver_writeRead(DRV->i2c_dev, &reg_addr, 1,
                                         pdata, len, 100);
    return (ret == ESP_OK) ? 0 : -EIO;
}

static void _reset_gpio_ctl(bool state)
{
    gpio_set_level(DRV->reset_pin, state ? 1 : 0);
}

/* ═══════════════════════════════════════════════════════════════
 * I2C 基础读写（带重试）
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_drv_i2c_writes(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint8_t *buf, uint16_t len)
{
    (void)drv;
    int ret = _i2c_write_raw(0, reg_addr, buf, len);
    if (ret < 0)
        aw_dev_err(DRV->dev_index, "i2c writes fail: reg=0x%02X len=%u", reg_addr, len);
    return ret;
}

int aw883xx_drv_i2c_write(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t reg_data)
{
    (void)drv;
    int ret, cnt;
    uint8_t buf[2] = { (uint8_t)(reg_data >> 8), (uint8_t)(reg_data & 0xFF) };

    for (cnt = 0; cnt < AW_I2C_RETRIES; cnt++) {
        ret = _i2c_write_raw(0, reg_addr, buf, 2);
        if (ret >= 0) break;
        aw_dev_err(DRV->dev_index, "i2c_write retry %d: reg=0x%02X", cnt, reg_addr);
        AW_MS_DELAY(AW_I2C_RETRY_DELAY_MS);
    }
    return ret;
}

int aw883xx_drv_i2c_read(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t *reg_data)
{
    (void)drv;
    int ret, cnt;
    uint8_t buf[2] = {0, 0};

    for (cnt = 0; cnt < AW_I2C_RETRIES; cnt++) {
        ret = _i2c_read_raw(0, reg_addr, buf, 2);
        if (ret >= 0) {
            *reg_data = ((uint16_t)buf[0] << 8) | buf[1];
            break;
        }
        aw_dev_err(DRV->dev_index, "i2c_read retry %d: reg=0x%02X", cnt, reg_addr);
        AW_MS_DELAY(AW_I2C_RETRY_DELAY_MS);
    }
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 * 寄存器读写封装（带 mutex）
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_drv_reg_write(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t reg_data)
{
    int ret;
    aw_mutex_lock();
    ret = aw883xx_drv_i2c_write(drv, reg_addr, reg_data);
    aw_mutex_unlock();
    if (ret < 0)
        aw_dev_err(drv->dev_index, "reg_write 0x%02X=0x%04X fail", reg_addr, reg_data);
    return ret;
}

int aw883xx_drv_reg_read(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t *reg_data)
{
    int ret;
    aw_mutex_lock();
    ret = aw883xx_drv_i2c_read(drv, reg_addr, reg_data);
    aw_mutex_unlock();
    if (ret < 0)
        aw_dev_err(drv->dev_index, "reg_read 0x%02X fail", reg_addr);
    return ret;
}

int aw883xx_drv_reg_write_bits(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t mask, uint16_t reg_data)
{
    int ret;
    uint16_t cur = 0;

    aw_mutex_lock();
    ret = aw883xx_drv_i2c_read(drv, reg_addr, &cur);
    if (ret >= 0) {
        cur = (cur & mask) | (reg_data & ~mask);
        ret = aw883xx_drv_i2c_write(drv, reg_addr, cur);
    }
    aw_mutex_unlock();
    if (ret < 0)
        aw_dev_err(drv->dev_index, "reg_wbits 0x%02X fail", reg_addr);
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 * DSP 读写封装
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_drv_dsp_write(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t dsp_data, uint8_t data_type)
{
    int ret;
    struct aw_dsp_mem_desc *dsp = &drv->aw_pa->dsp_mem_desc;

    aw_mutex_lock();

    /* 设 DSP 地址 */
    ret = aw883xx_drv_i2c_write(drv, dsp->dsp_madd_reg, dsp_addr);
    if (ret < 0) goto exit;

    if (data_type == AW_DSP_16_DATA) {
        ret = aw883xx_drv_i2c_write(drv, dsp->dsp_mdat_reg, (uint16_t)dsp_data);
    } else if (data_type == AW_DSP_32_DATA) {
        ret = aw883xx_drv_i2c_write(drv, dsp->dsp_mdat_reg, (uint16_t)(dsp_data & 0xFFFF));
        if (ret >= 0)
            ret = aw883xx_drv_i2c_write(drv, dsp->dsp_mdat_reg, (uint16_t)(dsp_data >> 16));
    } else {
        aw_dev_err(drv->dev_index, "bad data_type=%u", data_type);
        ret = -EINVAL;
    }

exit:
    aw_mutex_unlock();
    return ret;
}

int aw883xx_drv_dsp_read(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t *dsp_data, uint8_t data_type)
{
    int ret;
    uint16_t lo = 0, hi = 0;
    struct aw_dsp_mem_desc *dsp = &drv->aw_pa->dsp_mem_desc;

    aw_mutex_lock();

    ret = aw883xx_drv_i2c_write(drv, dsp->dsp_madd_reg, dsp_addr);
    if (ret < 0) goto exit;

    if (data_type == AW_DSP_16_DATA) {
        ret = aw883xx_drv_i2c_read(drv, dsp->dsp_mdat_reg, &lo);
        *dsp_data = lo;
    } else if (data_type == AW_DSP_32_DATA) {
        ret = aw883xx_drv_i2c_read(drv, dsp->dsp_mdat_reg, &lo);
        if (ret >= 0)
            ret = aw883xx_drv_i2c_read(drv, dsp->dsp_mdat_reg, &hi);
        *dsp_data = ((uint32_t)hi << 16) | lo;
    } else {
        aw_dev_err(drv->dev_index, "bad data_type=%u", data_type);
        ret = -EINVAL;
    }

exit:
    aw_mutex_unlock();
    return ret;
}

int aw883xx_drv_dsp_write_bits(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t dsp_mask, uint32_t dsp_data, uint8_t data_type)
{
    int ret;
    uint32_t cur = 0;

    ret = aw883xx_drv_dsp_read(drv, dsp_addr, &cur, data_type);
    if (ret < 0) return ret;

    cur = (cur & dsp_mask) | (dsp_data & ~dsp_mask);
    return aw883xx_drv_dsp_write(drv, dsp_addr, cur, data_type);
}

/* ═══════════════════════════════════════════════════════════════
 * 杂项辅助
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_drv_get_dev_num(void) { return 1; }

int aw883xx_drv_get_version(char *buf, int size)
{
    if (size > (int)strlen(AW883XX_DRIVER_VERSION)) {
        memcpy(buf, AW883XX_DRIVER_VERSION, strlen(AW883XX_DRIVER_VERSION));
        return (int)strlen(AW883XX_DRIVER_VERSION);
    }
    return -ENOMEM;
}

/* ═══════════════════════════════════════════════════════════════
 * 硬件复位 + CHIPID 读取
 * ═══════════════════════════════════════════════════════════════ */

void aw883xx_drv_hw_reset(struct aw883xx_driver *drv)
{
    aw_dev_info(drv->dev_index, "HW reset: RSTN=0");
    gpio_set_level(drv->reset_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    gpio_set_level(drv->reset_pin, 1);
    aw_dev_info(drv->dev_index, "HW reset: RSTN=1");
    vTaskDelay(pdMS_TO_TICKS(2));
}

static int _read_chipid(void)
{
    int cnt;
    uint16_t reg_val = 0;

    for (cnt = 0; cnt < AW_READ_CHIPID_RETRIES; cnt++) {
        if (aw883xx_drv_i2c_read(DRV, AW_PID_2183_ID_REG, &reg_val) >= 0) {
            aw_dev_info(DRV->dev_index, "CHIPID=0x%04X", reg_val);
            if (reg_val == AW_PID_2183_IDCODE_DEFAULT_VALUE) {
                DRV->chip_id = reg_val;
                return 0;
            }
            aw_dev_err(DRV->dev_index, "CHIPID mismatch: got 0x%04X, expect 0x%04X",
                       reg_val, AW_PID_2183_IDCODE_DEFAULT_VALUE);
            return -ENODEV;
        }
        AW_MS_DELAY(AW_CHIPID_RETRY_DELAY);
    }
    aw_dev_err(DRV->dev_index, "CHIPID read timeout");
    return -EIO;
}

/* ═══════════════════════════════════════════════════════════════
 * 对外 API 实现
 * ═══════════════════════════════════════════════════════════════ */

/* ─── aw883xx_init ─── */
esp_err_t aw883xx_init(i2c_master_bus_handle_t bus)
{
    esp_err_t ret_esp;
    int ret;

    if (DRV->inited) {
        ESP_LOGW("aw883xx", "already inited");
        return ESP_OK;
    }

    memset(DRV, 0, sizeof(*DRV));

    /* 1. 挂载 I2C 设备 */
    ret_esp = i2cDriver_addDevice(bus, AW88399QNR_I2C_ADDR, I2C0_SPEED_HZ, &DRV->i2c_dev);
    if (ret_esp != ESP_OK) {
        ESP_LOGE("aw883xx", "I2C add device fail: %s", esp_err_to_name(ret_esp));
        return ret_esp;
    }

    /* 2. 配置 RSTN GPIO */
    DRV->reset_pin = AW88399_PA_RSTN_PIN;
    gpio_config_t io_cfg = {
        .pin_bit_mask = ((uint64_t)1 << AW88399_PA_RSTN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level(AW88399_PA_RSTN_PIN, 1);

    DRV->dev_index = AW_DEV_0;
    DRV->i2c_addr  = AW88399QNR_I2C_ADDR;

    /* 3. 硬件复位 */
    aw883xx_drv_hw_reset(DRV);

    /* 4. CHIPID 校验 */
    ret = _read_chipid();
    if (ret < 0) {
        ESP_LOGE("aw883xx", "CHIPID check fail: %d", ret);
        goto fail_i2c;
    }

    /* 5. 填充 init_info */
    DRV->init_info.dev       = AW_DEV_0;
    DRV->init_info.i2c_addr  = AW88399QNR_I2C_ADDR;
    DRV->init_info.re_min    = 2000;
    DRV->init_info.re_max    = 39000;
    DRV->init_info.prof_info = g_dev0_prof_info;
    DRV->init_info.i2c_read_func  = _i2c_read_raw;
    DRV->init_info.i2c_write_func = _i2c_write_raw;
    DRV->init_info.reset_gpio_ctl = _reset_gpio_ctl;
    DRV->init_info.dev_init_ops   = (int (*)(void *))aw883xx_pid_2183_dev_init;

    /* 6. 设备初始化（注册描述符 + 固件加载）*/
    ret = aw883xx_pid_2183_dev_init(DRV);
    if (ret < 0) {
        ESP_LOGE("aw883xx", "pid_2183 init fail: %d", ret);
        goto fail_dealloc;
    }

    DRV->inited = true;
    ESP_LOGI("aw883xx", "init ok: CHIPID=0x%04lX, prof=%s",
             (unsigned long)DRV->chip_id,
             aw883xx_dev_get_profile_name(DRV->aw_pa));
    return ESP_OK;

fail_dealloc:
    if (DRV->aw_pa) { free(DRV->aw_pa); DRV->aw_pa = NULL; }
fail_i2c:
    if (DRV->i2c_dev) { i2cDriver_removeDevice(DRV->i2c_dev); DRV->i2c_dev = NULL; }
    return ESP_FAIL;
}

/* ─── aw883xx_deinit ─── */
esp_err_t aw883xx_deinit(void)
{
    if (!DRV->inited) return ESP_OK;

    if (DRV->aw_pa) {
        aw883xx_device_stop(DRV->aw_pa);
        free(DRV->aw_pa);
        DRV->aw_pa = NULL;
    }

    if (DRV->i2c_dev) {
        i2cDriver_removeDevice(DRV->i2c_dev);
        DRV->i2c_dev = NULL;
    }

    DRV->inited = false;
    ESP_LOGI("aw883xx", "deinit done");
    return ESP_OK;
}

/* ─── aw883xx_start ─── */
esp_err_t aw883xx_start(void)
{
    if (!DRV->inited || !DRV->aw_pa) {
        ESP_LOGW("aw883xx", "not inited");
        return ESP_ERR_INVALID_STATE;
    }

    /* 如果场景切换了，先更新固件 */
    int ret = aw883xx_device_fw_update(DRV->aw_pa, false, false);
    if (ret < 0) {
        aw_dev_err(DRV->dev_index, "fw_update before start fail");
        return ESP_FAIL;
    }

    ret = aw883xx_device_start(DRV->aw_pa);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

/* ─── aw883xx_stop ─── */
esp_err_t aw883xx_stop(void)
{
    if (!DRV->inited || !DRV->aw_pa)
        return ESP_ERR_INVALID_STATE;

    int ret = aw883xx_device_stop(DRV->aw_pa);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

/* ─── aw883xx_setVolume ─── */
esp_err_t aw883xx_setVolume(uint8_t pct)
{
    if (!DRV->inited || !DRV->aw_pa)
        return ESP_ERR_INVALID_STATE;

    if (pct > 100) pct = 100;

    /* AW88399 音量映射：VOL = 0 (最大) ~ 1023 (静音) */
    uint32_t vol = (uint32_t)((100 - pct) * AW_PID_2183_MUTE_VOLUME / 100);

    aw883xx_device_params(DRV->aw_pa, AW_DEV_VOLUME_PARAMS,
                           &vol, sizeof(vol), AW_SET_DEV_PARAMS);

    aw_dev_info(DRV->dev_index, "volume=%u%% (reg=%lu)", pct, (unsigned long)vol);
    return ESP_OK;
}

/* ─── aw883xx_setSampleRate ─── */
esp_err_t aw883xx_setSampleRate(uint32_t sample_rate)
{
    if (!DRV->inited || !DRV->aw_pa)
        return ESP_ERR_INVALID_STATE;

    uint16_t sr_code;
    switch (sample_rate) {
    case 8000:  sr_code = 0; break;
    case 11025: sr_code = 1; break;
    case 12000: sr_code = 2; break;
    case 16000: sr_code = 3; break;
    case 22050: sr_code = 4; break;
    case 24000: sr_code = 5; break;
    case 32000: sr_code = 6; break;
    case 44100: sr_code = 7; break;
    case 48000: sr_code = 8; break;
    default:
        ESP_LOGW("aw883xx", "sample_rate %lu not in table, using nearest",
                 (unsigned long)sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = aw883xx_drv_reg_write_bits(DRV, AW_PID_2183_I2SCTRL1_REG,
        AW_PID_2183_I2SSR_MASK,
        sr_code << AW_PID_2183_I2SSR_START_BIT);

    aw_dev_info(DRV->dev_index, "sample_rate=%lu Hz (code=%u)", (unsigned long)sample_rate, sr_code);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

/* ─── aw883xx_setProfile ─── */
esp_err_t aw883xx_setProfile(const char *name)
{
    if (!DRV->inited || !DRV->aw_pa)
        return ESP_ERR_INVALID_STATE;

    int ret = aw883xx_dev_set_profile_name(DRV->aw_pa, name);
    if (ret < 0) return ESP_ERR_NOT_FOUND;

    /* 如果正在播放，重启以加载新场景 */
    if (DRV->aw_pa->status == AW_DEV_PW_ON) {
        aw883xx_device_stop(DRV->aw_pa);
        ret = aw883xx_device_fw_update(DRV->aw_pa, false, true);
        if (ret >= 0)
            ret = aw883xx_device_start(DRV->aw_pa);
    }

    aw_dev_info(DRV->dev_index, "profile=%s", name);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

/* ─── aw883xx_getProfile ─── */
const char *aw883xx_getProfile(void)
{
    if (!DRV->inited || !DRV->aw_pa) return "none";
    return aw883xx_dev_get_profile_name(DRV->aw_pa);
}

/* ─── aw883xx_isInited ─── */
bool aw883xx_isInited(void) { return DRV->inited; }

/* ─── aw883xx_getFwVersion ─── */
esp_err_t aw883xx_getFwVersion(uint32_t *ver)
{
    if (!DRV->inited || !DRV->aw_pa) return ESP_ERR_INVALID_STATE;

    aw883xx_device_params(DRV->aw_pa, AW_DEV_DSP_PARAMS,
                           ver, sizeof(*ver), AW_GET_DEV_PARAMS);
    return ESP_OK;
}

/* ─── aw883xx_testWrites ─── */
void aw883xx_testWrites(void)
{
    if (!DRV->inited || !DRV->aw_pa) {
        ESP_LOGW("aw883xx", "not inited, skip test");
        return;
    }

    uint16_t chipid, sysst, sysctrl, sysctrl2, i2sctrl1, i2sctrl3;
    uint32_t fw_ver = 0;

    aw883xx_drv_i2c_read(DRV, AW_PID_2183_ID_REG, &chipid);
    aw883xx_drv_i2c_read(DRV, AW_PID_2183_SYSST_REG, &sysst);
    aw883xx_drv_i2c_read(DRV, AW_PID_2183_SYSCTRL_REG, &sysctrl);
    aw883xx_drv_i2c_read(DRV, AW_PID_2183_SYSCTRL2_REG, &sysctrl2);
    aw883xx_drv_i2c_read(DRV, AW_PID_2183_I2SCTRL1_REG, &i2sctrl1);
    aw883xx_drv_i2c_read(DRV, AW_PID_2183_I2SCTRL3_REG, &i2sctrl3);
    aw883xx_getFwVersion(&fw_ver);

    ESP_LOGI("aw883xx", "=== AW88399QNR Diagnostic ===");
    ESP_LOGI("aw883xx", "  CHIPID    = 0x%04X", chipid);
    ESP_LOGI("aw883xx", "  SYSST     = 0x%04X (PLLS=%d CLKS=%d SWS=%d)",
             sysst,
             (sysst & AW_PID_2183_PLLS_LOCKED_VALUE) ? 1 : 0,
             (sysst & AW_PID_2183_CLKS_STABLE_VALUE) ? 1 : 0,
             (sysst & AW_PID_2183_SWS_SWITCHING_VALUE) ? 1 : 0);
    ESP_LOGI("aw883xx", "  SYSCTRL   = 0x%04X (PWDN=%d AMPPD=%d DSPBY=%d HMUTE=%d)",
             sysctrl,
             (sysctrl & AW_PID_2183_PWDN_POWER_DOWN_VALUE) ? 1 : 0,
             (sysctrl & AW_PID_2183_AMPPD_POWER_DOWN_VALUE) ? 1 : 0,
             (sysctrl & AW_PID_2183_DSPBY_BYPASS_VALUE) ? 1 : 0,
             (sysctrl & AW_PID_2183_HMUTE_ENABLE_VALUE) ? 1 : 0);
    ESP_LOGI("aw883xx", "  SYSCTRL2  = 0x%04X", sysctrl2);
    ESP_LOGI("aw883xx", "  I2SCTRL1  = 0x%04X", i2sctrl1);
    ESP_LOGI("aw883xx", "  I2SCTRL3  = 0x%04X", i2sctrl3);
    ESP_LOGI("aw883xx", "  FW_VER    = 0x%04lX", (unsigned long)fw_ver);
    ESP_LOGI("aw883xx", "  STATUS    = %s", (DRV->aw_pa->status == AW_DEV_PW_ON) ? "ON" : "OFF");
    ESP_LOGI("aw883xx", "  PROFILE   = %s", aw883xx_getProfile());
    ESP_LOGI("aw883xx", "===============================");
}
