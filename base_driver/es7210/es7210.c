/**
 ****************************************************************************************************
 * @file        es7210.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       es7210驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "es7210.h"


const char* es7210_tag = "es7210";
i2c_master_dev_handle_t es7210_handle = NULL;

typedef struct {
    uint32_t mclk;            /* mclk 频率 */
    uint32_t lrck;            /* lrck */
    uint8_t  ss_ds;           /* 未使用 */
    uint8_t  adc_div;         /* adc 时钟分频器 */
    uint8_t  dll;             /* dll 旁路 */
    uint8_t  doubler;         /* 倍频器启用 */
    uint8_t  osr;             /* adc 过采样率 */
    uint8_t  mclk_src;        /* 选择 mclk 源 */
    uint32_t lrck_h;          /* lrck 的高 4 位 */
    uint32_t lrck_l;          /* lrck 的低 8 位 */
} coeff_div_t;

static const coeff_div_t es7210_coeff_div[] = {/* 定义时钟树:需根据芯片修改 */
    /* 8k */
    {12288000,  8000,  0x00,  0x03,  0x01,  0x00,  0x20,  0x00,    0x06,  0x00},
    {16384000,  8000,  0x00,  0x04,  0x01,  0x00,  0x20,  0x00,    0x08,  0x00},
    {19200000,  8000,  0x00,  0x1e,  0x00,  0x01,  0x28,  0x00,    0x09,  0x60},
    {4096000,   8000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},

    /* 11.025k */
    {11289600,  11025,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x01,  0x00},

    /* 12k */
    {12288000,  12000,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x04,  0x00},
    {19200000,  12000,  0x00,  0x14,  0x00,  0x01,  0x28,  0x00,    0x06,  0x40},

    /* 16k */
    {4096000,   16000,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},
    {19200000,  16000,  0x00,  0x0a,  0x00,  0x00,  0x1e,  0x00,    0x04,  0x80},
    {16384000,  16000,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x04,  0x00},
    {12288000,  16000,  0x00,  0x03,  0x01,  0x01,  0x20,  0x00,    0x03,  0x00},

    /* 22.05k */
    {11289600,  22050,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},

    /* 24k */
    {12288000,  24000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},
    {19200000,  24000,  0x00,  0x0a,  0x00,  0x01,  0x28,  0x00,    0x03,  0x20},

    /* 32k */
    {12288000,  32000,  0x00,  0x03,  0x00,  0x00,  0x20,  0x00,    0x01,  0x80},
    {16384000,  32000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},
    {19200000,  32000,  0x00,  0x05,  0x00,  0x00,  0x1e,  0x00,    0x02,  0x58},

    /* 44.1k */
    {11289600,  44100,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},

    /* 48k */
    {12288000,  48000,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},
    {19200000,  48000,  0x00,  0x05,  0x00,  0x01,  0x28,  0x00,    0x01,  0x90},

    /* 64k */
    {16384000,  64000,  0x01,  0x01,  0x01,  0x00,  0x20,  0x00,    0x01,  0x00},
    {19200000,  64000,  0x00,  0x05,  0x00,  0x01,  0x1e,  0x00,    0x01,  0x2c},

    /* 88.2k */
    {11289600,  88200,  0x01,  0x01,  0x01,  0x01,  0x20,  0x00,    0x00,  0x80},

    /* 96k */
    {12288000,  96000,  0x01,  0x01,  0x01,  0x01,  0x20,  0x00,    0x00,  0x80},
    {19200000,  96000,  0x01,  0x05,  0x00,  0x01,  0x28,  0x00,    0x00,  0xc8},
};

/* 无需修改，时钟表查表函数，仅在c文件中声明，防止重复定义 */
static const coeff_div_t *es7210_get_coeff(uint32_t mclk, uint32_t lrck)
{
    for (int i = 0; i < sizeof(es7210_coeff_div) / sizeof(coeff_div_t); i++) 
    {
        if (es7210_coeff_div[i].lrck == lrck && es7210_coeff_div[i].mclk == mclk) 
        {
            return &es7210_coeff_div[i];
        }
    }
    return NULL;
}

/**
 * @brief       ES7210写寄存器
 * @param       reg_addr: 寄存器地址
 * @param       data: 写入的数据
 * @retval      无
 */
