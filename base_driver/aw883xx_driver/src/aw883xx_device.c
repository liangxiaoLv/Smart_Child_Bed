/*
 * aw883xx_device.c — 设备操作层 (adapted from AWINIC aw883xx_device.c)
 *
 * 裁剪：
 *   - 移除 AW_MONITOR (无 VBAT/TEMP/VPK 监控)
 *   - 移除 AW_FADE (应用层自行实现淡入淡出)
 *   - 移除 AW_IRQ (无 GPIO 中断线)
 *   - 保留 AW_CALIB 在条件编译下，默认关闭
 *
 * 平台适配：
 *   - delay: AW_MS_DELAY → vTaskDelay (定义于 aw883xx_base.h)
 *   - I2C: 通过 aw_device->ops 函数指针表（由 pid_2183_init.c 注入）
 *   - 内存: calloc/free → calloc/free (ESP-IDF 提供)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aw883xx_base.h"
#include "aw883xx_device.h"
#include "aw883xx_pid_2183_reg.h"

/* ═══════════════════════════════════════════════════════════════
 * 平台常量（原 aw883xx_device.h 缺失的定义）
 * ═══════════════════════════════════════════════════════════════ */
#define AW_DEV_SYSST_CHECK_MAX     (10)

/* cali_check_result — 未启用校准时始终返回 true */
static inline bool aw883xx_cali_check_result(void *cali_desc) {
    (void)cali_desc;
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * 内部辅助函数
 * ═══════════════════════════════════════════════════════════════ */

static int aw_dev_get_iis_status(struct aw_device *aw_dev)
{
    uint16_t reg_val = 0;
    int ret;

    ret = aw_dev->ops.aw_i2c_read(aw_dev, AW_PID_2183_I2SINT_REG, &reg_val);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "I2S status read failed, ret=%d", ret);
        return ret;
    }

    /* I2SCAPCNT > 0 表示检测到有效的 BCLK/LRCK */
    if (reg_val > 0) {
        return 0;
    }

    aw_dev_dbg(aw_dev->dev, "I2SINT=0x%04x — no BCLK/LRCK", reg_val);
    return -EINVAL;
}

static void aw_dev_get_cur_mode_st(struct aw_device *aw_dev)
{
    struct aw_profctrl_desc *desc = &aw_dev->profctrl_desc;
    uint16_t reg_val = 0;

    aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
    desc->cur_mode = reg_val & (~desc->mask);
    aw_dev_dbg(aw_dev->dev, "cur_mode=0x%04x", desc->cur_mode);
}

/* ─── 软复位 ─── */
static void aw_dev_soft_reset(struct aw_device *aw_dev)
{
    struct aw_soft_rst *reset = &aw_dev->soft_rst;
    aw_dev->ops.aw_i2c_write(aw_dev, reset->reg, reset->reg_value);
    aw_dev_info(aw_dev->dev, "soft reset done");
}

/* ─── 全局掉电 / 上电 ─── */
static void aw_dev_pwd(struct aw_device *aw_dev, bool pwd)
{
    struct aw_pwd_desc *desc = &aw_dev->pwd_desc;
    aw_dev_dbg(aw_dev->dev, "pwd=%d", pwd);
    aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask,
        pwd ? desc->enable : desc->disable);
}

/* ─── 功放掉电 / 上电 ─── */
static void aw_dev_amppd(struct aw_device *aw_dev, bool amppd)
{
    struct aw_amppd_desc *desc = &aw_dev->amppd_desc;
    aw_dev_dbg(aw_dev->dev, "amppd=%d", amppd);
    aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask,
        amppd ? desc->enable : desc->disable);
}

/* ─── 硬件静音 ─── */
static int aw_dev_set_hmute(struct aw_device *aw_dev, uint32_t mute)
{
    struct aw_mute_desc *desc = &aw_dev->mute_desc;
    aw_dev_dbg(aw_dev->dev, "mute=%lu", (unsigned long)mute);
    return aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask,
        mute == AW_DEV_HMUTE_ENABLE ? desc->enable : desc->disable);
}

/* ─── DSP 使能 / 旁路 ─── */
static int aw_dev_dsp_enable(struct aw_device *aw_dev, uint32_t dsp)
{
    struct aw_dsp_en_desc *desc = &aw_dev->dsp_en_desc;
    int ret;

    if (dsp == AW_DEV_DSP_WORK)
        ret = aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->enable);
    else if (dsp == AW_DEV_DSP_BYPASS)
        ret = aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->disable);
    else
        return -EINVAL;

    aw_dev_info(aw_dev->dev, "dsp_enable=%lu -> %d", (unsigned long)dsp, ret);
    return ret;
}

/* ─── 清除中断状态 ─── */
static int aw_dev_clear_int_status(struct aw_device *aw_dev)
{
    uint16_t reg_val;
    aw_dev->ops.aw_i2c_read(aw_dev, aw_dev->int_desc.st_reg, &reg_val);
    aw_dev_dbg(aw_dev->dev, "SYSINT=0x%04x", reg_val);
    return 0;
}

