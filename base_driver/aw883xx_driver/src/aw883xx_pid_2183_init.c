/*
 * aw883xx_pid_2183_init.c — AW88399 (PID_2183) 设备初始化 (adapted from AWINIC)
 *
 * 适配变更：
 *   - struct aw883xx → struct aw883xx_driver (扁平化主结构体)
 *   - I2C/DSP 函数前向声明（实现在 aw883xx_driver.c）
 *   - 移除 AW_MONITOR 代码块
 */
#include <stdio.h>
#include <stdlib.h>
#include "aw883xx_base.h"
#include "aw883xx_internal.h"
#include "aw883xx_device.h"
#include "aw883xx_pid_2183_reg.h"

/* ═══════════════════════════════════════════════════════════════
 * 前向声明 — aw883xx_driver.c 中实现的 I2C/DSP 基础函数
 * ═══════════════════════════════════════════════════════════════ */

extern int aw883xx_drv_i2c_writes(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint8_t *buf, uint16_t len);
extern int aw883xx_drv_i2c_write(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t reg_data);
extern int aw883xx_drv_i2c_read(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t *reg_data);
extern int aw883xx_drv_reg_write(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t reg_data);
extern int aw883xx_drv_reg_read(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t *reg_data);
extern int aw883xx_drv_reg_write_bits(struct aw883xx_driver *drv,
    uint8_t reg_addr, uint16_t mask, uint16_t reg_data);
extern int aw883xx_drv_dsp_write(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t dsp_data, uint8_t data_type);
extern int aw883xx_drv_dsp_read(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t *dsp_data, uint8_t data_type);
extern int aw883xx_drv_dsp_write_bits(struct aw883xx_driver *drv,
    uint16_t dsp_addr, uint32_t dsp_mask, uint32_t dsp_data, uint8_t data_type);
extern int aw883xx_drv_get_dev_num(void);
extern int aw883xx_drv_get_version(char *buf, int size);

/* ═══════════════════════════════════════════════════════════════
 * Ops 桥接层 — aw_device_ops → aw883xx_driver I2C/DSP 函数
 * ═══════════════════════════════════════════════════════════════ */

static int _i2c_writes(struct aw_device *aw_dev, uint8_t reg_addr,
    uint8_t *buf, uint16_t len)
{
    return aw883xx_drv_i2c_writes(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, buf, len);
}

static int _i2c_write(struct aw_device *aw_dev, uint8_t reg_addr,
    uint16_t reg_data)
{
    return aw883xx_drv_i2c_write(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, reg_data);
}

static int _i2c_read(struct aw_device *aw_dev, uint8_t reg_addr,
    uint16_t *reg_data)
{
    return aw883xx_drv_i2c_read(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, reg_data);
}

static int _reg_write(struct aw_device *aw_dev, uint8_t reg_addr,
    uint16_t reg_data)
{
    return aw883xx_drv_reg_write(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, reg_data);
}

static int _reg_read(struct aw_device *aw_dev, uint8_t reg_addr,
    uint16_t *reg_data)
{
    return aw883xx_drv_reg_read(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, reg_data);
}

static int _reg_write_bits(struct aw_device *aw_dev, uint8_t reg_addr,
    uint16_t mask, uint16_t reg_data)
{
    return aw883xx_drv_reg_write_bits(
        (struct aw883xx_driver *)aw_dev->private_data, reg_addr, mask, reg_data);
}

static int _dsp_write(struct aw_device *aw_dev, uint16_t dsp_addr,
    uint32_t dsp_data, uint8_t data_type)
{
    return aw883xx_drv_dsp_write(
        (struct aw883xx_driver *)aw_dev->private_data, dsp_addr, dsp_data, data_type);
}

static int _dsp_read(struct aw_device *aw_dev, uint16_t dsp_addr,
    uint32_t *dsp_data, uint8_t data_type)
{
    return aw883xx_drv_dsp_read(
        (struct aw883xx_driver *)aw_dev->private_data, dsp_addr, dsp_data, data_type);
}

static int _dsp_write_bits(struct aw_device *aw_dev, uint16_t dsp_addr,
    uint32_t dsp_mask, uint32_t dsp_data, uint8_t data_type)
{
    return aw883xx_drv_dsp_write_bits(
        (struct aw883xx_driver *)aw_dev->private_data, dsp_addr, dsp_mask, dsp_data, data_type);
}