esp_err_t es7210_write_reg(uint8_t reg_addr, uint8_t data)
{
    esp_err_t ret;
    uint8_t *buf = malloc(2);

    if (buf == NULL)
    {
        ESP_LOGE(es7210_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    buf[0] = reg_addr;              
    buf[1] = data;                  /* 拷贝数据至存储区当中 */

    do 
    {
        i2c_master_bus_wait_all_done(bus_handle, 1000);
        ret = i2c_master_transmit(es7210_handle, buf, 2, 1000);   
    } while (ret != ESP_OK);

    free(buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       ES7210读寄存器
 * @param       reg_add:寄存器地址
 * @retval      无
 */
esp_err_t es7210_read_reg(uint8_t reg_addr)
{
    uint8_t reg_data = 0;

    i2c_master_transmit_receive(es7210_handle, &reg_addr, 1, &reg_data, 1, -1);
    
    return reg_data;
}

/**
 * @brief       配置ES7210的I2S接口格式
 * @param       i2s_format: I2S数据格式（I2S、LJ、DSP-A、DSP-B）
 * @param       bit_width: I2S数据位宽（16/18/20/24/32位）
 * @param       tdm_enable: 是否使能TDM模式
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_i2s_format(es7210_i2s_fmt_t i2s_format, es7210_i2s_bits_t bit_width, bool tdm_enable)
{
    uint8_t reg_val = 0;

    /* 根据bit_width选择寄存器值，设置I2S数据位宽 */
    switch (bit_width) 
    {
    case ES7210_I2S_BITS_16B:
        reg_val = 0x60;
        break;
    case ES7210_I2S_BITS_18B:
        reg_val = 0x40;
        break;
    case ES7210_I2S_BITS_20B:
        reg_val = 0x20;
        break;
    case ES7210_I2S_BITS_24B:
        reg_val = 0x00;
        break;
    case ES7210_I2S_BITS_32B:
        reg_val = 0x80;
        break;
    default:
        abort(); /* 非法参数，终止程序 */
    }

    /* 配置I2S接口1寄存器，设置数据格式和位宽 */
    es7210_write_reg(ES7210_SDP_INTERFACE1_REG11, i2s_format | reg_val);

    const char *mode_str = NULL;

    /* 根据i2s_format选择寄存器值，设置I2S工作模式 */
    switch (i2s_format) 
    {
    case ES7210_I2S_FMT_I2S:
        reg_val = 0x02;
        mode_str = "standard i2s";
        break;
    case ES7210_I2S_FMT_LJ:
        reg_val = 0x02;
        mode_str = "left justify";
        break;
    case ES7210_I2S_FMT_DSP_A:
        reg_val = 0x01;
        mode_str = "DSP-A";
        break;
    case ES7210_I2S_FMT_DSP_B:
        reg_val = 0x01;
        mode_str = "DSP-B";
        break;
    default:
        abort(); 
    }

    /* 配置I2S接口2寄存器，设置TDM模式或普通模式 */
    if (tdm_enable) 
    {
        es7210_write_reg(ES7210_SDP_INTERFACE2_REG12, reg_val); /* 使能TDM，写入对应模式值 */
    }
    else 
    {
        es7210_write_reg(ES7210_SDP_INTERFACE2_REG12, 0x00); /* 普通模式，写0 */
    }

    /* 打印当前配置到日志，便于调试 */
    ESP_LOGI(es7210_tag, "format: %s, bit width: %d, tdm mode %s", mode_str, bit_width, tdm_enable ? "enabled" : "disabled");
    return ESP_OK;
}

/**
 * @brief       设置ES7210的I2S采样率相关寄存器
 * @param       sample_rate_hz: 采样率（Hz）
 * @param       mclk_ratio: MCLK与采样率的倍频系数
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_i2s_sample_rate(uint32_t sample_rate_hz, uint32_t mclk_ratio)
{
    uint32_t mclk_freq_hz = sample_rate_hz * mclk_ratio;                            /* 计算MCLK频率 */
    const coeff_div_t *coeff_div = es7210_get_coeff(mclk_freq_hz, sample_rate_hz);  /* 查表获取分频参数 */

    /* 设置ADC过采样率（osr） */
    es7210_write_reg(ES7210_OSR_REG07, coeff_div->osr);

    /* 设置ADC分频器、倍频器和DLL旁路 */
    es7210_write_reg(ES7210_MAINCLK_REG02, (coeff_div->adc_div) | (coeff_div->doubler << 6) | (coeff_div->dll << 7));

    /* 设置LRCK分频高8位和低8位 */
    es7210_write_reg(ES7210_LRCK_DIVH_REG04, coeff_div->lrck_h);
    es7210_write_reg(ES7210_LRCK_DIVL_REG05, coeff_div->lrck_l);

    /* 打印采样率和MCLK频率，便于调试 */
    ESP_LOGI(es7210_tag, "sample rate: %"PRIu32"Hz, mclk frequency: %"PRIu32"Hz", sample_rate_hz, mclk_freq_hz);

    return ESP_OK;
}

/**
 * @brief       设置ES7210的麦克风增益
 * @param       mic_gain: 麦克风增益枚举值
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_mic_gain(es7210_mic_gain_t mic_gain)
{
    /* 设置MIC1~MIC4的增益，|0x10为固定配置 */
    es7210_write_reg(ES7210_MIC1_GAIN_REG43, mic_gain | 0x10);
    es7210_write_reg(ES7210_MIC2_GAIN_REG44, mic_gain | 0x10);
    es7210_write_reg(ES7210_MIC3_GAIN_REG45, mic_gain | 0x10);
    es7210_write_reg(ES7210_MIC4_GAIN_REG46, mic_gain | 0x10);

    return ESP_OK;
}

/**
 * @brief       设置ES7210的麦克风偏置电压
 * @param       mic_bias: 麦克风偏置电压枚举值
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_mic_bias(es7210_mic_bias_t mic_bias)
{
    /* 设置MIC1/2和MIC3/4的偏置电压 */
    es7210_write_reg(ES7210_MIC12_BIAS_REG41, mic_bias);
    es7210_write_reg(ES7210_MIC34_BIAS_REG42, mic_bias);

    return ESP_OK;
}