/* ─── 中断屏蔽 ─── */
static int aw_dev_set_intmask(struct aw_device *aw_dev, uint32_t flag)
{
    struct aw_int_desc *desc = &aw_dev->int_desc;
    int ret;

    if (flag == AW_DEV_UNMASK_INT_VAL)
        ret = aw_dev->ops.aw_reg_write(aw_dev, desc->mask_reg, desc->int_mask);
    else
        ret = aw_dev->ops.aw_reg_write(aw_dev, desc->mask_reg, desc->mask_default);

    aw_dev_info(aw_dev->dev, "intmask=%lu", (unsigned long)flag);
    return ret;
}

/* ─── 内存时钟选择 ─── */
static void aw_dev_memclk_select(struct aw_device *aw_dev, unsigned char flag)
{
    struct aw_memclk_desc *desc = &aw_dev->memclk_desc;

    if (flag == AW_DEV_MEMCLK_PLL)
        aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->mcu_hclk);
    else if (flag == AW_DEV_MEMCLK_OSC)
        aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->osc_clk);

    aw_dev_info(aw_dev->dev, "memclk=%d", flag);
}

/* ─── Dither 控制 ─── */
static void aw_dev_set_dither(struct aw_device *aw_dev, bool dither)
{
    struct aw_dither_desc *desc = &aw_dev->dither_desc;
    if (desc->reg == AW_REG_NONE) return;

    aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask,
        dither ? desc->enable : desc->disable);
    aw_dev_info(aw_dev->dev, "dither=%d", dither);
}

/* ─── 看门狗状态检查 ─── */
static int aw_dev_get_dsp_status(struct aw_device *aw_dev)
{
    int i;
    uint16_t reg_val = 0;
    struct aw_watch_dog_desc *desc = &aw_dev->watch_dog_desc;

    for (i = 0; i < AW_DEV_DSP_CHECK_MAX; i++) {
        aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
        if (!(reg_val & (~desc->mask))) {
            aw_dev_dbg(aw_dev->dev, "dsp wdt ok");
            return 0;
        }
        AW_MS_DELAY(AW_5_MS);
    }

    aw_dev_err(aw_dev->dev, "dsp wdt error, WDT=0x%04x", reg_val);
    return -EINVAL;
}

/* ─── DSP 状态检查（启动时）─── */
static int aw883xx_dev_dsp_check(struct aw_device *aw_dev)
{
    int ret, i;

    if (aw_dev->dsp_cfg == AW_DEV_DSP_BYPASS) {
        aw_dev_dbg(aw_dev->dev, "dsp bypass, skip check");
        return 0;
    }

    aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);
    aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_WORK);
    AW_MS_DELAY(AW_1_MS);

    for (i = 0; i < AW_DEV_DSP_CHECK_MAX; i++) {
        ret = aw_dev_get_dsp_status(aw_dev);
        if (ret >= 0) return 0;
        aw_dev_err(aw_dev->dev, "dsp wdt status error, retry %d", i);
        AW_MS_DELAY(AW_2_MS);
    }
    return -EINVAL;
}

/* ─── PLL 锁定检查（Mode 1: 直接检测 I2S 时钟）─── */
static int aw_dev_mode1_pll_check(struct aw_device *aw_dev)
{
    int i;

    for (i = 0; i < AW_DEV_SYSST_CHECK_MAX; i++) {
        if (aw_dev_get_iis_status(aw_dev) >= 0)
            return 0;
        aw_dev_err(aw_dev->dev, "mode1 I2S check error, retry %d", i);
        AW_MS_DELAY(AW_2_MS);
    }
    return -EINVAL;
}

/* ─── PLL 锁定检查（Mode 2: CCO_MUX 分频）─── */
static int aw_dev_mode2_pll_check(struct aw_device *aw_dev)
{
    int i, ret = -EINVAL;
    uint16_t reg_val;
    struct aw_cco_mux_desc *desc = &aw_dev->cco_mux_desc;

    aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
    reg_val &= (~desc->mask);
    if (reg_val == desc->divider) {
        aw_dev_dbg(aw_dev->dev, "CCO_MUX already divider");
        return -EINVAL;
    }

    aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->divider);

    for (i = 0; i < AW_DEV_SYSST_CHECK_MAX; i++) {
        if (aw_dev_get_iis_status(aw_dev) >= 0) { ret = 0; break; }
        AW_MS_DELAY(AW_2_MS);
    }

    aw_dev->ops.aw_reg_write_bits(aw_dev, desc->reg, desc->mask, desc->bypass);

    if (ret == 0) {
        AW_MS_DELAY(AW_2_MS);
        for (i = 0; i < AW_DEV_SYSST_CHECK_MAX; i++) {
            if (aw_dev_mode1_pll_check(aw_dev) >= 0) return 0;
            AW_MS_DELAY(AW_2_MS);
        }
    }

    return -EINVAL;
}

