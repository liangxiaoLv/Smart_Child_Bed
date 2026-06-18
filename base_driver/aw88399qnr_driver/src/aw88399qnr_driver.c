/**
 * AW88399QNR 音频功放驱动
 * ======================
 * 上海艾为 (Awinic) 智能音频放大器，I2S 输入 + I2C 控制
 * 寄存器为 8-bit 访问，地址映射参考官方 aw883xx PID_2183 寄存器表
 * CHIPID=0x32 (区别于 AW88399 标准版 0x2183)
 */

#include "aw88399qnr_driver.h"
#include "i2c_driver.h"
#include "pin_map.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aw88399qnr";

/* ─── 寄存器地址（8-bit 访问，地址参考 AW_PID_2183 寄存器表）─── */
#define REG_ID          0x00    /* R:  芯片 ID (CHIPID=0x32) */
#define REG_SYSCTRL     0x04    /* R/W: 系统控制 */
#define REG_SYSCTRL2    0x05    /* R/W: 系统控制2 (含 VOL[9:0], QNR仅低8位可用) */
#define REG_I2SCTRL1    0x06    /* R/W: I2S 配置1 (采样率/位宽/格式) */
#define REG_I2SCTRL3    0x08    /* R/W: I2S 配置3 (TX/RX enable) */
#define REG_DACCFG2     0x0A    /* R/W: DAC 配置2 (旧代码曾误用为 I2SCTRL1) */

/* SYSCTRL (0x04) 位定义 — 按官方 PID_2183 寄存器表 */
#define SYSCTRL_PWDN    (1 << 0)    /* bit0: 全局掉电 (1=掉电) */
#define SYSCTRL_AMPPD   (1 << 1)    /* bit1: 功放掉电 (1=掉电) */
#define SYSCTRL_DSPBY   (1 << 2)    /* bit2: DSP 旁路 (1=旁路, I2S→DAC直通) */
#define SYSCTRL_I2SEN   (1 << 6)    /* bit6: I2S 接口使能 */

/* SYSCTRL 值 */
#define SYSCTRL_SAFE     (SYSCTRL_DSPBY | SYSCTRL_AMPPD | SYSCTRL_PWDN)  /* 0x07 */
/* QNR: bit6 (I2SEN) 只读为0，I2S 自动使能，旁路态只需 DSPBY=1 */
#define SYSCTRL_BYPASS   SYSCTRL_DSPBY  /* 0x04 */

/* I2SCTRL1 (0x06) 配置 — QNR 仅 bit[4:0] 可写 (I2SBCK[0] + I2SSR[3:0]) */
/* BCLK 固定为 32×FS (bit5 只读)，I2SFS 固定为 16-bit (bit[7:6] 只读) */
#define I2SCTRL1_MONO_16BIT_44K1  0x07  /* SR=44.1k (唯一可写部分: I2SSR=7) */

/* I2SCTRL3 (0x08) 默认: I2SRXEN=1, DOHZ=1, I2STXEN=0 */
#define I2SCTRL3_DEFAULT 0x01

/* ─── 音量映射 ────────────────────────────────────────────── */
/* SYSCTRL2 VOL[9:0] 10-bit: 0x000=0dB, 0x3FF=静音
   QNR 版只暴露低 8 位，pct=0 映射到 0xFF */
#define VOL_MAX_8BIT    0xFF

/* ─── 模块状态 ────────────────────────────────────────────── */
static i2c_master_dev_handle_t s_dev    = NULL;
static bool                    s_inited = false;