/**
 * @brief       ES7210初始化
 * @param       is_tdm : 是否启用TDM模式
 * @retval      无
 */
void es7210_init(bool is_tdm)
{
    /* 未调用myiic_init初始化IIC */
    if (bus_handle == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t es7210_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,       /* 传输速率 */
        .device_address  = ES7210_ADDRRES,      /* 从机7位的地址 */
    };

    /* I2C总线上添加es7210设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &es7210_i2c_dev_conf, &es7210_handle));
    ESP_ERROR_CHECK(i2c_master_bus_wait_all_done(bus_handle, 1000));

    ESP_LOGI(es7210_tag, "Configure ES7210 codec parameters");

    es7210_codec_config_t codec_conf = {
        .i2s_format = ES7210_I2S_FMT_I2S,
        .mclk_ratio = 256,
        .sample_rate_hz = 48000,
        .bit_width = ES7210_I2S_BITS_16B,
        .mic_bias = ES7210_MIC_BIAS_2V87,
        .mic_gain = ES7210_MIC_GAIN_37_5DB,
        .flags.tdm_enable = is_tdm
    };

    es7210_config_codec(&codec_conf);
    es7210_config_volume(32);
}

/**
 * @brief       配置ES7210编解码器的主要参数
 * @param       codec_conf: 编解码器配置结构体指针
 * @retval      ESP_OK: 配置成功
 */
esp_err_t es7210_config_codec(const es7210_codec_config_t *codec_conf)
{
    /* 软件复位ES7210 */
    es7210_write_reg(ES7210_RESET_REG00, 0xFF);
    es7210_write_reg(ES7210_RESET_REG00, 0x32);

    /* 设置上电初始化时间 */
    es7210_write_reg(ES7210_TIME_CONTROL0_REG09, 0x30);
    es7210_write_reg(ES7210_TIME_CONTROL1_REG0A, 0x30);

    /* 配置ADC1-4的高通滤波器（HPF） */
    es7210_write_reg(ES7210_ADC12_HPF1_REG23, 0x2A);
    es7210_write_reg(ES7210_ADC12_HPF2_REG22, 0x0A);
    es7210_write_reg(ES7210_ADC34_HPF1_REG21, 0x2A);
    es7210_write_reg(ES7210_ADC34_HPF2_REG20, 0x0A);

    /* 设置采样位宽、I2S协议、TDM使能等 */
    es7210_set_i2s_format(codec_conf->i2s_format, codec_conf->bit_width,codec_conf->flags.tdm_enable);

    /* 配置模拟电源和VMID电压 */
    es7210_write_reg(ES7210_ANALOG_REG40, 0xC3);

    /* 设置MIC1-4偏置电压 */
    es7210_set_mic_bias(codec_conf->mic_bias);

    /* 设置MIC1-4增益 */
    es7210_set_mic_gain(codec_conf->mic_gain);

    /* 打开MIC1-4电源 */
    es7210_write_reg(ES7210_MIC1_POWER_REG47, 0x08);
    es7210_write_reg(ES7210_MIC2_POWER_REG48, 0x08);
    es7210_write_reg(ES7210_MIC3_POWER_REG49, 0x08);
    es7210_write_reg(ES7210_MIC4_POWER_REG4A, 0x08);

    /* 设置ADC采样率 */
    es7210_set_i2s_sample_rate(codec_conf->sample_rate_hz, codec_conf->mclk_ratio);

    /* 关闭DLL */
    es7210_write_reg(ES7210_POWER_DOWN_REG06, 0x04);

    /* 打开MIC1-4偏置、ADC1-4、PGA1-4电源 */
    es7210_write_reg(ES7210_MIC12_POWER_REG4B, 0x0F);
    es7210_write_reg(ES7210_MIC34_POWER_REG4C, 0x0F);

    /* 使能设备 */
    es7210_write_reg(ES7210_RESET_REG00, 0x71);
    es7210_write_reg(ES7210_RESET_REG00, 0x41);

    return ESP_OK;
}

/**
 * @brief       配置ES7210的音量（增益）
 * @param       volume_db: 音量（dB）
 * @retval      ESP_OK: 配置成功
 */
esp_err_t es7210_config_volume(int8_t volume_db)
{
    uint8_t reg_val = 191 + volume_db * 2; /* 计算寄存器对应的音量值 */

    /* 设置ADC1~ADC4的音量 */
    es7210_write_reg(ES7210_ADC1_DIRECT_DB_REG1B, reg_val);
    es7210_write_reg(ES7210_ADC2_DIRECT_DB_REG1C, reg_val);
    es7210_write_reg(ES7210_ADC3_DIRECT_DB_REG1D, reg_val);
    es7210_write_reg(ES7210_ADC4_DIRECT_DB_REG1E, reg_val);

    return ESP_OK;
}