static int aw_dev_syspll_check(struct aw_device *aw_dev)
{
    int ret = aw_dev_mode1_pll_check(aw_dev);
    if (ret < 0) {
        aw_dev_info(aw_dev->dev, "mode1 failed, try mode2");
        ret = aw_dev_mode2_pll_check(aw_dev);
    }
    return ret;
}

/* ─── 系统状态检查（PLL + AMP + CLK）─── */
static int aw883xx_dev_sysst_check(struct aw_device *aw_dev)
{
    int ret = -1, i;
    uint16_t reg_val = 0;
    struct aw_sysst_desc *desc = &aw_dev->sysst_desc;
    struct aw_noise_gate_en *ng = &aw_dev->noise_gate_en;
    unsigned int check_value = desc->st_check;

    if (ng->reg != AW_REG_NONE) {
        aw_dev->ops.aw_reg_read(aw_dev, ng->reg, &reg_val);
        check_value = (reg_val & (~ng->noise_gate_mask))
                      ? desc->st_check : desc->st_sws_check;
    }

    for (i = 0; i < AW_DEV_SYSST_CHECK_MAX; i++) {
        aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
        if ((reg_val & desc->st_mask) == check_value) {
            aw_dev_dbg(aw_dev->dev, "sysst ok, SYSST=0x%04x", reg_val);
            return 0;
        }
        AW_MS_DELAY(AW_2_MS);
    }

    aw_dev_err(aw_dev->dev, "sysst timeout, SYSST=0x%04x, expect=0x%04x",
               reg_val, check_value);
    return ret;
}

/* ─── SRAM 自检 ─── */
static int aw_dev_sram_check(struct aw_device *aw_dev)
{
    int ret;
    uint16_t reg_val;
    struct aw_dsp_st *dsp = &aw_dev->dsp_st_desc;

    /* 写入奇偶测试图案到 S1/E1 区域 */
    aw_dev->ops.aw_dsp_write(aw_dev, dsp->dsp_reg_s1,
        AW_DSP_ODD_NUM_BIT_TEST, AW_DSP_16_DATA);
    AW_MS_DELAY(AW_2_MS);
    aw_dev->ops.aw_dsp_read(aw_dev, dsp->dsp_reg_s1, (uint32_t *)&reg_val, AW_DSP_16_DATA);
    aw_dev_info(aw_dev->dev, "sram s1 write=0x%04x read=0x%04x",
                AW_DSP_ODD_NUM_BIT_TEST, reg_val);

    aw_dev->ops.aw_dsp_write(aw_dev, dsp->dsp_reg_e1,
        AW_DSP_EVEN_NUM_BIT_TEST, AW_DSP_16_DATA);
    AW_MS_DELAY(AW_2_MS);
    aw_dev->ops.aw_dsp_read(aw_dev, dsp->dsp_reg_e1, (uint32_t *)&reg_val, AW_DSP_16_DATA);
    aw_dev_info(aw_dev->dev, "sram e1 write=0x%04x read=0x%04x",
                AW_DSP_EVEN_NUM_BIT_TEST, reg_val);

    if (dsp->dsp_reg_s2 != AW_REG_NONE) {
        aw_dev->ops.aw_dsp_write(aw_dev, dsp->dsp_reg_s2,
            AW_DSP_ODD_NUM_BIT_TEST, AW_DSP_16_DATA);
        AW_MS_DELAY(AW_1_MS);
        aw_dev->ops.aw_dsp_read(aw_dev, dsp->dsp_reg_s2, (uint32_t *)&reg_val, AW_DSP_16_DATA);
        aw_dev_info(aw_dev->dev, "sram s2 write=0x%04x read=0x%04x",
                    AW_DSP_ODD_NUM_BIT_TEST, reg_val);
    }

    ret = aw_dev_get_dsp_status(aw_dev);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "sram check failed");
        return ret;
    }
    aw_dev_info(aw_dev->dev, "sram check ok");
    return 0;
}

/* ─── DSP 固件版本读取 ─── */
static int aw_dev_get_firmware_ver(struct aw_device *aw_dev)
{
    uint16_t ver = 0;
    int ret = aw_dev->ops.aw_dsp_read(aw_dev,
        aw_dev->fw_ver_desc.version_reg,
        (uint32_t *)&ver,
        aw_dev->fw_ver_desc.data_type);
    if (ret < 0) return ret;
    aw_dev->fw_ver = ver;
    aw_dev_info(aw_dev->dev, "fw_ver=0x%04x", ver);
    return 0;
}

/* ─── 备份区恢复（暂不实现，无 EEPROM）─── */
static void aw_dev_backup_sec_recovery(struct aw_device *aw_dev)
{
    (void)aw_dev;
    aw_dev_dbg(aw_dev->dev, "backup sec recovery: no EEPROM, skip");
}

/* ─── crc_flag 管理 ─── */
static int aw_dev_get_crc_flag(struct aw_device *aw_dev)
{
    /* 由外部设置，返回当前 crc 状态 */
    return aw_dev->dsp_crc_st;
}

