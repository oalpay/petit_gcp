#include "gcp_app.h"
#include "gcp_app_client.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>

#include "gcp_ota.h"

#define TAG "GCP_APP"

#define TOPIC_TELEMETRY_ROOT_FORMAT "/devices/%s/events/%s"
#define TOPIC_DEFAULT_PULSE "pulse"
#define TOPIC_DEFAULT_LOG "logs"

#define JSON_KEY_DEVICE_CONFIG "device_config"
#define JSON_KEY_DEVICE_CONFIG_STATE_PERIOD "state_period_ms"
#define JSON_KEY_DEVICE_FIRMWARE "firmware"
#define JSON_KEY_DEVICE_FIRMWARE_VERSION "version"
#define JSON_KEY_DEVICE_FIRMWARE_URL "url"
#define JSON_KEY_APP_CONFIG "app_config"
#define JSON_KEY_DEVICE_STATE "device_state"
#define JSON_KEY_APP_STATE "app_state"
#define JSON_KEY_FIRMWARE "firmware"
#define JSON_KEY_RSSI "rssi"
#define JSON_KEY_RESET_REASON "reset_reason"

void timer_callback(TimerHandle_t xTimer)
{
    /* Optionally do something if the pxTimer parameter is NULL. */
    configASSERT(xTimer);
    gcp_app_handle_t app_handle = (gcp_app_handle_t)pvTimerGetTimerID(xTimer);
    if (xTimer == app_handle->state_update_timer)
    {
        xEventGroupSetBits(app_handle->app_event_group, GCP_EVENT_STATE_UPDATE_BIT);
    }
    else if (xTimer == app_handle->device_pulse_timer)
    {
        xEventGroupSetBits(app_handle->app_event_group, GCP_EVENT_DEVICE_PULSE_BIT);
    }
}
static void delete_timers(gcp_app_handle_t app_handle)
{
    if (app_handle->state_update_timer != NULL)
    {
        if (xTimerDelete(app_handle->state_update_timer, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "[stop_timers] state_update_timer xTimerDelete fail");
        }
    }
    if (app_handle->device_pulse_timer != NULL)
    {
        if (xTimerDelete(app_handle->device_pulse_timer, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "[stop_timers] device_pulse_timer xTimerDelete fail");
        }
    }
}
static void stop_timers(gcp_app_handle_t app_handle)
{
    if (app_handle->state_update_timer != NULL)
    {
        if (xTimerStop(app_handle->state_update_timer, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "[stop_timers] state_update_timer xTimerStop fail");
        }
    }
    if (app_handle->device_pulse_timer != NULL)
    {
        if (xTimerStop(app_handle->device_pulse_timer, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "[stop_timers] device_pulse_timer xTimerStop fail");
        }
    }
}
static void start_timers(gcp_app_handle_t app_handle)
{
    if (app_handle->state_update_timer == NULL)
    {
        app_handle->state_update_timer = xTimerCreate(
            "state_update_timer",
            app_handle->state_update_period_ms / portTICK_PERIOD_MS,
            pdTRUE,
            (void *)app_handle,
            timer_callback);
    }
    if (xTimerStart(app_handle->state_update_timer, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "[init_timers_and_queue] state_update_timer xTimerStart fail");
    }
    if (app_handle->device_pulse_timer == NULL)
    {
        app_handle->device_pulse_timer = xTimerCreate(
            "device_pulse_timer",
            CONFIG_DEVICE_PULSE_PERIOD_MS / portTICK_PERIOD_MS,
            pdTRUE,
            (void *)app_handle,
            timer_callback);
    }
    if (xTimerStart(app_handle->device_pulse_timer, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "[init_timers_and_queue] device_pulse_timer xTimerStart fail");
    }
}

static void set_state_update_period(gcp_app_handle_t app_handle, uint32_t new_period)
{
    if (new_period == app_handle->state_update_period_ms)
    {
        return;
    }
    if (new_period <= CONFIG_STATE_PERIOD_MS_MIN)
    {
        ESP_LOGE(TAG, "Error: [set_state_update_period] config state update period '%d' less than minimum", new_period);
        return;
    }
    if (xTimerChangePeriod(app_handle->state_update_timer, new_period / portTICK_PERIOD_MS, 100) == pdPASS)
    {
        ESP_LOGI(TAG, "[set_state_update_period] state update period changed to:%d", new_period);
        app_handle->state_update_period_ms = new_period;
    }
    else
    {
        ESP_LOGE(TAG, "[set_state_update_period] error changing period");
    }
}

