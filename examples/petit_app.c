
#include "stdio.h"
#include "gcp_app.h"
#include "gcp_jwt.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_helper.h"
#include "../test/test_data.h" /* replace with your own definitions */

#define TAG "PETIT_APP"

extern const uint8_t gcp_jwt_private_pem_key_start[] asm("_binary_rsa_private_pem_start");
extern const uint8_t gcp_jwt_private_pem_key_end[] asm("_binary_rsa_private_pem_end");

extern const uint8_t iot_google_pem_key_start[] asm("_binary_google_roots_pem_start");
extern const uint8_t iot_google_pem_key_end[] asm("_binary_google_roots_pem_end");

/* return your heap allocated, jwt token it will be freed by the framework*/
static char *jwt_callback(const char *project_id)
{
    ESP_LOGI(TAG, "[jwt_callback]");
    return create_GCP_JWT(project_id, (const char *)gcp_jwt_private_pem_key_start, gcp_jwt_private_pem_key_end - gcp_jwt_private_pem_key_start);
}

static void app_connected_callback(gcp_app_handle_t client, void *user_context)
{
    ESP_LOGI(TAG, "[app_connected_callback]");
    gcp_app_log(client, "tesdt");
    gcp_app_send_telemetry(client, "topic_love", "1");
}

static void app_disconnected_callback(gcp_app_handle_t client, void *user_context)
{
    ESP_LOGI(TAG, "[app_disconnected_callback]");
}

static void app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context)
{
    char *pretty_config = cJSON_Print(config);
    ESP_LOGI(TAG, "[app_config_callback] app_config:%s \n", pretty_config);
    free(pretty_config);
}

static void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context)
{
    ESP_LOGI(TAG, "[app_command_callback] topic:%s, cmd:%s", topic, command);
}

static void app_get_state_callback(gcp_app_handle_t client, gcp_app_state_handle_t state, void *user_context)
{
    ESP_LOGI(TAG, "[app_get_state_callback]");
    cJSON_AddStringToObject(state, "desire", "objet petit");
}

void app_main()
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_credentials_t wifi_credentials = {
        .ssid = WIFI_SSID,
        .passphrase = WIFI_PASSPHARSE};
    wifi_helper_start(&wifi_credentials);
    wifi_helper_set_global_ca_store(iot_google_pem_key_start, iot_google_pem_key_end - iot_google_pem_key_start);
    wifi_wait_connection();

    gcp_device_identifiers_t default_gcp_device_identifiers = {
        .registery = REGISTERY,
        .region = REGION,
        .project_id = PROJECT_ID,
        .device_id = DEVICE_ID};

    gcp_app_config_t gcp_app_config = {
        .cmd_callback = &app_command_callback,
        .config_callback = &app_config_callback,
        .state_callback = &app_get_state_callback,
        .connected_callback = &app_connected_callback,
        .disconnected_callback = &app_disconnected_callback,
        .device_identifiers = &default_gcp_device_identifiers,
        .jwt_callback = &jwt_callback,
        .user_context = "What does it matter how many lovers you have if none of them gives you the universe?",
        .ota_server_cert_pem = (const char *)iot_google_pem_key_start};

    gcp_app_handle_t petit_app = gcp_app_init(&gcp_app_config);
    gcp_app_start(petit_app);
}