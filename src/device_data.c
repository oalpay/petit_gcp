#include "device_data.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TAG "DEVICE_DATA"

#define DEVICE_DATA_NVS_NAMESPACE "data_h"

void *gcp_nvs_get_data(char *name, void *default_data, size_t size)
{
    // Open
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_DATA_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        goto end;
    }
    err = nvs_get_blob(nvs, name, default_data, &size);
end:
    nvs_close(nvs);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "[get_data] Problem reading data from nvs: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "[get_data] Writing defaults to nvs");
        gcp_nvs_set_data(name, default_data, size);
    }
    return default_data;
}

esp_err_t gcp_nvs_set_data(char *name, void *data, size_t size)
{
    // Open
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_DATA_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        goto err;
    }
    err = nvs_set_blob(nvs, name, data, size);
    if (err != ESP_OK)
    {
        goto err;
    }
    err = nvs_commit(nvs);
err:
    nvs_close(nvs);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "[get_data] Error writing data: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t gcp_nvs_delete_data(char *name, size_t size)
{
    // Open
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_DATA_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        goto err;
    }
    err = nvs_erase_key(nvs, name);
    if (err != ESP_OK)
    {
        goto err;
    }
    err = nvs_commit(nvs);
err:
    nvs_close(nvs);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "[get_data] Error erasing data: %s", esp_err_to_name(err));
    }
    return err;
}