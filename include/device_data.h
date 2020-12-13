#ifndef GCP_DEVICE_DATA__H
#define GCP_DEVICE_DATA__H

#include "esp_err.h"

void *gcp_nvs_get_data(char *name, void *default_data, size_t size);
esp_err_t gcp_nvs_set_data(char *name, void *data, size_t size);
esp_err_t gcp_nvs_delete_data(char *name, size_t size);

#endif