#include "tf_card.h"
#include "tf_card_driver.h"
#include "esp_log.h"
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "tf_card";

static void listDir(const char *path, int depth)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "打开目录失败: %s", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char fullPath[512];
        int written = snprintf(fullPath, sizeof(fullPath),
                               "%s/%s", path, entry->d_name);
        if (written >= sizeof(fullPath)) {
            ESP_LOGW(TAG, "路径过长: %s/%s", path, entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(fullPath, &st) != 0) {
            ESP_LOGW(TAG, "stat 失败: %s", fullPath);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "%*s[DIR]  %s/", depth * 2, "", entry->d_name);
            listDir(fullPath, depth + 1);
        } else {
            const char *unit = "B";
            uint32_t sz = st.st_size;
            if (sz >= 1024)      { sz /= 1024; unit = "KB"; }
            if (sz >= 1024)      { sz /= 1024; unit = "MB"; }
            ESP_LOGI(TAG, "%*s%-20s %6lu %s", depth * 2, "",
                     entry->d_name, (unsigned long)sz, unit);
        }
    }

    closedir(dir);
}

esp_err_t tfCard_listFiles(void)
{
    esp_err_t ret = tfCardDriver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF 卡初始化失败，无法列出文件");
        return ret;
    }

    ESP_LOGI(TAG, "=== TF 卡文件列表 ===");
    listDir(tfCardDriver_getMountPoint(), 0);
    ESP_LOGI(TAG, "=== 列表结束 ===");
    return ESP_OK;
}
