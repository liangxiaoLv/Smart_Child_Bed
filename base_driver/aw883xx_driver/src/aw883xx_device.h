/*
 * aw883xx_device.h — 设备操作层 (adapted from AWINIC aw883xx_device.h)
 *
 * 裁剪：移除 AW_MONITOR，校准结构体内联（不依赖 aw883xx_calib.h）。
 */
#ifndef __AW883XX_DEVICE_H__
#define __AW883XX_DEVICE_H__

#include "aw883xx_base.h"
#include "aw883xx_init.h"
#include "aw_profile_process.h"

#define AW_DEV_DEFAULT_CH          (0)
#define AW_DEV_I2S_CHECK_MAX       (5)
#define AW_DEV_DSP_CHECK_MAX       (5)
#define AW_PROF_NAME_MAX           (50)

/* DSP I2C 块写 */
#define AW_MAX_RAM_WRITE_BYTE_SIZE (128)
#define AW_DSP_ODD_NUM_BIT_TEST    (0x5555)
#define AW_DSP_EVEN_NUM_BIT_TEST   (0xAAAA)
#define AW_DSP_ST_CHECK_MAX        (2)
#define AW_FADE_IN_OUT_DEFAULT     (0)
#define AW_REG_NONE                (0xFF)
#define AW_CALI_DELAY_CACL(value)  (((value) * 32) / 48)

#define AW_FW_ADDR_LEN             (4)
#define AW_CRC_ADDR_LEN            (1)

enum {
    AW_1_MS = 1,  AW_2_MS = 2,  AW_3_MS = 3,  AW_4_MS = 4,  AW_5_MS = 5,
    AW_10_MS = 10, AW_30_MS = 30,
};

enum { AW_DEV_TYPE_OK = 0, AW_DEV_TYPE_NONE = 1 };
enum { AW_DEV_PW_OFF = 0, AW_DEV_PW_ON };
enum { AW_DEV_FW_FAILED = 0, AW_DEV_FW_OK };
enum { AW_DEV_MEMCLK_OSC = 0, AW_DEV_MEMCLK_PLL = 1 };
enum { AW_DEV_DSP_WORK = 0, AW_DEV_DSP_BYPASS = 1 };
enum { AW_DSP_16_DATA = 0, AW_DSP_32_DATA = 1 };
enum { AW_NOT_RCV_MODE = 0, AW_RCV_MODE = 1 };
enum { AW_EF_AND_CHECK = 0, AW_EF_OR_CHECK };
enum { AW_SW_CRC_CHECK = 0, AW_HW_CRC_CHECK };
enum { AW_FW_CRC_CHECK = 0, AW_CFG_CRC_CHECK };
enum { AW_HW_TYPE = 0, AW_DSP_TYPE };
enum { AW_DEV_HMUTE_DISABLE = 0, AW_DEV_HMUTE_ENABLE };
enum { AW_DEV_MASK_INT_VAL = 0, AW_DEV_UNMASK_INT_VAL };
enum { AW_DEV_VDSEL_DAC = 0, AW_DEV_VDSEL_VSENSE = 1 };
enum { AW_DSP_CRC_ON = 0, AW_DSP_CRC_BYPASS = 1 };
enum { AW_DSP_FW_UPDATE_OFF = 0, AW_DSP_FW_UPDATE_ON = 1 };
enum { AW_FORCE_UPDATE_OFF = 0, AW_FORCE_UPDATE_ON = 1 };
enum { AW_QCOM = 0, AW_MTK = 1, AW_SPRD = 2 };

enum fade_time_t { AW_FADE_IN_TIME = 0, AW_FADE_OUT_TIME = 1 };
enum aw_realtime_crc_t { AW_REALTIME_CRC_CHECK_NORMAL = 0, AW_REALTIME_CRC_CHECK_ABNORMAL };

/* ─── 超时 / 延迟常量 ─── */
enum {
    AW_500_US = 500,  AW_1000_US = 1000,  AW_1500_US = 1500,
    AW_2000_US = 2000, AW_3000_US = 3000,  AW_4000_US = 4000,
    AW_5000_US = 5000, AW_10000_US = 10000, AW_100000_US = 100000,
};

/* ═══════════════════════════════════════════════════════════════
 * 寄存器描述符结构体
 * ═══════════════════════════════════════════════════════════════ */

struct aw_int_desc {
    unsigned int mask_reg, st_reg, mask_default, int_mask, intst_mask;
    uint16_t sysint_st;
};

