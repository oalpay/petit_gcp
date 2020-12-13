#include "unity.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include "gcp_app.h"
#include "gcp_app_client.h"
#include "gcp_jwt.h"
#include "device_data.h"
#include "wifi_helper.h"
#include "esp_wifi.h"

#include "test_data.h"

#define TAG "TEST"

#define TEST_USER_CONTEXT "TEST"

extern const uint8_t gcp_jwt_private_pem_key_start[] asm("_binary_rsa_private_pem_start");
extern const uint8_t gcp_jwt_private_pem_key_end[] asm("_binary_rsa_private_pem_end");

extern const uint8_t iot_google_pem_key_start[] asm("_binary_google_roots_pem_start");
extern const uint8_t iot_google_pem_key_end[] asm("_binary_google_roots_pem_end");

static char *jwt_callback(const char *project_id)
{
    ESP_LOGI(TAG, "[jwt_callback]");
    return create_GCP_JWT(project_id, (const char *)gcp_jwt_private_pem_key_start, gcp_jwt_private_pem_key_end - gcp_jwt_private_pem_key_start);
}

#define CONNECTION_TIMEOUT_MS 30 * 1000
#define GCP_APP_CONNECTED BIT1
#define GCP_CONFIG_RECEIVED BIT2

EventGroupHandle_t test_event_group;

static void app_connected_callback(gcp_app_handle_t client, void *user_context)
{
    ESP_LOGI(TAG, "[app_connected_callback]");
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL_STRING(TEST_USER_CONTEXT, user_context);
    xEventGroupSetBits(test_event_group, GCP_APP_CONNECTED);
    gcp_app_log(client, "tesdt");
}

static void app_disconnected_callback(gcp_app_handle_t client, void *user_context)
{
    ESP_LOGI(TAG, "[app_disconnected_callback]");
}

static void app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context)
{
    ESP_LOGI(TAG, "[app_config_callback]");
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING(TEST_USER_CONTEXT, user_context);
    xEventGroupSetBits(test_event_group, GCP_CONFIG_RECEIVED);
}

static void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context)
{
    ESP_LOGI(TAG, "[app_command_callback]");
}

gcp_app_state_handle_t app_get_state_callback(gcp_app_handle_t client, void *user_context)
{
    ESP_LOGI(TAG, "[app_get_state_callback]");
    return NULL;
}

wifi_credentials_t default_wifi_credentials = {
    .ssid = WIFI_SSID,
    .passphrase = WIFI_PASSPHARSE};

#define TOPIC_ROOT "/devices/" GCP_DEVICE_ID "/events/"
#define TOPIC_LOG "tlogs"
#define TOPIC_PULSE "tpulse"

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
    .topic_path_log = TOPIC_LOG,
    .topic_path_pulse = TOPIC_PULSE,
    .user_context = TEST_USER_CONTEXT};

void test_gcp_app_init()
{
    gcp_app_handle_t gcp_app_handle = gcp_app_init(&gcp_app_config);
    TEST_ASSERT_EQUAL(&app_command_callback, gcp_app_handle->app_config->cmd_callback);
    TEST_ASSERT_EQUAL(&app_config_callback, gcp_app_handle->app_config->config_callback);
    TEST_ASSERT_EQUAL(&app_get_state_callback, gcp_app_handle->app_config->state_callback);
    TEST_ASSERT_EQUAL(&app_connected_callback, gcp_app_handle->app_config->connected_callback);
    TEST_ASSERT_EQUAL(&app_disconnected_callback, gcp_app_handle->app_config->disconnected_callback);
    TEST_ASSERT_EQUAL(&jwt_callback, gcp_app_handle->app_config->jwt_callback);
    TEST_ASSERT_EQUAL(TEST_USER_CONTEXT, gcp_app_handle->app_config->user_context);
    TEST_ASSERT_EQUAL_STRING(TOPIC_LOG, gcp_app_handle->app_config->topic_path_log);
    TEST_ASSERT_EQUAL_STRING(TOPIC_PULSE, gcp_app_handle->app_config->topic_path_pulse);
}

void test_gcp_app()
{

    wifi_helper_start(&default_wifi_credentials);
    wifi_wait_connection();
    wifi_helper_set_global_ca_store(iot_google_pem_key_start, iot_google_pem_key_end - iot_google_pem_key_start);

    test_event_group = xEventGroupCreate();
    gcp_app_handle_t gcp_app_handle = gcp_app_init(&gcp_app_config);
    TEST_ASSERT_NOT_NULL(gcp_app_handle);
    gcp_app_start(gcp_app_handle);

    EventBits_t evt_bit = xEventGroupWaitBits(test_event_group, GCP_APP_CONNECTED | GCP_CONFIG_RECEIVED, true, true, CONNECTION_TIMEOUT_MS / portTICK_PERIOD_MS);
    TEST_ASSERT_EQUAL(GCP_APP_CONNECTED, evt_bit & GCP_APP_CONNECTED);
    TEST_ASSERT_EQUAL(GCP_CONFIG_RECEIVED, evt_bit & GCP_CONFIG_RECEIVED);

    ESP_LOGI(TAG, "[test_gcp_app] esp_wifi_stop");
    esp_wifi_stop();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "[test_gcp_app] esp_wifi_start");
    esp_wifi_start();

    evt_bit = xEventGroupWaitBits(test_event_group, GCP_APP_CONNECTED | GCP_CONFIG_RECEIVED, true, true, CONNECTION_TIMEOUT_MS / portTICK_PERIOD_MS);
    TEST_ASSERT_EQUAL(GCP_APP_CONNECTED, evt_bit & GCP_APP_CONNECTED);
    TEST_ASSERT_EQUAL(GCP_CONFIG_RECEIVED, evt_bit & GCP_CONFIG_RECEIVED);
}

void test_device_data()
{
    char *key = "key";
    struct
    {
        char name[10];
    } test_struct;
    strcpy(test_struct.name, "test1");
    gcp_nvs_get_data(key, &test_struct, sizeof(test_struct));
    TEST_ASSERT_EQUAL_STRING("test1", test_struct.name);

    strcpy(test_struct.name, "test2");
    gcp_nvs_set_data(key, &test_struct, sizeof(test_struct));
    strcpy(test_struct.name, "empty");
    gcp_nvs_get_data(key, &test_struct, sizeof(test_struct));
    TEST_ASSERT_EQUAL_STRING("test2", test_struct.name);

    strcpy(test_struct.name, "test3");
    gcp_nvs_delete_data(key, sizeof(test_struct));
    gcp_nvs_get_data(key, &test_struct, sizeof(test_struct));
    TEST_ASSERT_EQUAL_STRING("test3", test_struct.name);
}

void app_main()
{
    UNITY_BEGIN();

    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
    /*
    wifi_helper_start(&default_wifi_credentials);
    wifi_wait_connection();

    gcp_app_handle_t gcp_app_handle = gcp_app_init(&gcp_app_config);
    gcp_app_start(gcp_app_handle);
    */

    RUN_TEST(test_gcp_app_init);
    RUN_TEST(test_gcp_app);
    RUN_TEST(test_device_data);
    UNITY_END();
}