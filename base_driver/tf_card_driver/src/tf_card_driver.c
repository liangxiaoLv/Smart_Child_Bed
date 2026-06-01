#include "tf_card_driver.h"
#include "pin_map.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

static const char *TAG = "tf_card_drv";
static const char *s_mountPoint = "/sdcard";
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t tfCardDriver_init(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "已挂载，跳过");
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SPI2_MOSI_PIN,
        .miso_io_num     = SPI2_MISO_PIN,
        .sclk_io_num     = SPI2_CLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4092,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST_ID, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI2 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST_ID;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
    slot_cfg.gpio_cs = SD_CARD_CS_PIN;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(s_mountPoint, &host, &slot_cfg,
                                   &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF 卡挂载失败: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST_ID);
        return ret;
    }

    ESP_LOGI(TAG, "TF 卡已挂载 %s", s_mountPoint);
    s_mounted = true;
    return ESP_OK;
}

const char *tfCardDriver_getMountPoint(void)
{
    return s_mounted ? s_mountPoint : NULL;
}

esp_err_t tfCardDriver_deinit(void)
{
    if (!s_mounted) return ESP_OK;

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mountPoint, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "卸载失败: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_bus_free(SPI2_HOST_ID);
    s_card    = NULL;
    s_mounted = false;
    ESP_LOGI(TAG, "TF 卡已卸载");
    return ESP_OK;
}