/* ─── I2C 8-bit 寄存器读写 ──────────────────────────────── */
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    esp_err_t ret = i2cDriver_write(s_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_write 0x%02X=0x%02X 失败: %s",
                 reg, val, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out)
{
    uint8_t rd = 0;
    esp_err_t ret = i2cDriver_writeRead(s_dev, &reg, 1, &rd, 1, 100);
    if (ret == ESP_OK) {
        *out = rd;
    } else {
        ESP_LOGE(TAG, "reg_read 0x%02X 失败: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/* ─── 公开 API ────────────────────────────────────────────── */

esp_err_t aw88399qnr_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_inited) return ESP_OK;

    /* 挂载设备，400kHz */
    esp_err_t ret = i2cDriver_addDevice(bus, AW88399QNR_I2C_ADDR,
                                        400000, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设备挂载失败 addr=0x%02X", AW88399QNR_I2C_ADDR);
        return ret;
    }

    /* 读取 CHIPID（8-bit） */
    uint8_t chip_id = 0;
    ret = reg_read(REG_ID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CHIPID 读取失败，I2C 不通");
        return ret;
    }
    ESP_LOGI(TAG, "CHIPID=0x%02X %s", chip_id,
             chip_id == 0x32 ? "OK" : "(非预期)");

    /* 1. 安全态：掉电 + 功放掉电 + DSP 旁路 = 0x07 */
    reg_write(REG_SYSCTRL, SYSCTRL_SAFE);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 2. I2S 配置：16-bit Philips 标准，Mono，64×FS，44.1kHz */
    reg_write(REG_I2SCTRL1, I2SCTRL1_MONO_16BIT_44K1);

    /* 3. I2SCTRL3：RX使能，TX关闭 */
    reg_write(REG_I2SCTRL3, I2SCTRL3_DEFAULT);

    /* 4. 初始音量 80% */
    uint8_t vol = (uint8_t)((100 - 80) * VOL_MAX_8BIT / 100);  /* = 51 = 0x33 */
    reg_write(REG_SYSCTRL2, vol);

    /* 5. 使能：I2SEN=1, DSPBY=1, AMPPD=0, PWDN=0 */
    ret = reg_write(REG_SYSCTRL, SYSCTRL_BYPASS);  /* 0x44 */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ── 回读关键寄存器 ── */
    uint8_t rb_sys = 0, rb_i2s = 0, rb_sys2 = 0;
    reg_read(REG_SYSCTRL,  &rb_sys);
    reg_read(REG_I2SCTRL1, &rb_i2s);
    reg_read(REG_SYSCTRL2, &rb_sys2);
    ESP_LOGI(TAG, "回读 SYSCTRL=0x%02X I2SCTRL1=0x%02X SYSCTRL2=0x%02X",
             rb_sys, rb_i2s, rb_sys2);

    if (ret == ESP_OK) {
        s_inited = true;
        ESP_LOGI(TAG, "初始化完成");
    }
    return ret;
}

esp_err_t aw88399qnr_deinit(void)
{
    if (!s_inited) return ESP_OK;
    reg_write(REG_SYSCTRL, SYSCTRL_SAFE);  /* 掉电 */
    s_inited = false;
    ESP_LOGI(TAG, "已掉电");
    return ESP_OK;
}

esp_err_t aw88399qnr_setVolume(uint8_t pct)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (pct > 100) pct = 100;

    /* 音量映射: pct=100 → 0x00 (0dB), pct=0 → 0xFF (静音, 8-bit范围) */
    uint8_t vol = (uint8_t)((100 - pct) * VOL_MAX_8BIT / 100);
    return reg_write(REG_SYSCTRL2, vol);
}

bool aw88399qnr_isInited(void)
{
    return s_inited;
}

esp_err_t aw88399qnr_setSampleRate(uint32_t sample_rate)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    uint8_t sr_bits;

    /* I2SCTRL1[3:0] I2SSR 采样率编码 */
    switch (sample_rate) {
    case 8000:  sr_bits = 0x00; break;
    case 11025: sr_bits = 0x01; break;
    case 12000: sr_bits = 0x02; break;
    case 16000: sr_bits = 0x03; break;
    case 22050: sr_bits = 0x04; break;
    case 24000: sr_bits = 0x05; break;
    case 32000: sr_bits = 0x06; break;
    case 44100: sr_bits = 0x07; break;
    case 48000: sr_bits = 0x08; break;
    default:
        ESP_LOGW(TAG, "不支持的采样率: %lu Hz", sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 读-改-写 I2SCTRL1，仅修改低 4 位（采样率），其余位只读 */
    uint8_t cur;
    esp_err_t ret = reg_read(REG_I2SCTRL1, &cur);
    if (ret != ESP_OK) return ret;

    cur = (cur & 0xF0) | (sr_bits & 0x0F);
    ret = reg_write(REG_I2SCTRL1, cur);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S 采样率同步: %lu Hz", sample_rate);
    }
    return ret;
}