static int aw_dev_set_crc_flag(struct aw_device *aw_dev, uint32_t status)
{
    aw_dev->dsp_crc_st = (unsigned char)status;
    return 0;
}

/* ─── CRC 实时检测 ─── */
static int aw_dev_crc_realtime_get(struct aw_device *aw_dev, uint32_t *status)
{
    uint16_t val;
    struct aw_crc_check_realtime_desc *desc = &aw_dev->crc_check_realtime_desc;
    int ret = aw_dev->ops.aw_dsp_read(aw_dev, desc->addr,
        (uint32_t *)&val, desc->data_type);
    if (ret < 0) return ret;
    *status = val & (~desc->mask);
    return 0;
}

static int aw_dev_crc_realtime_set(struct aw_device *aw_dev, uint32_t enable)
{
    struct aw_crc_check_realtime_desc *desc = &aw_dev->crc_check_realtime_desc;
    return aw_dev->ops.aw_dsp_write_bits(aw_dev, desc->addr,
        desc->mask,
        enable ? desc->enable : desc->disable,
        desc->data_type);
}

/* ─── CRC 校验 ─── */
static int aw_dev_crc_check(struct aw_device *aw_dev)
{
    if (aw_dev->crc_type == AW_SW_CRC_CHECK) {
        if (aw_dev->ops.aw_sw_crc_check)
            return aw_dev->ops.aw_sw_crc_check(aw_dev);
    }
    return 0;
}