/* ═══════════════════════════════════════════════════════════════
 * PID_2183 特定函数
 * ═══════════════════════════════════════════════════════════════ */

static void _i2s_tx_enable(struct aw_device *aw_dev, bool flag)
{
    struct aw883xx_driver *drv = (struct aw883xx_driver *)aw_dev->private_data;
    aw_dev_dbg(drv->dev_index, "i2s_tx_enable=%d", flag);

    aw883xx_drv_reg_write_bits(drv, AW_PID_2183_I2SCTRL3_REG,
        AW_PID_2183_I2STXEN_MASK,
        flag ? AW_PID_2183_I2STXEN_ENABLE_VALUE
             : AW_PID_2183_I2STXEN_DISABLE_VALUE);
}

static bool _check_rd_access(int reg)
{
    if (reg >= AW_PID_2183_REG_MAX) return false;
    return (aw_pid_2183_reg_access[reg] & REG_RD_ACCESS) != 0;
}

static bool _check_wr_access(int reg)
{
    if (reg >= AW_PID_2183_REG_MAX) return false;
    return (aw_pid_2183_reg_access[reg] & REG_WR_ACCESS) != 0;
}

static int _get_reg_num(void) { return AW_PID_2183_REG_MAX; }

static unsigned int _reg_val_to_db(unsigned int reg_val) { return reg_val; }
static int _db_to_reg_val(uint16_t db_val) { return db_val; }

static int _set_volume(struct aw_device *aw_pa, uint16_t volume)
{
    struct aw883xx_driver *drv = (struct aw883xx_driver *)aw_pa->private_data;
    struct aw_volume_desc *vol_desc = &drv->aw_pa->volume_desc;
    uint16_t reg_value, reg_vol;

    reg_vol = _db_to_reg_val(volume);

    if (aw883xx_drv_reg_read(drv, AW_PID_2183_SYSCTRL2_REG, &reg_value) < 0) {
        aw_dev_err(aw_pa->dev, "read SYSCTRL2 fail");
        return -EIO;
    }

    reg_value = (reg_vol << AW_PID_2183_VOL_START_BIT) |
                (reg_value & AW_PID_2183_VOL_MASK);

    if (aw883xx_drv_reg_write(drv, AW_PID_2183_SYSCTRL2_REG, reg_value) < 0) {
        aw_dev_err(aw_pa->dev, "write SYSCTRL2 fail");
        return -EIO;
    }

    (void)vol_desc; /* unused in this path */
    return 0;
}