struct aw_pwd_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_mute_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_sysst_desc {
    unsigned int reg, st_check, st_mask, pll_check, dsp_check, dsp_mask, st_sws_check;
};

struct aw_profctrl_desc {
    unsigned int reg, mask, rcv_mode_val, cur_mode;
};

struct aw_volume_desc {
    unsigned int reg, mask, shift, init_volume, mute_volume, ctl_volume, max_volume, monitor_volume;
};

struct aw_dsp_en_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_memclk_desc {
    unsigned int reg, mask, mcu_hclk, osc_clk;
};

struct aw_watch_dog_desc {
    unsigned int reg, mask;
};

struct aw_dsp_mem_desc {
    unsigned int dsp_madd_reg, dsp_mdat_reg, dsp_fw_base_addr, dsp_cfg_base_addr;
    unsigned int dsp_rom_check_reg, dsp_rom_check_data;
};

struct aw_soft_rst {
    uint8_t reg;
    uint16_t reg_value;
};

struct aw_dsp_vol_desc {
    unsigned int reg, mute_st, noise_st, mask;
};

struct aw_amppd_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_dsp_crc_desc {
    unsigned int ctl_reg, ctl_mask, ctl_enable, ctl_disable;
    unsigned int dsp_reg, check_reg;
    unsigned char data_type, check_reg_data_type;
};

struct aw_cco_mux_desc {
    unsigned int reg, mask, divider, bypass;
};

struct aw_re_range_desc {
    uint32_t re_min, re_max;
};

struct aw_dsp_st {
    unsigned int dsp_reg_s1, dsp_reg_e1, dsp_reg_s2, dsp_reg_e2;
};

struct aw_noise_gate_en {
    unsigned int reg, noise_gate_mask;
};

struct aw_fw_ver_desc {
    unsigned int version_reg, data_type;
};

struct aw_dither_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_tx_en_desc {
    unsigned int reg, mask, enable, disable;
};

struct aw_efuse_check_desc {
    unsigned int reg, mask, or_val, and_val, check_val;
};

struct aw_vsense_desc {
    unsigned int vcalb_vsense_reg, vcalk_vdsel_mask, vcalb_vsense_default;
};

struct aw_vcalb_desc {
    unsigned int icalkh_reg, icalkh_reg_mask, icalkh_shift;
    unsigned int icalkl_reg, icalkl_reg_mask, icalkl_shift;
    unsigned int icalk_sign_mask, icalk_neg_mask;
    int icalk_value_factor;
    unsigned int vcalkh_reg, vcalkh_reg_mask, vcalkh_shift;
    unsigned int vcalkl_reg, vcalkl_reg_mask, vcalkl_shift;
    unsigned int vcalk_sign_mask, vcalk_neg_mask;
    int vcalk_value_factor, vscal_factor, iscal_factor;
    unsigned int vcalb_adj_shift, vcalb_reg;
    int cabl_base_value, vcalb_accuracy;
    unsigned int icalkh_dac_reg, icalkh_dac_reg_mask, icalkh_dac_shift;
    unsigned int icalkl_dac_reg, icalkl_dac_reg_mask, icalkl_dac_shift;
    unsigned int icalk_dac_sign_mask, icalk_dac_neg_mask;
    int icalk_dac_value_factor;
    unsigned int vcalkh_dac_reg, vcalkh_dac_reg_mask, vcalkh_dac_shift;
    unsigned int vcalkl_dac_reg, vcalkl_dac_reg_mask, vcalkl_dac_shift;
    unsigned int vcalk_dac_sign_mask, vcalk_dac_neg_mask;
    int vcalk_dac_value_factor, vscal_dac_factor, iscal_dac_factor;
    uint16_t init_value, last_value;
};

struct aw_crc_check_desc {
    unsigned int ram_clk_reg, ram_clk_mask, ram_clk_on, ram_clk_off;
    unsigned int crc_ctrl_reg, crc_end_addr_mask;
    unsigned int crc_fw_check_en_mask, crc_fwcheck_enable, crc_fwcheck_disable;
    unsigned int crc_cfg_check_en_mask, crc_cfgcheck_enable, crc_cfgcheck_disable;
    unsigned int crc_check_reg, crc_check_mask, crc_check_bits_shift, crc_check_pass;
    unsigned int crc_fw_base_addr, crc_cfg_base_addr, crc_init_val;
};

struct aw_crc_check_realtime_desc {
    unsigned int addr, mask, init_switch, status, enable, disable;
    unsigned char data_type;
    unsigned int check_addr, check_mask, check_abnormal, check_normal;
    unsigned char check_data_type;
};