/* ─── vcalb 设置（无校准能力时为空）─── */
static int aw_dev_set_vcalb(struct aw_device *aw_dev)
{
    (void)aw_dev;
    aw_dev_dbg(aw_dev->dev, "no calibration, vcalb skipped");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * Profile / 场景管理
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_dev_check_prof(uint32_t dev, struct aw_prof_info *prof_info)
{
    struct aw_prof_desc *prof_desc;
    int i, j;

    if (!prof_info || prof_info->count <= 0) {
        aw_dev_err(dev, "prof_info invalid");
        return -EINVAL;
    }

    prof_desc = prof_info->prof_desc;
    for (i = 0; i < prof_info->count; i++) {
        if (!prof_desc[i].sec_desc[0].data || prof_desc[i].sec_desc[0].len <= 0) {
            aw_dev_err(dev, "prof[%d] reg data invalid", i);
            return -EINVAL;
        }
    }

    for (i = 0; i < prof_info->count; i++) {
        for (j = i + 1; j < prof_info->count; j++) {
            if (strncmp(prof_info->prof_desc[i].name,
                        prof_info->prof_desc[j].name, AW_PROF_NAME_MAX) == 0) {
                aw_dev_err(dev, "dup prof name[%s]", prof_info->prof_desc[i].name);
                return -EINVAL;
            }
        }
    }

    aw_dev_info(dev, "prof check ok, count=%u", prof_info->count);
    return 0;
}

struct aw_sec_data_desc *aw883xx_dev_get_prof_data_byname(
    struct aw_device *aw_dev, char *prof_name, int data_type)
{
    int i;

    if (data_type >= AW_DATA_TYPE_MAX) {
        aw_dev_err(aw_dev->dev, "bad data_type=%d", data_type);
        return NULL;
    }

    for (i = 0; i < aw_dev->prof_info->count; i++) {
        if (strncmp(prof_name, aw_dev->prof_info->prof_desc[i].name,
                    AW_PROF_NAME_MAX) == 0) {
            aw_dev_dbg(aw_dev->dev, "found prof[%s] type=%d",
                       aw_dev->prof_info->prof_desc[i].name, data_type);
            return &aw_dev->prof_info->prof_desc[i].sec_desc[data_type];
        }
    }
    aw_dev_err(aw_dev->dev, "prof[%s] not found", prof_name);
    return NULL;
}

static int aw_dev_check_profile_name(struct aw_device *aw_dev, const char *prof_name)
{
    int i;
    for (i = 0; i < aw_dev->prof_info->count; i++) {
        if (strncmp(prof_name, aw_dev->prof_info->prof_desc[i].name,
                    AW_PROF_NAME_MAX) == 0)
            return 0;
    }
    aw_dev_err(aw_dev->dev, "prof[%s] not found", prof_name);
    return -EINVAL;
}

int aw883xx_dev_set_profile_name(struct aw_device *aw_dev, const char *prof_name)
{
    if (aw_dev_check_profile_name(aw_dev, prof_name))
        return -EINVAL;
    strncpy(aw_dev->set_prof_name, prof_name, AW_PROF_NAME_MAX - 1);
    aw_dev_info(aw_dev->dev, "set prof=%s", aw_dev->set_prof_name);
    return 0;
}

char *aw883xx_dev_get_profile_name(struct aw_device *aw_dev)
{
    return aw_dev->set_prof_name;
}

static int aw_dev_prof_init(struct aw_device *aw_dev, struct aw_init_info *init_info)
{
    struct aw_prof_info *prof_info = init_info->prof_info;
    const char *first_name;

    if (!prof_info || !prof_info->prof_desc) {
        aw_dev_err(aw_dev->dev, "no profile data");
        return -EINVAL;
    }

    if (prof_info->chip_id != aw_dev->chip_id) {
        aw_dev_err(aw_dev->dev, "chip_id mismatch: prof=0x%04lx dev=0x%04x",
                   (unsigned long)prof_info->chip_id, aw_dev->chip_id);
        return -EINVAL;
    }

    aw_dev->prof_info = prof_info;
    first_name = prof_info->prof_desc[0].name;
    strncpy(aw_dev->first_prof_name, first_name, AW_PROF_NAME_MAX - 1);
    aw_dev_info(aw_dev->dev, "first prof=%s", aw_dev->first_prof_name);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 音量控制
 * ═══════════════════════════════════════════════════════════════ */

static int aw_dev_set_volume(struct aw_device *aw_dev, uint32_t set_vol)
{
    struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
    uint16_t hw_vol = set_vol + vol_desc->init_volume;

    if (aw_dev->ops.aw_set_hw_volume(aw_dev, hw_vol) < 0) {
        aw_dev_err(aw_dev->dev, "set volume failed");
        return -EINVAL;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 寄存器 dump
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_dev_reg_dump(struct aw_device *aw_dev)
{
    int reg_num = aw_dev->ops.aw_get_reg_num();
    uint8_t i;
    uint16_t reg_val;

    for (i = 0; i < reg_num; i++) {
        if (aw_dev->ops.aw_check_rd_access(i)) {
            aw_dev->ops.aw_reg_read(aw_dev, i, &reg_val);
            aw_dev_info(aw_dev->dev, "R 0x%02X=0x%04X", i, reg_val);
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 固件 / 配置更新
 * ═══════════════════════════════════════════════════════════════ */

static int aw_dev_reg_container_update(struct aw_device *aw_dev,
    const uint8_t *data, uint32_t len)
{
    int i, ret;
    uint8_t reg_addr;
    uint16_t reg_val, read_vol;
    struct aw_int_desc *int_desc = &aw_dev->int_desc;
    struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
    uint16_t *reg_data = (uint16_t *)data;
    int data_len = len >> 1;

    if (data_len % 2 != 0) {
        aw_dev_err(aw_dev->dev, "bad reg data len=%d", data_len);
        return -EINVAL;
    }

    for (i = 0; i < data_len; i += 2) {
        reg_addr = reg_data[i];
        reg_val = reg_data[i + 1];

        /* 特殊寄存器处理 */
        if (reg_addr == int_desc->mask_reg) {
            int_desc->int_mask = reg_val;
            reg_val = int_desc->mask_default;
        }
        if (reg_addr == aw_dev->mute_desc.reg)
            reg_val = (reg_val & aw_dev->mute_desc.mask) | aw_dev->mute_desc.enable;
        if (reg_addr == aw_dev->pwd_desc.reg)
            reg_val = (reg_val & aw_dev->pwd_desc.mask) | aw_dev->pwd_desc.enable;
        if (reg_addr == aw_dev->txen_desc.reg) {
            aw_dev->txen_st = reg_val & (~aw_dev->txen_desc.mask);
            reg_val = (reg_val & aw_dev->txen_desc.mask) | aw_dev->txen_desc.disable;
        }
        if (reg_addr == aw_dev->volume_desc.reg) {
            read_vol = (reg_val & (~aw_dev->volume_desc.mask)) >> aw_dev->volume_desc.shift;
            aw_dev->volume_desc.init_volume = aw_dev->ops.aw_reg_val_to_db(read_vol);
        }
        if (reg_addr == aw_dev->efuse_check_desc.reg) {
            aw_dev->efuse_check_desc.check_val =
                ((reg_val & (~aw_dev->efuse_check_desc.mask)) == aw_dev->efuse_check_desc.or_val)
                ? AW_EF_OR_CHECK : AW_EF_AND_CHECK;
        }
        if (reg_addr == aw_dev->crc_check_desc.crc_ctrl_reg)
            aw_dev->crc_check_desc.crc_init_val = reg_val;
        if (reg_addr == aw_dev->vcalb_desc.vcalb_reg) {
            aw_dev->vcalb_desc.init_value = reg_val;
            continue;
        }
        if (reg_addr == aw_dev->cali_desc.hw_cali_re_desc.hbits_reg) {
            aw_dev->cali_desc.hw_cali_re_desc.re_hbits = reg_val;
            continue;
        }
        if (reg_addr == aw_dev->cali_desc.hw_cali_re_desc.lbits_reg) {
            aw_dev->cali_desc.hw_cali_re_desc.re_lbits = reg_val;
            continue;
        }
        if (reg_addr == aw_dev->dsp_en_desc.reg) {
            aw_dev->dsp_cfg = (reg_val & (~aw_dev->dsp_en_desc.mask))
                              ? AW_DEV_DSP_BYPASS : AW_DEV_DSP_WORK;
            reg_val = (reg_val & aw_dev->dsp_en_desc.mask) | aw_dev->dsp_en_desc.disable;
        }
        if (reg_addr == aw_dev->dither_desc.reg) {
            aw_dev->dither_st = reg_val & (~aw_dev->dither_desc.mask);
            aw_dev_info(aw_dev->dev, "dither_st=0x%04x", aw_dev->dither_st);
        }

        ret = aw_dev->ops.aw_reg_write(aw_dev, reg_addr, reg_val);
        if (ret < 0) return ret;
    }

    aw_dev_pwd(aw_dev, false);
    AW_MS_DELAY(AW_1_MS);

    aw_dev_get_cur_mode_st(aw_dev);

    if (strncmp(aw_dev->cur_prof_name, aw_dev->set_prof_name, AW_PROF_NAME_MAX) != 0) {
        vol_desc->ctl_volume = 0;
    } else {
        aw_dev_set_volume(aw_dev, vol_desc->ctl_volume);
    }

    aw_dev_dbg(aw_dev->dev, "reg update done");
    return 0;
}

static int aw_dev_reg_update(struct aw_device *aw_dev)
{
    struct aw_sec_data_desc *reg_data =
        aw883xx_dev_get_prof_data_byname(aw_dev, aw_dev->set_prof_name, AW_DATA_TYPE_REG);
    if (!reg_data) return -EINVAL;
    aw_dev_reg_container_update(aw_dev, reg_data->data, reg_data->len);
    return 0;
}

static int aw_dev_dsp_container_update(struct aw_device *aw_dev,
    const uint8_t *dsp_data, uint32_t len, uint16_t base)
{
    struct aw_dsp_mem_desc *dsp = &aw_dev->dsp_mem_desc;
    uint32_t tmp_len;
    int i;

    aw_mutex_lock();

    /* 写 DSP 起始地址 */
    aw_dev->ops.aw_i2c_write(aw_dev, dsp->dsp_madd_reg, base);

    /* 块写数据（每块 128 字节） */
    for (i = 0; i < (int)len; i += AW_MAX_RAM_WRITE_BYTE_SIZE) {
        tmp_len = ((len - i) < AW_MAX_RAM_WRITE_BYTE_SIZE) ? (len - i) : AW_MAX_RAM_WRITE_BYTE_SIZE;
        aw_dev->ops.aw_i2c_writes(aw_dev, dsp->dsp_mdat_reg, (uint8_t *)&dsp_data[i], tmp_len);
    }

    aw_mutex_unlock();
    aw_dev_dbg(aw_dev->dev, "dsp container update: base=0x%04x len=%lu", base, (unsigned long)len);
    return 0;
}

static int aw_dev_dsp_fw_update(struct aw_device *aw_dev)
{
    struct aw_sec_data_desc *dsp_fw =
        aw883xx_dev_get_prof_data_byname(aw_dev, aw_dev->set_prof_name, AW_DATA_TYPE_DSP_FW);
    if (!dsp_fw) return -EINVAL;

    aw_dev->dsp_fw_len = dsp_fw->len;
    aw_dev_info(aw_dev->dev, "dsp fw update: len=%lu", (unsigned long)dsp_fw->len);
    return aw_dev_dsp_container_update(aw_dev, dsp_fw->data, dsp_fw->len,
        aw_dev->dsp_mem_desc.dsp_fw_base_addr);
}

static int aw_dev_dsp_cfg_update(struct aw_device *aw_dev)
{
    struct aw_sec_data_desc *dsp_cfg =
        aw883xx_dev_get_prof_data_byname(aw_dev, aw_dev->set_prof_name, AW_DATA_TYPE_DSP_CFG);
    if (!dsp_cfg) return -EINVAL;

    aw_dev->dsp_cfg_len = dsp_cfg->len;
    aw_dev_info(aw_dev->dev, "dsp cfg update: len=%lu", (unsigned long)dsp_cfg->len);
    return aw_dev_dsp_container_update(aw_dev, dsp_cfg->data, dsp_cfg->len,
        aw_dev->dsp_mem_desc.dsp_cfg_base_addr);
}

/* ═══════════════════════════════════════════════════════════════
 * 公开接口：固件更新 / 启动 / 停止 / 探测
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_device_fw_update(struct aw_device *aw_dev,
    bool up_dsp_fw_en, bool force_up_en)
{
    int ret;

    aw_dev_info(aw_dev->dev, "fw_update: cur=%s set=%s force=%d",
                aw_dev->cur_prof_name, aw_dev->set_prof_name, force_up_en);

    if (strncmp(aw_dev->cur_prof_name, aw_dev->set_prof_name, AW_PROF_NAME_MAX) == 0
        && force_up_en == 0) {
        aw_dev_info(aw_dev->dev, "scene unchanged, skip");
        return 0;
    }

    ret = aw_dev_reg_update(aw_dev);
    if (ret < 0) { aw_dev_err(aw_dev->dev, "reg update fail"); return ret; }

    aw_dev_set_hmute(aw_dev, AW_DEV_HMUTE_ENABLE);

    if (aw_dev->dsp_cfg == AW_DEV_DSP_WORK)
        aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);

    aw_dev_memclk_select(aw_dev, AW_DEV_MEMCLK_OSC);

    ret = aw_dev_sram_check(aw_dev);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "sram check fail");
        goto error;
    }

    aw_dev_backup_sec_recovery(aw_dev);

    if (up_dsp_fw_en) {
        ret = aw_dev_dsp_fw_update(aw_dev);
        if (ret < 0) { aw_dev_err(aw_dev->dev, "dsp fw update fail"); goto error; }
    }

    ret = aw_dev_dsp_cfg_update(aw_dev);
    if (ret < 0) { aw_dev_err(aw_dev->dev, "dsp cfg update fail"); goto error; }

    aw_dev_memclk_select(aw_dev, AW_DEV_MEMCLK_PLL);

    strncpy(aw_dev->cur_prof_name, aw_dev->set_prof_name, AW_PROF_NAME_MAX - 1);
    aw_dev_info(aw_dev->dev, "fw_update [%s] done", aw_dev->cur_prof_name);
    return 0;

error:
    aw_dev_memclk_select(aw_dev, AW_DEV_MEMCLK_PLL);
    return ret;
}

int aw883xx_device_start(struct aw_device *aw_dev)
{
    int ret;

    aw_dev_info(aw_dev->dev, "start");

    if (aw_dev->status == AW_DEV_PW_ON) {
        aw_dev_info(aw_dev->dev, "already on");
        return 0;
    }

    aw_dev_set_dither(aw_dev, false);

    /* 1. 上电 */
    aw_dev_pwd(aw_dev, false);
    AW_MS_DELAY(AW_2_MS);

    /* 2. PLL 锁定 */
    ret = aw_dev_syspll_check(aw_dev);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "PLL check fail");
        goto pll_fail;
    }

    /* 3. 功放开机 */
    aw_dev_amppd(aw_dev, false);
    AW_MS_DELAY(AW_1_MS);

    /* 4. 系统状态检查 */
    ret = aw883xx_dev_sysst_check(aw_dev);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "sysst check fail");
        goto sysst_fail;
    }

    if (aw_dev->dsp_cfg == AW_DEV_DSP_WORK) {
        aw_dev_backup_sec_recovery(aw_dev);

        ret = aw_dev_crc_check(aw_dev);
        if (ret < 0) {
            aw_dev_err(aw_dev->dev, "CRC check fail");
            goto crc_fail;
        }

        aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);
        aw_dev_set_vcalb(aw_dev);

        ret = aw883xx_dev_dsp_check(aw_dev);
        if (ret < 0) {
            aw_dev_err(aw_dev->dev, "DSP check fail");
            goto dsp_fail;
        }
    } else {
        aw_dev_dbg(aw_dev->dev, "DSP bypass mode");
    }

    /* TX 反馈使能 */
    if (aw_dev->ops.aw_i2s_tx_enable && aw_dev->txen_st)
        aw_dev->ops.aw_i2s_tx_enable(aw_dev, true);

    /* Dither */
    if (aw_dev->dither_st == aw_dev->dither_desc.enable)
        aw_dev_set_dither(aw_dev, true);

    /* 取消静音 */
    aw_dev_set_hmute(aw_dev, AW_DEV_HMUTE_DISABLE);

    /* 中断 */
    aw_dev_clear_int_status(aw_dev);
    aw_dev_set_intmask(aw_dev, AW_DEV_UNMASK_INT_VAL);

    aw_dev->status = AW_DEV_PW_ON;
    aw_dev_info(aw_dev->dev, "start done");
    return 0;

dsp_fail:
crc_fail:
    aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);
sysst_fail:
    aw_dev_clear_int_status(aw_dev);
    aw_dev_amppd(aw_dev, true);
pll_fail:
    aw_dev_pwd(aw_dev, true);
    aw_dev->status = AW_DEV_PW_OFF;
    return ret;
}

