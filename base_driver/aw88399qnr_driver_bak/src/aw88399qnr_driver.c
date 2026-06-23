/**
 * AW88399QNR 音频功放驱动
 * ======================
 * 上海艾为 AW88399 智能音频放大器（QNR 封装），I2S 输入 + I2C 控制
 * I2C 地址：0x34（ADDR 脚接 GND）
 *
 * 寄存器协议（datasheet）：
 *   写：[reg_addr 8-bit] [data_hi 8-bit] [data_lo 8-bit]  （3 字节，大端）
 *   读：先写 [reg_addr]，再读 [data_hi] [data_lo]          （2 字节，大端）
 *   CHIPID(0x00) = 0x2183
 *
 * 启动序列（参考 aw883xx_device.c aw883xx_device_start，DSP 旁路模式）：
 *   1. 软复位
 *   2. 安全态（PWDN=1, 默认 0xE307）
 *   3. 配置 I2S 格式 / SYSINTM / I2SCTRL3
 *   4. 清 PWDN，等待 PLL 锁定（SYSST.PLLS=1 & CLKS=1）
 *   5. 清 AMPPD，等待 SWS（SYSST.SWS=1）
 *   6. DSP 旁路（DSPBY=1）
 *   7. 设置音量
 *   8. 清 HMUTE，出音
 */

#include "aw88399qnr_driver.h"
#include "i2c_driver.h"
#include "pin_map.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aw88399qnr";

/* ─── 寄存器地址（datasheet Table 1）────────────────────── */
#define REG_ID          0x00  /* RO:  芯片 ID，= 0x2183 */
#define REG_SYSST       0x01  /* RO:  系统状态 */
#define REG_SYSINT      0x02  /* RC:  系统中断（读清） */
#define REG_SYSINTM     0x03  /* R/W: 中断屏蔽，默认 0xFFFB */
#define REG_SYSCTRL     0x04  /* R/W: 系统控制，默认 0xE307 */
#define REG_SYSCTRL2    0x05  /* R/W: 音量控制，默认 0x8000 */
#define REG_I2SCTRL1    0x06  /* R/W: I2S 配置1，默认 0x04E8 */
#define REG_I2SCTRL3    0x08  /* R/W: I2S 配置3，默认 0x2C01 */

/* ─── SYSST (0x01) ──────────────────────────────────────── */
#define SYSST_PLLS      (1u << 0)   /* PLL 锁定 */
#define SYSST_CLKS      (1u << 4)   /* 时钟稳定 */
#define SYSST_SWS       (1u << 8)   /* 功放切换完成（正在开关机） */

/* PLL 检查掩码：PLLS=1 & CLKS=1 */
#define SYSST_PLL_OK    (SYSST_PLLS | SYSST_CLKS)

/* ─── SYSCTRL (0x04) ────────────────────────────────────── */
#define SYSCTRL_PWDN        (1u << 0)   /* 全局掉电，1=掉电 */
#define SYSCTRL_AMPPD       (1u << 1)   /* 功放掉电，1=掉电 */
#define SYSCTRL_DSPBY       (1u << 2)   /* DSP 旁路，1=旁路(I2S→DAC 直通) */
#define SYSCTRL_I2SEN       (1u << 6)   /* I2S 接口使能，1=使能 */
#define SYSCTRL_HMUTE       (1u << 8)   /* 硬件静音，1=静音 */
#define SYSCTRL_HDCCE       (1u << 9)   /* 直流消除，1=使能 */
#define SYSCTRL_RCV_GAIN    (2u << 12)  /* RCV_GAIN=2（默认） */
#define SYSCTRL_SPK_GAIN    (1u << 14)  /* 扬声器增益，1=12.62Av */
#define SYSCTRL_ULS_HMUTE   (1u << 15)  /* 低压保护静音 */

/* 上电默认值（datasheet）= 0xE307 */
#define SYSCTRL_DEFAULT     0xE307u