static void gcp_app_device_config_received(gcp_app_handle_t app_handle, cJSON *device_config)
{
    const cJSON *state_period_ms = cJSON_GetObjectItem(device_config, JSON_KEY_DEVICE_CONFIG_STATE_PERIOD);
    if (cJSON_IsNumber(state_period_ms))
    {
        set_state_update_period(app_handle, state_period_ms->valueint);
    }
    const cJSON *firmware = cJSON_GetObjectItem(device_config, JSON_KEY_DEVICE_FIRMWARE);
    if (cJSON_IsObject(firmware))
    {
        const cJSON *firmware_version = cJSON_GetObjectItem(firmware, JSON_KEY_DEVICE_FIRMWARE_VERSION);
        const cJSON *firmware_url = cJSON_GetObjectItem(firmware, JSON_KEY_DEVICE_FIRMWARE_URL);
        if (cJSON_IsString(firmware_version) && cJSON_IsString(firmware_url))
        {
            char device_firmware_version[32];
            gcp_ota_get_running_app_version(device_firmware_version);
            if (strcmp(device_firmware_version, firmware_version->valuestring) != 0)
            {
                ESP_LOGI(TAG, "[gcp_app_device_config_received] current version:%s, new version:%s", device_firmware_version, firmware_version->valuestring);
                gcp_ota_update_firmware(firmware_url->valuestring, app_handle->app_config->ota_server_cert_pem);
            }
        }
    }
}

static void gcp_app_config_callback(gcp_client_handle_t client, gcp_client_config_handle_t config, void *user_context)
{
    ESP_LOGD(TAG, "[gcp_app_config_callback] parsing cloud config");
    /* config not set for device pass null to initilize */
    cJSON *gcp_config_json = cJSON_Parse(config);
    if (gcp_config_json == NULL && cJSON_GetErrorPtr() != NULL)
    {
        ESP_LOGE(TAG, "Error: [gcp_app_config_callback] error parsing JSON starting around '%.20s'", cJSON_GetErrorPtr());
    }
    else
    {
        gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;

        cJSON *device_config = cJSON_GetObjectItem(gcp_config_json, JSON_KEY_DEVICE_CONFIG);
        gcp_app_device_config_received(app_client, device_config);

        if (app_client->app_config->config_callback != NULL)
        {
            cJSON *app_config = cJSON_GetObjectItem(gcp_config_json, JSON_KEY_APP_CONFIG);
            app_client->app_config->config_callback(app_client, app_config, app_client->app_config->user_context);
        }
    }
    cJSON_Delete(gcp_config_json);
}

static void gcp_app_command_callback(gcp_client_handle_t client, char *topic, char *command, void *user_context)
{
    gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;
    if (app_client->app_config->cmd_callback != NULL)
    {
        app_client->app_config->cmd_callback(app_client, topic, command, app_client->app_config->user_context);
    }
}

static cJSON *get_app_device_state(gcp_app_handle_t app_client)
{
    cJSON *json_state = cJSON_CreateObject();
    /* device state */
    cJSON *json_device_state = cJSON_CreateObject();
    char version[32];
    gcp_ota_get_running_app_version(version);
    cJSON_AddStringToObject(json_device_state, JSON_KEY_FIRMWARE, version);
    cJSON_AddNumberToObject(json_device_state, JSON_KEY_DEVICE_CONFIG_STATE_PERIOD, app_client->state_update_period_ms);
    cJSON_AddNumberToObject(json_device_state, JSON_KEY_RESET_REASON, esp_reset_reason());

    cJSON_AddItemToObject(json_state, JSON_KEY_DEVICE_STATE, json_device_state);

    /* app state */
    cJSON *app_state = NULL;
    if (app_client->app_config->state_callback != NULL)
    {
        app_state = app_client->app_config->state_callback(app_client, app_client->app_config->user_context);
    }
    cJSON_AddItemToObject(json_state, JSON_KEY_APP_STATE, app_state);
    return json_state;
}

static void gcp_app_send_state(gcp_app_handle_t app_client)
{
    static cJSON *last_state;
    cJSON *new_state = get_app_device_state(app_client);
    if (cJSON_Compare(last_state, new_state, true))
    {
        // no change
        cJSON_Delete(new_state);
        return;
    }
    char *new_state_s = cJSON_Print(new_state);
    gcp_send_state(app_client->gcp_client, new_state_s);
    cJSON_Minify(new_state_s);
    ESP_LOGI(TAG, "[gcp_send_state] sending new state:\n%s", new_state_s);
    free(new_state_s);
    cJSON_Delete(last_state);
    last_state = new_state;
}

static void gcp_send_device_pulse(gcp_app_handle_t app_client)
{
    char *pulse_path_log = app_client->app_config->topic_path_pulse == NULL ? TOPIC_DEFAULT_LOG : app_client->app_config->topic_path_pulse;
    gcp_send_telemetry(app_client->gcp_client, pulse_path_log, "pulse");
}

