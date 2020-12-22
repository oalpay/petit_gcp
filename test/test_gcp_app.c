#include "unity.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include "gcp_app.h"
#include "gcp_app_internal.h"
#include "gcp_jwt.h"
#include "device_data.h"
#include "wifi_helper.h"
#include "esp_wifi.h"
#include "test_data.h"

#include "gcp_client.fake.h"
#include "fake_app.h"

DEFINE_FFF_GLOBALS;

#define TAG "TEST"

#define OTA_SERVER_CERT "OTA_SERVER_CERT"
#define USER_CONTEXT "TEST"
#define TOPIC_ROOT "/devices/" GCP_DEVICE_ID "/events/"
#define TOPIC_LOG "tlogs"
#define TOPIC_PULSE "tpulse"

#define TIMER_PERIOD_MS 100
#define TIMER_UPDATED_PERIOD_MS 200

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define CONFIG_UPDATE_TZ "GMT-3"
#define APP_CONFIG_KEY "APP_CONFIG_KEY"
#define APP_CONFIG_VALUE "APP_CONFIG_VALUE"
#define CONFIG_UPDATE "{\"app_config\":{\"" APP_CONFIG_KEY "\":\"" APP_CONFIG_VALUE "\"},\"device_config\":{\"state_period_ms\":" STR(TIMER_UPDATED_PERIOD_MS) ",\"pulse_period_ms\":" STR(TIMER_UPDATED_PERIOD_MS) ",\"tz\":\"" CONFIG_UPDATE_TZ "\"}}"

gcp_device_identifiers_t default_gcp_device_identifiers = {
    .registery = REGISTERY,
    .region = REGION,
    .project_id = PROJECT_ID,
    .device_id = DEVICE_ID};



gcp_app_config_t gcp_app_config = {
    .cmd_callback = &app_command_callback,
    .config_callback = &app_config_callback,
    .state_callback = &app_get_state_callback,
    .state_update_period_ms = TIMER_PERIOD_MS,
    .connected_callback = &app_connected_callback,
    .disconnected_callback = &app_disconnected_callback,
    .device_identifiers = &default_gcp_device_identifiers,
    .jwt_callback = &app_jwt_callback,
    .topic_path_log = TOPIC_LOG,
    .topic_path_pulse = TOPIC_PULSE,
    .pulse_update_period_ms = TIMER_PERIOD_MS,
    .user_context = USER_CONTEXT,
    .ota_server_cert_pem = (const char *)OTA_SERVER_CERT};

void setUp(void)
{
    // Register resets
    RESET_FAKE(gcp_client_init);
    RESET_FAKE(gcp_client_start);
    RESET_FAKE(gcp_send_state);
    RESET_FAKE(gcp_send_telemetry);
    RESET_FAKE(gcp_client_destroy);

    RESET_FAKE(app_connected_callback);
    RESET_FAKE(app_disconnected_callback);
    RESET_FAKE(app_config_callback);
    RESET_FAKE(app_command_callback);
    RESET_FAKE(app_get_state_callback);
    RESET_FAKE(app_jwt_callback);
}

void test_gcp_app_init_and_destroy()
{
    gcp_app_handle_t gcp_app_handle = gcp_app_init(&gcp_app_config);
    TEST_ASSERT_EQUAL_MESSAGE(&app_command_callback, gcp_app_handle->app_config->cmd_callback, "app_command_callback");
    TEST_ASSERT_EQUAL_MESSAGE(&app_config_callback, gcp_app_handle->app_config->config_callback, "app_config_callback");
    TEST_ASSERT_EQUAL_MESSAGE(&app_get_state_callback, gcp_app_handle->app_config->state_callback, "app_get_state_callback");
    TEST_ASSERT_EQUAL_MESSAGE(TIMER_PERIOD_MS, gcp_app_handle->app_config->state_update_period_ms, "state_update_period_ms");
    TEST_ASSERT_EQUAL_MESSAGE(&app_connected_callback, gcp_app_handle->app_config->connected_callback, "app_connected_callback");
    TEST_ASSERT_EQUAL_MESSAGE(&app_disconnected_callback, gcp_app_handle->app_config->disconnected_callback, "app_disconnected_callback");
    TEST_ASSERT_EQUAL_MESSAGE(&app_jwt_callback, gcp_app_handle->app_config->jwt_callback, "app_jwt_callback");
    TEST_ASSERT_EQUAL_MESSAGE(OTA_SERVER_CERT, gcp_app_handle->app_config->ota_server_cert_pem, OTA_SERVER_CERT);
    TEST_ASSERT_EQUAL_MESSAGE(USER_CONTEXT, gcp_app_handle->app_config->user_context, USER_CONTEXT);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TOPIC_LOG, gcp_app_handle->app_config->topic_path_log, TOPIC_LOG);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TOPIC_PULSE, gcp_app_handle->app_config->topic_path_pulse, TOPIC_PULSE);
    TEST_ASSERT_EQUAL_MESSAGE(TIMER_PERIOD_MS, gcp_app_handle->app_config->pulse_update_period_ms, "pulse_update_period_ms");
    TEST_ASSERT_EQUAL_MESSAGE(1, gcp_client_init_fake.call_count, "gcp_client_init_fake.call_count");
    TEST_ASSERT_EQUAL_MESSAGE(app_jwt_callback, gcp_client_init_fake.arg0_val->jwt_callback, "gcp_client_init_fake.arg0_val->jwt_callback");

    gcp_app_destroy(gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(gcp_client_destroy_fake.call_count, 1, "gcp_client_destroy_fake.call_count");
}