static int _get_volume(struct aw_device *aw_pa, uint16_t *volume)
{
    struct aw883xx_driver *drv = (struct aw883xx_driver *)aw_pa->private_data;
    uint16_t reg_value, reg_vol;

    if (aw883xx_drv_reg_read(drv, AW_PID_2183_SYSCTRL2_REG, &reg_value) < 0) {
        aw_dev_err(aw_pa->dev, "read SYSCTRL2 fail");
        return -EIO;
    }

    reg_vol = (reg_value & (~AW_PID_2183_VOL_MASK)) >> AW_PID_2183_VOL_START_BIT;
    *volume = _reg_val_to_db(reg_vol);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * aw883xx_pid_2183_dev_init — 设备描述符构造函数
 * ═══════════════════════════════════════════════════════════════ */

int aw883xx_pid_2183_dev_init(struct aw883xx_driver *drv)
{
    struct aw_device *aw_pa;

    aw_pr_dbg("enter");

    if (!drv) {
        aw_pr_err("drv is null");
        return -EINVAL;
    }

    aw_pa = calloc(1, sizeof(struct aw_device));
    if (!aw_pa) {
        aw_dev_err(drv->dev_index, "calloc failed");
        return -ENOMEM;
    }

    /* ─── 基础信息 ─── */
    aw_pa->chip_id     = drv->chip_id;   /* 0x2183 */
    aw_pa->dev         = drv->dev_index; /* AW_DEV_0 */
    aw_pa->private_data = (void *)drv;
    aw_pa->fw_status    = AW_DEV_FW_FAILED;
    aw_pa->fade_step    = AW_PID_2183_VOLUME_STEP_DB;
    aw_pa->crc_type     = AW_HW_CRC_CHECK;

    /* ─── 函数指针表 ─── */
    aw_pa->ops.aw_set_vcalb   = NULL;
    aw_pa->ops.aw_set_cali_re = NULL;
    aw_pa->ops.aw_get_cali_re = NULL;
    aw_pa->ops.aw_get_r0      = NULL;
    aw_pa->ops.aw_sw_crc_check = NULL;
    aw_pa->ops.aw_init_vcalb_update = NULL;

    aw_pa->ops.aw_get_version     = aw883xx_drv_get_version;
    aw_pa->ops.aw_i2c_writes      = _i2c_writes;
    aw_pa->ops.aw_i2c_write       = _i2c_write;
    aw_pa->ops.aw_reg_write       = _reg_write;
    aw_pa->ops.aw_reg_write_bits  = _reg_write_bits;
    aw_pa->ops.aw_i2c_read        = _i2c_read;
    aw_pa->ops.aw_reg_read        = _reg_read;
    aw_pa->ops.aw_dsp_read        = _dsp_read;
    aw_pa->ops.aw_dsp_write       = _dsp_write;
    aw_pa->ops.aw_dsp_write_bits  = _dsp_write_bits;
    aw_pa->ops.aw_get_dev_num     = aw883xx_drv_get_dev_num;

    aw_pa->ops.aw_get_hw_volume   = _get_volume;
    aw_pa->ops.aw_set_hw_volume   = _set_volume;
    aw_pa->ops.aw_reg_val_to_db   = _reg_val_to_db;

    aw_pa->ops.aw_check_rd_access = _check_rd_access;
    aw_pa->ops.aw_check_wr_access = _check_wr_access;
    aw_pa->ops.aw_get_reg_num     = _get_reg_num;

    aw_pa->ops.aw_i2s_tx_enable   = _i2s_tx_enable;

    /* ─── 读取芯片特定寄存器 PID（占位） ─── */
    aw_pa->ops.aw_read_dsp_pid    = NULL;

    /* ─── SPI CRC 不适用 ─── */
    aw_pa->dsp_crc_desc.ctl_reg   = AW_REG_NONE;

    /* ═══════════════════════════════════════════════════════
     * 寄存器描述符填充（与原始 aw883xx_pid_2183_init.c 一致）
     * ═══════════════════════════════════════════════════════ */

    /* ─── 中断 ─── */
    aw_pa->int_desc.mask_reg    = AW_PID_2183_SYSINTM_REG;
    aw_pa->int_desc.mask_default = AW_PID_2183_SYSINTM_DEFAULT;
    aw_pa->int_desc.int_mask     = AW_PID_2183_SYSINTM_DEFAULT;
    aw_pa->int_desc.st_reg       = AW_PID_2183_SYSINT_REG;
    aw_pa->int_desc.intst_mask   = AW_PID_2183_BIT_SYSINT_CHECK;

    /* ─── 掉电 ─── */
    aw_pa->pwd_desc.reg    = AW_PID_2183_SYSCTRL_REG;
    aw_pa->pwd_desc.mask   = AW_PID_2183_PWDN_MASK;
    aw_pa->pwd_desc.enable  = AW_PID_2183_PWDN_POWER_DOWN_VALUE;
    aw_pa->pwd_desc.disable = AW_PID_2183_PWDN_WORKING_VALUE;

    /* ─── 静音 ─── */
    aw_pa->mute_desc.reg    = AW_PID_2183_SYSCTRL_REG;
    aw_pa->mute_desc.mask   = AW_PID_2183_HMUTE_MASK;
    aw_pa->mute_desc.enable  = AW_PID_2183_HMUTE_ENABLE_VALUE;
    aw_pa->mute_desc.disable = AW_PID_2183_HMUTE_DISABLE_VALUE;

    /* ─── TX 使能 ─── */
    aw_pa->txen_desc.reg    = AW_PID_2183_I2SCTRL3_REG;
    aw_pa->txen_desc.mask   = AW_PID_2183_I2STXEN_MASK;
    aw_pa->txen_desc.enable  = AW_PID_2183_I2STXEN_ENABLE_VALUE;
    aw_pa->txen_desc.disable = AW_PID_2183_I2STXEN_DISABLE_VALUE;

    /* ─── Vsense ─── */
    aw_pa->vsense_desc.vcalb_vsense_reg  = AW_PID_2183_VSNCTRL1_REG;
    aw_pa->vsense_desc.vcalk_vdsel_mask  = AW_PID_2183_VDSEL_MASK;

    /* ─── eFuse ─── */
    aw_pa->efuse_check_desc.reg     = AW_PID_2183_DBGCTRL_REG;
    aw_pa->efuse_check_desc.mask    = AW_PID_2183_EF_DBMD_MASK;
    aw_pa->efuse_check_desc.or_val  = AW_PID_2183_EF_DBMD_OR_VALUE;
    aw_pa->efuse_check_desc.and_val = AW_PID_2183_EF_DBMD_AND_VALUE;

    /* ─── Vcalb ─── */
    aw_pa->vcalb_desc.icalkh_reg      = AW_PID_2183_EFRH4_REG;
    aw_pa->vcalb_desc.icalkh_reg_mask = AW_PID_2183_EF_ISN_GESLP_H_MASK;
    aw_pa->vcalb_desc.icalkh_shift    = AW_PID_2183_EF_ISN_GESLP_H_START_BIT;
    aw_pa->vcalb_desc.icalkl_reg      = AW_PID_2183_EFRL4_REG;
    aw_pa->vcalb_desc.icalkl_reg_mask = AW_PID_2183_EF_ISN_GESLP_L_MASK;
    aw_pa->vcalb_desc.icalkl_shift    = AW_PID_2183_EF_ISN_GESLP_L_START_BIT;
    aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2183_EF_ISN_GESLP_SIGN_MASK;
    aw_pa->vcalb_desc.icalk_neg_mask  = AW_PID_2183_EF_ISN_GESLP_SIGN_NEG;
    aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2183_ICABLK_FACTOR;

    aw_pa->vcalb_desc.vcalkh_reg      = AW_PID_2183_EFRH3_REG;
    aw_pa->vcalb_desc.vcalkh_reg_mask = AW_PID_2183_EF_VSN_GESLP_H_MASK;
    aw_pa->vcalb_desc.vcalkh_shift    = AW_PID_2183_EF_VSN_GESLP_H_START_BIT;
    aw_pa->vcalb_desc.vcalkl_reg      = AW_PID_2183_EFRL3_REG;
    aw_pa->vcalb_desc.vcalkl_reg_mask = AW_PID_2183_EF_VSN_GESLP_L_MASK;
    aw_pa->vcalb_desc.vcalkl_shift    = AW_PID_2183_EF_VSN_GESLP_L_START_BIT;
    aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2183_EF_VSN_GESLP_SIGN_MASK;
    aw_pa->vcalb_desc.vcalk_neg_mask  = AW_PID_2183_EF_VSN_GESLP_SIGN_NEG;
    aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2183_VCABLK_FACTOR;

    aw_pa->vcalb_desc.vscal_factor = AW_PID_2183_VSCAL_FACTOR;
    aw_pa->vcalb_desc.iscal_factor = AW_PID_2183_ISCAL_FACTOR;
    aw_pa->vcalb_desc.vcalb_adj_shift = AW_PID_2183_VCALB_ADJ_FACTOR;
    aw_pa->vcalb_desc.cabl_base_value = AW_PID_2183_CABL_BASE_VALUE;
    aw_pa->vcalb_desc.vcalb_accuracy  = AW_PID_2183_VCALB_ACCURACY;
    aw_pa->vcalb_desc.vcalb_reg       = AW_PID_2183_DSPVCALB_REG;

    /* Vsense DAC */
    aw_pa->vcalb_desc.vcalkh_dac_reg      = AW_PID_2183_EFRH2_REG;
    aw_pa->vcalb_desc.vcalkh_dac_reg_mask = AW_PID_2183_INTERNAL_VSN_TRIM_H_MASK;
    aw_pa->vcalb_desc.vcalkh_dac_shift    = AW_PID_2183_INTERNAL_VSN_TRIM_H_START_BIT;
    aw_pa->vcalb_desc.vcalkl_dac_reg      = AW_PID_2183_EFRL2_REG;
    aw_pa->vcalb_desc.vcalkl_dac_reg_mask = AW_PID_2183_INTERNAL_VSN_TRIM_L_MASK;
    aw_pa->vcalb_desc.vcalkl_dac_shift    = AW_PID_2183_INTERNAL_VSN_TRIM_L_START_BIT;
    aw_pa->vcalb_desc.vcalk_dac_sign_mask = AW_PID_2183_TEM4_SIGN_MASK;
    aw_pa->vcalb_desc.vcalk_dac_neg_mask  = AW_PID_2183_TEM4_SIGN_NEG;
    aw_pa->vcalb_desc.vcalk_dac_value_factor = AW_PID_2183_VCABLK_DAC_FACTOR;
    aw_pa->vcalb_desc.vscal_dac_factor = AW_PID_2183_VSCAL_DAC_FACTOR;
    aw_pa->vcalb_desc.iscal_dac_factor = AW_PID_2183_ISCAL_DAC_FACTOR;

    /* ─── 系统状态 ─── */
    aw_pa->sysst_desc.reg       = AW_PID_2183_SYSST_REG;
    aw_pa->sysst_desc.st_check  = AW_PID_2183_BIT_SYSST_NOSWS_CHECK;
    aw_pa->sysst_desc.st_mask   = AW_PID_2183_BIT_SYSST_CHECK_MASK;
    aw_pa->sysst_desc.pll_check = AW_PID_2183_BIT_PLL_CHECK;
    aw_pa->sysst_desc.dsp_check = AW_PID_2183_DSPS_NORMAL_VALUE;
    aw_pa->sysst_desc.dsp_mask  = AW_PID_2183_DSPS_MASK;
    aw_pa->sysst_desc.st_sws_check = AW_PID_2183_BIT_SYSST_SWS_CHECK;

    /* ─── Noise Gate ─── */
    aw_pa->noise_gate_en.reg = AW_PID_2183_PWMCTRL3_REG;
    aw_pa->noise_gate_en.noise_gate_mask = AW_PID_2183_NOISE_GATE_EN_MASK;

    /* ─── Profile Control ─── */
    aw_pa->profctrl_desc.reg = AW_PID_2183_SYSCTRL_REG;
    aw_pa->profctrl_desc.mask = AW_PID_2183_RCV_MODE_MASK;
    aw_pa->profctrl_desc.rcv_mode_val = AW_PID_2183_RCV_MODE_RECEIVER_VALUE;

    /* ─── 音量 ─── */
    aw_pa->volume_desc.reg       = AW_PID_2183_SYSCTRL2_REG;
    aw_pa->volume_desc.mask      = AW_PID_2183_VOL_MASK;
    aw_pa->volume_desc.shift     = AW_PID_2183_VOL_START_BIT;
    aw_pa->volume_desc.mute_volume = AW_PID_2183_MUTE_VOLUME;
    aw_pa->volume_desc.max_volume  = AW_PID_2183_VOL_DEFAULT_VALUE;
    aw_pa->volume_desc.ctl_volume  = AW_PID_2183_VOL_DEFAULT_VALUE;

    /* ─── DSP 使能 ─── */
    aw_pa->dsp_en_desc.reg    = AW_PID_2183_SYSCTRL_REG;
    aw_pa->dsp_en_desc.mask   = AW_PID_2183_DSPBY_MASK;
    aw_pa->dsp_en_desc.enable  = AW_PID_2183_DSPBY_WORKING_VALUE;
    aw_pa->dsp_en_desc.disable = AW_PID_2183_DSPBY_BYPASS_VALUE;

    /* ─── 内存时钟 ─── */
    aw_pa->memclk_desc.reg     = AW_PID_2183_DBGCTRL_REG;
    aw_pa->memclk_desc.mask    = AW_PID_2183_MEM_CLKSEL_MASK;
    aw_pa->memclk_desc.mcu_hclk = AW_PID_2183_MEM_CLKSEL_DAPHCLK_VALUE;
    aw_pa->memclk_desc.osc_clk  = AW_PID_2183_MEM_CLKSEL_OSCCLK_VALUE;

    /* ─── 看门狗 ─── */
    aw_pa->watch_dog_desc.reg  = AW_PID_2183_WDT_REG;
    aw_pa->watch_dog_desc.mask = AW_PID_2183_WDT_CNT_MASK;

    /* ─── DSP 内存 ─── */
    aw_pa->dsp_mem_desc.dsp_madd_reg = AW_PID_2183_DSPMADD_REG;
    aw_pa->dsp_mem_desc.dsp_mdat_reg = AW_PID_2183_DSPMDAT_REG;
    aw_pa->dsp_mem_desc.dsp_cfg_base_addr = AW_PID_2183_DSP_CFG_ADDR;
    aw_pa->dsp_mem_desc.dsp_fw_base_addr  = AW_PID_2183_DSP_FW_ADDR;
    aw_pa->dsp_mem_desc.dsp_rom_check_reg  = AW_PID_2183_DSP_ROM_CHECK_ADDR;
    aw_pa->dsp_mem_desc.dsp_rom_check_data = AW_PID_2183_DSP_ROM_CHECK_DATA;

    /* ─── 软复位 ─── */
    aw_pa->soft_rst.reg       = AW_PID_2183_ID_REG;
    aw_pa->soft_rst.reg_value = AW_PID_2183_SOFT_RESET_VALUE;

    /* ─── DSP 音量 ─── */
    aw_pa->dsp_vol_desc.reg      = AW_PID_2183_DSPCFG_REG;
    aw_pa->dsp_vol_desc.mask     = AW_PID_2183_DSP_VOL_MASK;
    aw_pa->dsp_vol_desc.mute_st  = AW_PID_2183_DSP_VOL_MUTE;
    aw_pa->dsp_vol_desc.noise_st = AW_PID_2183_DSP_VOL_NOISE_ST;

    /* ─── 功放掉电 ─── */
    aw_pa->amppd_desc.reg    = AW_PID_2183_SYSCTRL_REG;
    aw_pa->amppd_desc.mask   = AW_PID_2183_AMPPD_MASK;
    aw_pa->amppd_desc.enable  = AW_PID_2183_AMPPD_POWER_DOWN_VALUE;
    aw_pa->amppd_desc.disable = AW_PID_2183_AMPPD_WORKING_VALUE;

    /* ─── 喇叭温度 ─── */
    aw_pa->cali_desc.spkr_temp_desc.reg = AW_PID_2183_ASR2_REG;

    /* ─── 校准：RA ─── */
    aw_pa->cali_desc.ra_desc.dsp_reg  = AW_PID_2183_DSP_REG_CFG_ADPZ_RA;
    aw_pa->cali_desc.ra_desc.data_type = AW_DSP_32_DATA;
    aw_pa->cali_desc.ra_desc.shift    = AW_PID_2183_DSP_CFG_ADPZ_RA_SHIFT;

    /* ─── 校准：ACTAMPTH ─── */
    aw_pa->cali_desc.cali_cfg_desc.actampth_reg = AW_PID_2183_DSP_REG_CFG_MBMEC_ACTAMPTH;
    aw_pa->cali_desc.cali_cfg_desc.actampth_data_type = AW_DSP_32_DATA;

    /* ─── 校准：NOISEAMPTH ─── */
    aw_pa->cali_desc.cali_cfg_desc.noiseampth_reg = AW_PID_2183_DSP_REG_CFG_MBMEC_NOISEAMPTH;
    aw_pa->cali_desc.cali_cfg_desc.noiseampth_data_type = AW_DSP_32_DATA;

    /* ─── 校准：USTEPN ─── */
    aw_pa->cali_desc.cali_cfg_desc.ustepn_reg = AW_PID_2183_DSP_REG_CFG_ADPZ_USTEPN;
    aw_pa->cali_desc.cali_cfg_desc.ustepn_data_type = AW_DSP_16_DATA;

    /* ─── 校准：ALPHAN ─── */
    aw_pa->cali_desc.cali_cfg_desc.alphan_reg = AW_PID_2183_DSP_REG_CFG_RE_ALPHA;
    aw_pa->cali_desc.cali_cfg_desc.alphan_data_type = AW_DSP_16_DATA;

    /* ─── R0 ─── */
    aw_pa->cali_desc.r0_desc.dsp_reg  = AW_PID_2183_DSP_REG_CFG_ADPZ_RE;
    aw_pa->cali_desc.r0_desc.data_type = AW_DSP_16_DATA;
    aw_pa->cali_desc.r0_desc.shift    = AW_PID_2183_ADPZ_RE_SHIFT;

    /* ─── 校准：CALRE ─── */
    aw_pa->cali_desc.dsp_re_desc.dsp_reg  = AW_PID_2183_DSP_REG_CALRE;
    aw_pa->cali_desc.dsp_re_desc.shift    = AW_PID_2183_DSP_REG_CALRE_SHIFT;
    aw_pa->cali_desc.dsp_re_desc.data_type = AW_DSP_16_DATA;

    /* ─── 硬件校准 RE ─── */
    aw_pa->cali_desc.hw_cali_re_desc.hbits_reg   = AW_PID_2183_ACR1_REG;
    aw_pa->cali_desc.hw_cali_re_desc.hbits_mask  = AW_PID_2183_CALI_RE_HBITS_MASK;
    aw_pa->cali_desc.hw_cali_re_desc.hbits_shift = AW_PID_2183_CALI_RE_HBITS_SHIFT;
    aw_pa->cali_desc.hw_cali_re_desc.lbits_reg   = AW_PID_2183_ACR2_REG;
    aw_pa->cali_desc.hw_cali_re_desc.lbits_mask  = AW_PID_2183_CALI_RE_LBITS_MASK;
    aw_pa->cali_desc.hw_cali_re_desc.lbits_shift = AW_PID_2183_CALI_RE_LBITS_SHIFT;
    aw_pa->cali_desc.hw_cali_re_desc.cali_re_shift = AW_PID_2183_DSP_RE_SHIFT;

    /* ─── Noise DSP ─── */
    aw_pa->cali_desc.noise_desc.dsp_reg  = AW_PID_2183_DSP_REG_CFG_MBMEC_GLBCFG;
    aw_pa->cali_desc.noise_desc.data_type = AW_DSP_16_DATA;
    aw_pa->cali_desc.noise_desc.mask     = AW_PID_2183_DSP_REG_NOISE_MASK;

    /* ─── F0 ─── */
    aw_pa->cali_desc.f0_desc.dsp_reg  = AW_PID_2183_DSP_REG_RESULT_F0;
    aw_pa->cali_desc.f0_desc.shift    = AW_PID_2183_DSP_F0_SHIFT;
    aw_pa->cali_desc.f0_desc.data_type = AW_DSP_16_DATA;

    /* ─── Q ─── */
    aw_pa->cali_desc.q_desc.dsp_reg  = AW_PID_2183_DSP_REG_RESULT_Q;
    aw_pa->cali_desc.q_desc.shift    = AW_PID_2183_DSP_Q_SHIFT;
    aw_pa->cali_desc.q_desc.data_type = AW_DSP_16_DATA;

    /* ─── IV ─── */
    aw_pa->cali_desc.iv_desc.reg = AW_PID_2183_ASR1_REG;
    aw_pa->cali_desc.iv_desc.reabs_mask = AW_PID_2183_REABS_MASK;

    /* ─── 校准延时 ─── */
    aw_pa->cali_desc.cali_delay_desc.dsp_reg  = AW_PID_2183_DSP_CALI_F0_DELAY;
    aw_pa->cali_desc.cali_delay_desc.data_type = AW_DSP_16_DATA;

    /* ─── CCO_MUX ─── */
    aw_pa->cco_mux_desc.reg     = AW_PID_2183_PLLCTRL2_REG;
    aw_pa->cco_mux_desc.mask    = AW_PID_2183_CCO_MUX_MASK;
    aw_pa->cco_mux_desc.divider = AW_PID_2183_CCO_MUX_DIVIDED_VALUE;
    aw_pa->cco_mux_desc.bypass  = AW_PID_2183_CCO_MUX_BYPASS_VALUE;

    /* ─── DSP 状态区域 ─── */
    aw_pa->dsp_st_desc.dsp_reg_s1 = AW_PID_2183_DSP_ST_S1;
    aw_pa->dsp_st_desc.dsp_reg_e1 = AW_PID_2183_DSP_ST_E1;
    aw_pa->dsp_st_desc.dsp_reg_s2 = AW_PID_2183_DSP_ST_S2;
    aw_pa->dsp_st_desc.dsp_reg_e2 = AW_PID_2183_DSP_ST_E2;

    /* ─── CRC ─── */
    aw_pa->crc_check_desc.ram_clk_reg = AW_PID_2183_I2SCFG1_REG;
    aw_pa->crc_check_desc.ram_clk_mask = AW_PID_2183_RAM_CG_BYP_MASK;
    aw_pa->crc_check_desc.ram_clk_on   = AW_PID_2183_RAM_CG_BYP_BYPASS_VALUE;
    aw_pa->crc_check_desc.ram_clk_off  = AW_PID_2183_RAM_CG_BYP_WORK_VALUE;
    aw_pa->crc_check_desc.crc_cfg_base_addr = AW_PID_2183_CRC_CFG_BASE_ADDR;
    aw_pa->crc_check_desc.crc_fw_base_addr  = AW_PID_2183_CRC_FW_BASE_ADDR;

    aw_pa->crc_check_desc.crc_ctrl_reg = AW_PID_2183_CRCCTRL_REG;
    aw_pa->crc_check_desc.crc_end_addr_mask = AW_PID_2183_CRC_END_ADDR_MASK;
    aw_pa->crc_check_desc.crc_cfg_check_en_mask = AW_PID_2183_CRC_CFG_EN_MASK;
    aw_pa->crc_check_desc.crc_cfgcheck_disable  = AW_PID_2183_CRC_CFG_EN_DISABLE_VALUE;
    aw_pa->crc_check_desc.crc_cfgcheck_enable   = AW_PID_2183_CRC_CFG_EN_ENABLE_VALUE;
    aw_pa->crc_check_desc.crc_fw_check_en_mask  = AW_PID_2183_CRC_CODE_EN_MASK;
    aw_pa->crc_check_desc.crc_fwcheck_disable   = AW_PID_2183_CRC_CODE_EN_DISABLE_VALUE;
    aw_pa->crc_check_desc.crc_fwcheck_enable    = AW_PID_2183_CRC_CODE_EN_ENABLE_VALUE;

    aw_pa->crc_check_desc.crc_check_reg  = AW_PID_2183_HAGCST_REG;
    aw_pa->crc_check_desc.crc_check_mask = AW_PID_2183_CRC_CHECK_BITS_MASK;
    aw_pa->crc_check_desc.crc_check_pass = AW_PID_2183_CRC_CHECK_PASS_VAL;
    aw_pa->crc_check_desc.crc_check_bits_shift = AW_PID_2183_CRC_CHECK_START_BIT;

    /* ─── CRC 实时 ─── */
    aw_pa->crc_check_realtime_desc.addr = AW_PID_2183_CRC_REALTIME_ADDR;
    aw_pa->crc_check_realtime_desc.mask = AW_PID_2183_CRC_REALTIME_MASK;
    aw_pa->crc_check_realtime_desc.data_type  = AW_DSP_16_DATA;
    aw_pa->crc_check_realtime_desc.enable     = AW_PID_2183_CRC_REALTIME_ENABLE;
    aw_pa->crc_check_realtime_desc.disable    = AW_PID_2183_CRC_REALTIME_DISABLE;
    aw_pa->crc_check_realtime_desc.check_addr = AW_PID_2183_CRC_REALTIME_CHECK_ADDR;
    aw_pa->crc_check_realtime_desc.check_mask = AW_PID_2183_CRC_REALTIME_CHECK_MASK;
    aw_pa->crc_check_realtime_desc.check_abnormal = AW_PID_2183_CRC_REALTIME_CHECK_ABNORMAL;
    aw_pa->crc_check_realtime_desc.check_normal   = AW_PID_2183_CRC_REALTIME_CHECK_NORMAL;
    aw_pa->crc_check_realtime_desc.check_data_type = AW_DSP_16_DATA;

    /* ─── 固件版本 ─── */
    aw_pa->fw_ver_desc.version_reg  = AW_PID_2183_FIRMWARE_VERSION_REG;
    aw_pa->fw_ver_desc.data_type    = AW_DSP_16_DATA;

    /* ─── Dither ─── */
    aw_pa->dither_desc.reg    = AW_PID_2183_DBGCTRL_REG;
    aw_pa->dither_desc.mask   = AW_PID_2183_DITHER_EN_MASK;
    aw_pa->dither_desc.enable  = AW_PID_2183_DITHER_EN_ENABLE_VALUE;
    aw_pa->dither_desc.disable = AW_PID_2183_DITHER_EN_DISABLE_VALUE;

    /* ─── 关联 driver ─── */
    drv->aw_pa = aw_pa;

    /* ─── 启动探测 ─── */
    return aw883xx_device_probe(aw_pa, &drv->init_info);
}