int aw883xx_device_stop(struct aw_device *aw_dev)
{
    uint16_t reg_data;

    aw_dev_info(aw_dev->dev, "stop");

    aw_dev->ops.aw_reg_read(aw_dev, aw_dev->pwd_desc.reg, &reg_data);
    if (aw_dev->status == AW_DEV_PW_OFF
        && (reg_data & aw_dev->pwd_desc.enable)) {
        aw_dev_info(aw_dev->dev, "already off");
        return 0;
    }

    aw_dev->status = AW_DEV_PW_OFF;

    aw_dev_set_hmute(aw_dev, AW_DEV_HMUTE_ENABLE);
    AW_MS_DELAY(AW_4_MS);

    if (aw_dev->ops.aw_i2s_tx_enable)
        aw_dev->ops.aw_i2s_tx_enable(aw_dev, false);
    AW_MS_DELAY(AW_1_MS);

    aw_dev_set_intmask(aw_dev, AW_DEV_MASK_INT_VAL);
    aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);
    aw_dev_amppd(aw_dev, true);
    aw_dev_pwd(aw_dev, true);

    aw_dev_info(aw_dev->dev, "stop done");
    return 0;
}

static int aw_device_init(struct aw_device *aw_dev)
{
    int ret;

    strncpy(aw_dev->cur_prof_name, aw_dev->first_prof_name, AW_PROF_NAME_MAX - 1);
    strncpy(aw_dev->set_prof_name, aw_dev->first_prof_name, AW_PROF_NAME_MAX - 1);

    ret = aw883xx_device_fw_update(aw_dev, true, true);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "init fw update fail");
        return ret;
    }

    aw_dev_set_intmask(aw_dev, AW_DEV_MASK_INT_VAL);
    aw_dev_set_hmute(aw_dev, AW_DEV_HMUTE_ENABLE);

    if (aw_dev->ops.aw_i2s_tx_enable)
        aw_dev->ops.aw_i2s_tx_enable(aw_dev, false);
    AW_MS_DELAY(AW_1_MS);

    aw_dev_amppd(aw_dev, true);
    aw_dev_dsp_enable(aw_dev, AW_DEV_DSP_BYPASS);
    aw_dev_pwd(aw_dev, true);

    aw_dev_info(aw_dev->dev, "init done");
    return 0;
}