void mock_app_get_state_callback(gcp_app_handle_t client, gcp_app_state_handle_t state, void *user_context)
{
    cJSON_AddStringToObject(state, "desire", "objet petit");
}

char *g_app_config_value;
void mock_app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context)
{
    const cJSON *app_config = cJSON_GetObjectItem(config, APP_CONFIG_KEY);
    g_app_config_value = strdup(app_config->valuestring);
}

void test_gcp_app()
{
    struct gcp_client_t
    {
    } mock_client;
    gcp_client_handle_t mock_gcp_client_handle = &mock_client;
    gcp_client_init_fake.return_val = mock_gcp_client_handle;
    /* test start */
    gcp_app_handle_t gcp_app_handle = gcp_app_init(&gcp_app_config);
    gcp_app_start(gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(mock_gcp_client_handle, gcp_client_start_fake.arg0_val, "gcp_client_start_fake.arg0_val");
    TEST_ASSERT_EQUAL_MESSAGE(1, gcp_client_start_fake.call_count, "gcp_client_start_fake.call_count");

    /* test connected */
    app_get_state_callback_fake.custom_fake = mock_app_get_state_callback;
    gcp_app_connected_callback(mock_gcp_client_handle, gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(1, app_connected_callback_fake.call_count, "application connected callback called");
    vTaskDelay(TIMER_PERIOD_MS * 1.5 / portTICK_PERIOD_MS); //give some extra time for timers to do their work
    TEST_ASSERT_EQUAL_MESSAGE(1, app_get_state_callback_fake.call_count, "application state callback called");
    TEST_ASSERT_EQUAL_MESSAGE(1, gcp_send_state_fake.call_count, "state sent");
    TEST_ASSERT_EQUAL_MESSAGE(1, gcp_send_telemetry_fake.call_count, "pulse telemetry sent");

    /* test config received */
    app_config_callback_fake.custom_fake = mock_app_config_callback;
    gcp_app_config_callback(mock_gcp_client_handle, CONFIG_UPDATE, gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(1, app_config_callback_fake.call_count, "app_config_callback_fake.call_count");
    TEST_ASSERT_EQUAL_MESSAGE(TIMER_UPDATED_PERIOD_MS, gcp_app_handle->app_config->state_update_period_ms, "config update state period");
    TEST_ASSERT_EQUAL_MESSAGE(TIMER_UPDATED_PERIOD_MS, gcp_app_handle->app_config->pulse_update_period_ms, "config update pulse period");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(CONFIG_UPDATE_TZ, getenv("TZ"), "config update timezone");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(APP_CONFIG_VALUE, g_app_config_value, "APP_CONFIG_VALUE");

    /* test disconnect */
    setUp();
    gcp_app_disconnected_callback(mock_gcp_client_handle, gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(1, app_disconnected_callback_fake.call_count, "app_disconnected_callback_fake.call_count");
    vTaskDelay(TIMER_UPDATED_PERIOD_MS * 2 / portTICK_PERIOD_MS); //give some extra time for timers to do their work
    TEST_ASSERT_EQUAL_MESSAGE(0, app_get_state_callback_fake.call_count, "application state callback called after disconnect");
    TEST_ASSERT_EQUAL_MESSAGE(0, gcp_send_state_fake.call_count, "state sent after disconnect");
    TEST_ASSERT_EQUAL_MESSAGE(0, gcp_send_telemetry_fake.call_count, "pulse telemetry sent after disconnect");

    /* test re-connect */
    gcp_app_connected_callback(mock_gcp_client_handle, gcp_app_handle);
    TEST_ASSERT_EQUAL_MESSAGE(1, app_connected_callback_fake.call_count, "application connected callback called");
    vTaskDelay(TIMER_UPDATED_PERIOD_MS * 3 / portTICK_PERIOD_MS); //give some extra time for timers to do their work
    TEST_ASSERT_GREATER_THAN_MESSAGE(1, app_get_state_callback_fake.call_count, "application state callback called after reconnect");
    TEST_ASSERT_EQUAL_MESSAGE(1, gcp_send_state_fake.call_count, "state sent after reconnect");
    TEST_ASSERT_GREATER_THAN_MESSAGE(1, gcp_send_telemetry_fake.call_count, "pulse telemetry sent after reconnect");
    
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
    RUN_TEST(test_gcp_app_init_and_destroy);
    RUN_TEST(test_gcp_app);
    //RUN_TEST(test_device_data);
    UNITY_END();
}