/* ─── 校准描述符（内联，不依赖 aw883xx_calib.h）─ */
#define AW_CALI_CFG_NUM (4)

struct cali_cfg { uint32_t data[AW_CALI_CFG_NUM]; };

struct aw_cali_cfg_desc {
    unsigned int actampth_reg, noiseampth_reg, ustepn_reg, alphan_reg;
    unsigned char actampth_data_type, noiseampth_data_type, ustepn_data_type;
    unsigned int alphan_data_type;
};

struct aw_noise_desc    { unsigned int dsp_reg, mask; unsigned char data_type; };
struct aw_f0_desc       { unsigned int dsp_reg, shift; unsigned char data_type; };
struct aw_q_desc        { unsigned int dsp_reg, shift; unsigned char data_type; };
struct aw_dsp_cali_re_desc { unsigned int dsp_reg, shift; unsigned char data_type; };
struct aw_r0_desc       { unsigned int dsp_reg, shift, init_value; unsigned char data_type; };
struct aw_ra_desc       { unsigned int dsp_reg, shift; unsigned char data_type; };

struct aw_hw_cali_re_desc {
    unsigned int hbits_reg, lbits_reg, hbits_mask, hbits_shift;
    unsigned int lbits_mask, lbits_shift, cali_re_shift;
    uint16_t re_hbits, re_lbits;
};

struct aw_spkr_temp_desc  { unsigned int reg; };
struct aw_cali_delay_desc { unsigned int dsp_reg, delay; unsigned char data_type; };
struct aw_cali_iv_desc    { unsigned int reg, reabs_mask; };

struct aw_cali_desc {
    bool status;
    struct cali_cfg cali_cfg;
    uint16_t store_vol;
    uint32_t cali_re, re, f0, q, ra;
    int8_t cali_result;
    unsigned char cali_check_st;
    struct aw_cali_cfg_desc cali_cfg_desc;
    struct aw_ra_desc ra_desc;
    struct aw_noise_desc noise_desc;
    struct aw_f0_desc f0_desc;
    struct aw_q_desc q_desc;
    struct aw_dsp_cali_re_desc dsp_re_desc;
    struct aw_r0_desc r0_desc;
    struct aw_spkr_temp_desc spkr_temp_desc;
    struct aw_hw_cali_re_desc hw_cali_re_desc;
    struct aw_cali_delay_desc cali_delay_desc;
    struct aw_cali_iv_desc iv_desc;
};

/* ═══════════════════════════════════════════════════════════════
 * aw_device_ops — 函数指针表
 * ═══════════════════════════════════════════════════════════════ */
struct aw_device;

struct aw_device_ops {
    int (*aw_i2c_writes)(struct aw_device *aw_dev, uint8_t reg_addr, uint8_t *buf, uint16_t len);
    int (*aw_i2c_write)(struct aw_device *aw_dev, uint8_t reg_addr, uint16_t reg_data);
    int (*aw_i2c_read)(struct aw_device *aw_dev, uint8_t reg_addr, uint16_t *reg_data);
    int (*aw_reg_write)(struct aw_device *aw_dev, uint8_t reg_addr, uint16_t reg_data);
    int (*aw_reg_read)(struct aw_device *aw_dev, uint8_t reg_addr, uint16_t *reg_data);
    int (*aw_reg_write_bits)(struct aw_device *aw_dev, uint8_t reg_addr, uint16_t mask, uint16_t reg_data);
    int (*aw_dsp_write)(struct aw_device *aw_dev, uint16_t dsp_addr, uint32_t reg_data, uint8_t data_type);
    int (*aw_dsp_read)(struct aw_device *aw_dev, uint16_t dsp_addr, uint32_t *dsp_data, uint8_t data_type);
    int (*aw_dsp_write_bits)(struct aw_device *aw_dev, uint16_t dsp_addr,
                              uint32_t dsp_mask, uint32_t dsp_data, uint8_t data_type);
    int (*aw_set_hw_volume)(struct aw_device *aw_dev, uint16_t value);
    int (*aw_get_hw_volume)(struct aw_device *aw_dev, uint16_t *value);
    unsigned int (*aw_reg_val_to_db)(unsigned int value);
    void (*aw_i2s_tx_enable)(struct aw_device *aw_dev, bool flag);
    int (*aw_get_dev_num)(void);
    bool (*aw_check_wr_access)(int reg);
    bool (*aw_check_rd_access)(int reg);
    int (*aw_get_reg_num)(void);
    int (*aw_get_version)(char *buf, int size);
    int (*aw_read_dsp_pid)(struct aw_device *aw_dev);
    int (*aw_set_vcalb)(struct aw_device *aw_dev);
    int (*aw_sw_crc_check)(struct aw_device *aw_dev);
    int (*aw_set_cali_re)(struct aw_device *aw_dev);
    int (*aw_get_cali_re)(struct aw_device *aw_dev, uint32_t *re);
    int (*aw_get_r0)(struct aw_device *aw_dev, uint32_t *re);
    int (*aw_init_vcalb_update)(struct aw_device *aw_dev, backup_sec_t flag);
    int (*aw_init_re_update)(struct aw_device *aw_dev, backup_sec_t flag);
};