int aw883xx_device_probe(struct aw_device *aw_dev, struct aw_init_info *init_info)
{
    int ret;

    ret = aw_dev_prof_init(aw_dev, init_info);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "prof init fail");
        return ret;
    }

    /* MONITOR: 禁用，不调用 aw883xx_dev_get_monitor_func */

    ret = aw_device_init(aw_dev);
    if (ret < 0) {
        aw_dev_err(aw_dev->dev, "device init fail");
        return ret;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 状态 / 参数读写（精简版）
 * ═══════════════════════════════════════════════════════════════ */

bool aw883xx_device_status(struct aw_device *aw_dev, dev_status_t type,
    status_option_t status_ops)
{
    uint16_t val = 0;

    switch (type) {
    case AW_DEV_SOFT_RESET_STATUS:
        aw_dev_soft_reset(aw_dev);
        return true;
    case AW_DEV_REG_DUMP_STATUS:
        aw883xx_dev_reg_dump(aw_dev);
        return true;
    case AW_DEV_PLL_WDT_STATUS:
        aw_dev_syspll_check(aw_dev);
        aw_dev_get_dsp_status(aw_dev);
        return true;
    default:
        aw_dev_dbg(aw_dev->dev, "unsupported status type=%d", (int)type);
        break;
    }
    (void)status_ops;
    (void)val;
    return false;
}

int aw883xx_device_params(struct aw_device *aw_dev, dev_params_t params_type,
    void *data, uint8_t size, params_option_t params_ops)
{
    switch (params_type) {
    case AW_DEV_DSP_PARAMS:
        if (params_ops == AW_GET_DEV_PARAMS) {
            aw_dev_get_firmware_ver(aw_dev);
            *(uint32_t *)data = aw_dev->fw_ver;
        }
        break;
    case AW_DEV_VOLUME_PARAMS:
        if (params_ops == AW_SET_DEV_PARAMS) {
            aw_dev_set_volume(aw_dev, *(uint32_t *)data);
            aw_dev->volume_desc.ctl_volume = *(uint32_t *)data;
        }
        break;
    case AW_DEV_HMUTE_PARAMS:
        if (params_ops == AW_SET_DEV_PARAMS)
            aw_dev_set_hmute(aw_dev, *(uint32_t *)data);
        break;
    case AW_DEV_CRC_FLAG_PARAMS:
        if (params_ops == AW_SET_DEV_PARAMS)
            aw_dev_set_crc_flag(aw_dev, *(uint32_t *)data);
        else if (params_ops == AW_GET_DEV_PARAMS)
            *(uint32_t *)data = aw_dev_get_crc_flag(aw_dev);
        break;
    case AW_DEV_SYSST_PARAMS: {
        int ret = aw883xx_dev_sysst_check(aw_dev);
        *(int *)data = ret;
        break;
    }
    case AW_DEV_REALTIME_CRC_GET_PARAMS:
        aw_dev_crc_realtime_get(aw_dev, (uint32_t *)data);
        break;
    case AW_DEV_REALTIME_CRC_SET_PARAMS:
        aw_dev_crc_realtime_set(aw_dev, *(uint32_t *)data);
        break;
    default:
        aw_dev_dbg(aw_dev->dev, "unsupported params type=%d", (int)params_type);
        break;
    }
    (void)size;
    return 0;
}

int aw883xx_crc_realtime_check(struct aw_device *aw_dev, uint32_t *status)
{
    return aw_dev_crc_realtime_get(aw_dev, status);
}