/*
 * 工作态：保留高字节功能位，清 HMUTE/AMPPD/PWDN，置 I2SEN/DSPBY
 * = ULS_HMUTE | SPK_GAIN | RCV_GAIN=2 | HDCCE | I2SEN | DSPBY
 * = 0x8000|0x4000|0x2000|0x0200|0x0040|0x0004 = 0xE244
 *
 * 注：与 datasheet 默认值对比：
 *   去掉 HMUTE(0x0100), AMPPD(0x0002), PWDN(0x0001)
 *   加上 I2SEN(0x0040)
 *   保留 ULS_HMUTE/SPK_GAIN/RCV_GAIN/HDCCE/DSPBY
 */
#define SYSCTRL_ACTIVE  (SYSCTRL_ULS_HMUTE | SYSCTRL_SPK_GAIN | SYSCTRL_RCV_GAIN | \
                         SYSCTRL_HDCCE | SYSCTRL_I2SEN | SYSCTRL_DSPBY)

/* ─── SYSINTM (0x03) ───────────────────────────────────── */
/* 默认 0xFFFB，屏蔽除 PLLM(bit0) 以外所有中断，DSP 旁路模式全屏蔽 */
#define SYSINTM_ALL_MASK    0xFFFFu

/* ─── SYSCTRL2 (0x05) — 音量 ─────────────────────────── */
#define SYSCTRL2_EN_MPD     (1u << 15)  /* 过调制保护使能 */
#define VOL_MAX             0x3FFu      /* VOL[9:0]=0x3FF 完全静音 */

/* ─── I2SCTRL1 (0x06) ──────────────────────────────────── */
/*
 * 目标配置：Philips 标准，左声道，16-bit 帧，64×FS BCLK，44.1kHz
 *   CHSEL[11:10]=01(LEFT), I2SMD[9:8]=00(Philips), I2SFS[7:6]=00(16bit)
 *   I2SBCK[5:4]=10(64FS), I2SSR[3:0]=0111(44.1kHz)
 *   = 0x0427
 *
 * 采样率编码（I2SSR[3:0]）：
 *   8k=0, 11k=1, 12k=2, 16k=3, 22k=4, 24k=5, 32k=6, 44.1k=7, 48k=8
 */
#define I2SCTRL1_INIT   0x0427u  /* 16-bit, 64FS, 44.1kHz, LEFT, Philips */

/* ─── I2SCTRL3 (0x08) ──────────────────────────────────── */
/* datasheet 默认 0x2C01：DOHZ=1, DRVSTREN=12mA, I2SRXEN=1 */
#define I2SCTRL3_INIT   0x2C01u

/* ─── 超时参数 ──────────────────────────────────────────── */
#define PLL_CHECK_RETRIES   10  /* PLL 锁定轮询次数 */
#define PLL_CHECK_DELAY_MS  2   /* 每次轮询间隔 */
#define SWS_CHECK_RETRIES   10  /* SWS 轮询次数 */
#define SWS_CHECK_DELAY_MS  1

/* ─── 模块状态 ────────────────────────────────────────────── */
static i2c_master_dev_handle_t s_dev    = NULL;
static bool                    s_inited = false;

/* ─── I2C 16-bit 读写（datasheet 协议：大端，3字节写/2字节读）── */
static esp_err_t reg_write(uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    esp_err_t ret = i2cDriver_write(s_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "W 0x%02X=0x%04X err:%s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t reg_read(uint8_t reg, uint16_t *out)
{
    uint8_t rd[2] = {0, 0};
    esp_err_t ret = i2cDriver_writeRead(s_dev, &reg, 1, rd, 2, 100);
    if (ret == ESP_OK) {
        *out = ((uint16_t)rd[0] << 8) | rd[1];
    } else {
        ESP_LOGE(TAG, "R 0x%02X err:%s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/* 读-改-写：mask 中为1的位不变，mask 中为0的位用 val 覆盖 */
static esp_err_t reg_write_bits(uint8_t reg, uint16_t mask, uint16_t val)
{
    uint16_t cur = 0;
    esp_err_t ret = reg_read(reg, &cur);
    if (ret != ESP_OK) return ret;
    cur = (cur & mask) | (val & ~mask);
    return reg_write(reg, cur);
}