/* ─── 诊断：8-bit 寄存器读写验证 ──────────────────────────── */
void aw88399qnr_testWrites(void)
{
    uint8_t v;
    uint8_t r04, r05, r06;

    /* ── CHIPID ── */
    reg_read(REG_ID, &v);
    ESP_LOGI(TAG, "诊断 CHIPID: 0x%02X", v);

    /* ── SYSCTRL (0x04) 读写验证 ── */
    reg_read(REG_SYSCTRL, &r04);
    ESP_LOGI(TAG, "诊断 SYSCTRL 初始: 0x%02X (PWDN=%d AMPPD=%d DSPBY=%d I2SEN=%d)",
             r04, (r04>>0)&1, (r04>>1)&1, (r04>>2)&1, (r04>>6)&1);

    reg_write(REG_SYSCTRL, 0x07);  /* PWDN|AMPPD|DSPBY */
    reg_read(REG_SYSCTRL, &r04);
    ESP_LOGI(TAG, "诊断 写SYSCTRL=0x07 → 读0x%02X %s",
             r04, r04 == 0x07 ? "OK" : "MISMATCH");

    reg_write(REG_SYSCTRL, 0x44);  /* 尝试置 I2SEN=1 + DSPBY=1 */
    reg_read(REG_SYSCTRL, &r04);
    ESP_LOGI(TAG, "诊断 写SYSCTRL=0x44 → 读0x%02X (bit6只读, 预期0x04)",
             r04);

    /* ── SYSCTRL2 (0x05) 读写验证 ── */
    reg_read(REG_SYSCTRL2, &r05);
    ESP_LOGI(TAG, "诊断 SYSCTRL2 初始: 0x%02X", r05);

    reg_write(REG_SYSCTRL2, 0x33);
    reg_read(REG_SYSCTRL2, &r05);
    ESP_LOGI(TAG, "诊断 写SYSCTRL2=0x33 → 读0x%02X %s",
             r05, r05 == 0x33 ? "OK" : "MISMATCH");

    /* ── I2SCTRL1 (0x06) 读写验证 — QNR仅bit[4:0]可写 ── */
    reg_write(REG_I2SCTRL1, 0x27);  /* 尝试写 BCLK=64FS */
    reg_read(REG_I2SCTRL1, &v);
    ESP_LOGI(TAG, "诊断 I2SCTRL1(0x06): 写0x27 → 读0x%02X (bit5只读, 预期0x07)",
             v);

    /* ── I2SCTRL3 (0x08) 读写验证 ── */
    reg_read(REG_I2SCTRL3, &v);
    ESP_LOGI(TAG, "诊断 I2SCTRL3(0x08) 当前值: 0x%02X", v);

    /* ── 对比旧地址 ── */
    reg_read(REG_DACCFG2, &v);
    ESP_LOGI(TAG, "诊断 DACCFG2(0x0A) 当前值: 0x%02X", v);

    /* ── 2字节连续读，确认寄存器间距 ── */
    uint8_t rd2[2];
    uint8_t reg04 = 0x04;
    if (i2cDriver_writeRead(s_dev, &reg04, 1, rd2, 2, 100) == ESP_OK) {
        ESP_LOGI(TAG, "诊断 2B读 0x04-05: [0]=0x%02X [1]=0x%02X", rd2[0], rd2[1]);
    }
}