static void gcp_app_task(void *pvParameter)
{
    ESP_LOGI(TAG, "[gcp_app_task] started");
    gcp_app_handle_t app_client = (gcp_app_handle_t)pvParameter;
    for (;;)
    {
        EventBits_t evt_bit = xEventGroupWaitBits(app_client->app_event_group, GCP_EVENT_STATE_UPDATE_BIT | GCP_EVENT_DEVICE_PULSE_BIT | GCP_EVENT_APP_TASK_END_BIT, true, false, portMAX_DELAY);
        if (evt_bit & GCP_EVENT_APP_TASK_END_BIT)
        {
            break;
        }
        if (evt_bit & GCP_EVENT_STATE_UPDATE_BIT)
        {
            gcp_app_send_state(app_client);
        }
        if (evt_bit & GCP_EVENT_DEVICE_PULSE_BIT)
        {
            gcp_send_device_pulse(app_client);
        }
    }
    ESP_LOGI(TAG, "[gcp_app_task] ended");
    vTaskDelete(NULL);
}

static void gcp_client_connected_callback(gcp_client_handle_t client, void *user_context)
{
    gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;
    start_timers(app_client);
    if (app_client->app_config->connected_callback != NULL)
    {
        app_client->app_config->connected_callback(app_client, app_client->app_config->user_context);
    }
}
static void gcp_client_disconnected_callback(gcp_client_handle_t client, void *user_context)
{
    gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;
    stop_timers(app_client);
    if (app_client->app_config->disconnected_callback != NULL)
    {
        app_client->app_config->disconnected_callback(app_client, app_client->app_config->user_context);
    }
}

static gcp_app_config_t *deep_copy_config(gcp_app_config_t *app_config)
{
    gcp_app_config_t *config_copy = calloc(1, sizeof(*config_copy));
    config_copy->state_callback = app_config->state_callback;
    config_copy->cmd_callback = app_config->cmd_callback;
    config_copy->config_callback = app_config->config_callback;
    config_copy->jwt_callback = app_config->jwt_callback;
    config_copy->connected_callback = app_config->connected_callback;
    config_copy->disconnected_callback = app_config->disconnected_callback;
    config_copy->user_context = app_config->user_context;
    config_copy->device_identifiers = calloc(1, sizeof(gcp_device_identifiers_t));
    config_copy->topic_path_log = app_config->topic_path_log;
    config_copy->topic_path_pulse = app_config->topic_path_pulse;
    config_copy->ota_server_cert_pem = app_config->ota_server_cert_pem;
    memcpy(config_copy->device_identifiers, app_config->device_identifiers, sizeof(gcp_device_identifiers_t));
    return config_copy;
}

esp_err_t gcp_app_destroy(gcp_app_handle_t app)
{
    xEventGroupSetBits(app->app_event_group, GCP_EVENT_APP_TASK_END_BIT);
    gcp_client_destroy(app->gcp_client);
    delete_timers(app);
    free(app->app_config->device_identifiers);
    free(app->app_config);
    app = NULL;
    return ESP_OK;
}

gcp_app_handle_t gcp_app_init(gcp_app_config_t *app_config)
{
    ESP_LOGD(TAG, "[gcp_app_init] started");
    gcp_app_handle_t new_app = calloc(1, sizeof(*new_app));
    new_app->app_config = deep_copy_config(app_config);
    new_app->app_event_group = xEventGroupCreate();
    new_app->state_update_period_ms = CONFIG_STATE_PERIOD_MS_MIN;

    gcp_client_config_t gcp_client_config = {
        .cmd_callback = &gcp_app_command_callback,
        .config_callback = &gcp_app_config_callback,
        .connected_callback = &gcp_client_connected_callback,
        .disconnected_callback = &gcp_client_disconnected_callback,
        .device_identifiers = app_config->device_identifiers,
        .jwt_callback = app_config->jwt_callback,
        .user_context = new_app};

    new_app->gcp_client = gcp_client_init(&gcp_client_config);
    return new_app;
}
esp_err_t gcp_app_start(gcp_app_handle_t client)
{
    ESP_LOGD(TAG, "[gcp_app_start] started");
    esp_err_t err = gcp_client_start(client->gcp_client);
    xTaskCreate(&gcp_app_task, "gcp_app_task", 4096, client, 2, NULL);
    return err;
}

esp_err_t gcp_app_send_telemetry(gcp_app_handle_t client, const char *topic, const char *msg)
{
    return ESP_OK;
}
esp_err_t gcp_app_logf(gcp_app_handle_t client, char *format, ...)
{
    va_list argptr;
    va_start(argptr, format);
    char *message = NULL;
    vasprintf(&message, format, argptr);
    esp_err_t result = gcp_app_log(client, message);
    free(message);
    return result;
}

esp_err_t gcp_app_log(gcp_app_handle_t client, char *message)
{
    char *topic_path_log = client->app_config->topic_path_log == NULL ? TOPIC_DEFAULT_LOG : client->app_config->topic_path_log;
    return gcp_send_telemetry(client->gcp_client, topic_path_log, message);
}