/* ═══════════════════════════════════════════════════════════════
 * struct aw_device — 设备描述符
 * ═══════════════════════════════════════════════════════════════ */
struct aw_container { int len; uint8_t data[]; };

struct aw_device {
    int status;
    char cur_prof_name[AW_PROF_NAME_MAX];
    char set_prof_name[AW_PROF_NAME_MAX];
    char first_prof_name[AW_PROF_NAME_MAX];
    unsigned char dsp_crc_st;
    unsigned int chip_id;
    unsigned int dither_st;
    unsigned int txen_st;
    aw_dev_index_t dev;
    uint32_t fw_ver;
    unsigned int fade_step;
    void *private_data;
    aw_fade_en_t fade_en;
    unsigned char dsp_cfg;
    uint32_t dsp_fw_len;
    uint32_t dsp_cfg_len;
    uint8_t fw_status;
    uint8_t crc_type;

    struct aw_prof_info *prof_info;
    struct aw_container   *cont;

    struct aw_int_desc    int_desc;
    struct aw_pwd_desc    pwd_desc;
    struct aw_mute_desc   mute_desc;
    struct aw_sysst_desc  sysst_desc;
    struct aw_profctrl_desc profctrl_desc;
    struct aw_volume_desc volume_desc;
    struct aw_dsp_en_desc dsp_en_desc;
    struct aw_memclk_desc memclk_desc;
    struct aw_watch_dog_desc watch_dog_desc;
    struct aw_dsp_mem_desc dsp_mem_desc;
    struct aw_noise_gate_en noise_gate_en;
    struct aw_crc_check_realtime_desc crc_check_realtime_desc;
    struct aw_soft_rst    soft_rst;
    struct aw_crc_check_desc crc_check_desc;
    struct aw_vsense_desc vsense_desc;
    struct aw_vcalb_desc  vcalb_desc;
    struct aw_efuse_check_desc efuse_check_desc;
    struct aw_dsp_vol_desc dsp_vol_desc;
    struct aw_amppd_desc  amppd_desc;
    struct aw_dsp_crc_desc dsp_crc_desc;
    struct aw_cco_mux_desc cco_mux_desc;
    struct aw_cali_desc   cali_desc;
    struct aw_fw_ver_desc fw_ver_desc;
    struct aw_dither_desc dither_desc;
    struct aw_tx_en_desc  txen_desc;
    struct aw_re_range_desc re_range;
    struct aw_dsp_st      dsp_st_desc;
    struct aw_device_ops  ops;
};

struct crc_dsp_cfg {
    uint32_t len;
    unsigned char *data;
};

/* ═══════════════════════════════════════════════════════════════
 * 设备操作函数声明
 * ═══════════════════════════════════════════════════════════════ */
int aw883xx_device_probe(struct aw_device *aw_dev, struct aw_init_info *init_info);
int aw883xx_device_start(struct aw_device *aw_dev);
int aw883xx_device_stop(struct aw_device *aw_dev);
int aw883xx_device_fw_update(struct aw_device *aw_dev, bool up_dsp_fw_en, bool force_up_en);

bool aw883xx_device_status(struct aw_device *aw_dev, dev_status_t type, status_option_t status_ops);
int aw883xx_device_params(struct aw_device *aw_dev, dev_params_t params_type,
                           void *data, uint8_t size, params_option_t params_ops);

int aw883xx_dev_set_profile_name(struct aw_device *aw_dev, const char *prof_name);
char *aw883xx_dev_get_profile_name(struct aw_device *aw_dev);
struct aw_sec_data_desc *aw883xx_dev_get_prof_data_byname(struct aw_device *aw_dev,
                                                           char *prof_name, int data_type);
int aw883xx_dev_check_prof(uint32_t dev_index, struct aw_prof_info *prof_info);
int aw883xx_crc_realtime_check(struct aw_device *aw_dev, uint32_t *status);

#endif /* __AW883XX_DEVICE_H__ */
