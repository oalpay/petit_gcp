#include "gcp_app.h"
#include "gcp_app_internal.h"
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
#define JSON_KEY_DEVICE_CONFIG_TIMEZONE "tz"
#define JSON_KEY_DEVICE_CONFIG_STATE_PERIOD "state_period_ms"
#define JSON_KEY_DEVICE_CONFIG_PULSE_PERIOD "pulse_period_ms"
#define JSON_KEY_DEVICE_FIRMWARE "firmware"
#define JSON_KEY_DEVICE_FIRMWARE_VERSION "version"
#define JSON_KEY_DEVICE_FIRMWARE_URL "url"
#define JSON_KEY_APP_CONFIG "app_config"
#define JSON_KEY_DEVICE_STATE "device_state"
#define JSON_KEY_APP_STATE "app_state"
#define JSON_KEY_FIRMWARE "firmware"
#define JSON_KEY_RSSI "rssi"
#define JSON_KEY_RESET_REASON "reset_reason"

#define TIMER_WAIT (500 / portTICK_PERIOD_MS)

static void timer_callback(TimerHandle_t timer)
{
    ESP_LOGD(TAG, "[timer_callback] %s", pcTimerGetTimerName(timer));
    configASSERT(timer);
    gcp_app_handle_t app_handle = (gcp_app_handle_t)pvTimerGetTimerID(timer);
    if (timer == app_handle->state_update_timer)
    {
        xEventGroupSetBits(app_handle->app_event_group, GCP_EVENT_STATE_UPDATE_BIT);
    }
    else if (timer == app_handle->device_pulse_timer)
    {
        xEventGroupSetBits(app_handle->app_event_group, GCP_EVENT_DEVICE_PULSE_BIT);
    }
    else
    {
        ESP_LOGE(TAG, "[timer_callback] unrecognized timer");
    }
}

static bool stop_timer(xTimerHandle timer_handle)
{
    ESP_LOGD(TAG, "[stop_timer] %s", pcTimerGetTimerName(timer_handle));
    if (xTimerStop(timer_handle, TIMER_WAIT) != pdPASS)
    {
        ESP_LOGE(TAG, "[stop_timer] failed");
        return false;
    }
    return true;
}

static bool change_timer_period(xTimerHandle timer_handle, uint32_t period_ms)
{
    ESP_LOGD(TAG, "[change_timer_period] timer:%s, period:%d", pcTimerGetTimerName(timer_handle), period_ms);
    if (xTimerChangePeriod(timer_handle, period_ms / portTICK_PERIOD_MS, TIMER_WAIT) == pdFAIL)
    {
        ESP_LOGE(TAG, "[change_timer_period] failed");
        return false;
    }
    return true;
}

static void delete_timer_from_config(xTimerHandle *config_timer_handle)
{
    ESP_LOGD(TAG, "[delete_timer_from_config] %s", pcTimerGetTimerName(*config_timer_handle));
    if (xTimerDelete(*config_timer_handle, TIMER_WAIT) != pdPASS)
    {
        ESP_LOGE(TAG, "[delete_timer_from_config] failed");
    }
    else
    {
        *config_timer_handle = NULL;
    }
}

static void create_timer_in_config(gcp_app_handle_t app, xTimerHandle *config_timer_handle, char *name, uint32_t period_ms)
{
    ESP_LOGD(TAG, "[create_timer_in_config] %s", name);
    *config_timer_handle = xTimerCreate(
        name,
        period_ms / portTICK_PERIOD_MS,
        pdTRUE,
        app,
        timer_callback);
}

static void timer_config_received(xTimerHandle timer, uint32_t *config_period, uint32_t new_period)
{
    if (*config_period == new_period)
    {
        return;
    }
    ESP_LOGD(TAG, "[timer_config_received] %s timer period changing to:%d", pcTimerGetTimerName(timer), new_period);
    if (new_period == -1)
    {
        if (!stop_timer(timer))
        {
            return;
        }
    }
    else
    {
        if (!change_timer_period(timer, new_period))
        {
            return;
        }
    }
    ESP_LOGI(TAG, "[timer_config_received] %s timer period changed to:%d", pcTimerGetTimerName(timer), new_period);
    *config_period = new_period;
}

static void gcp_app_device_config_received(gcp_app_handle_t app_handle, cJSON *device_config)
{
    const cJSON *timezone = cJSON_GetObjectItem(device_config, JSON_KEY_DEVICE_CONFIG_TIMEZONE);
    if (cJSON_IsString(timezone))
    {
        setenv("TZ", timezone->valuestring, 1);
        tzset();
    }
    const cJSON *state_period_ms = cJSON_GetObjectItem(device_config, JSON_KEY_DEVICE_CONFIG_STATE_PERIOD);
    if (cJSON_IsNumber(state_period_ms))
    {
        timer_config_received(app_handle->state_update_timer, &app_handle->app_config->state_update_period_ms, state_period_ms->valueint);
    }
    const cJSON *pulse_period_ms = cJSON_GetObjectItem(device_config, JSON_KEY_DEVICE_CONFIG_PULSE_PERIOD);
    if (cJSON_IsNumber(pulse_period_ms))
    {
        timer_config_received(app_handle->device_pulse_timer, &app_handle->app_config->pulse_update_period_ms, pulse_period_ms->valueint);
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

void gcp_app_config_callback(gcp_client_handle_t client, gcp_client_config_handle_t config, void *user_context)
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

        /* call application callback */
        if (app_client->app_config->config_callback != NULL)
        {
            cJSON *app_config = cJSON_GetObjectItem(gcp_config_json, JSON_KEY_APP_CONFIG);
            app_client->app_config->config_callback(app_client, app_config, app_client->app_config->user_context);
        }
    }
    cJSON_Delete(gcp_config_json);
}

void gcp_app_command_callback(gcp_client_handle_t client, char *topic, char *command, void *user_context)
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
    cJSON_AddNumberToObject(json_device_state, JSON_KEY_DEVICE_CONFIG_STATE_PERIOD, app_client->app_config->state_update_period_ms);
    cJSON_AddNumberToObject(json_device_state, JSON_KEY_DEVICE_CONFIG_PULSE_PERIOD, app_client->app_config->pulse_update_period_ms);
    cJSON_AddNumberToObject(json_device_state, JSON_KEY_RESET_REASON, esp_reset_reason());

    cJSON_AddItemToObject(json_state, JSON_KEY_DEVICE_STATE, json_device_state);

    /* app state */
    cJSON *app_state = cJSON_CreateObject();
    if (app_client->app_config->state_callback != NULL)
    {
        app_client->app_config->state_callback(app_client, app_state, app_client->app_config->user_context);
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
        goto end;
    }
    char *new_state_s = cJSON_Print(new_state);
    cJSON_Minify(new_state_s);
    ESP_LOGI(TAG, "[gcp_send_state] sending new state:\n%s", new_state_s);
    gcp_send_state(app_client->gcp_client, new_state_s);
    free(new_state_s);
end:
    cJSON_Delete(last_state);
    last_state = new_state;
}

static void gcp_send_device_pulse(gcp_app_handle_t app_client)
{
    char *pulse_path_log = app_client->app_config->topic_path_pulse == NULL ? TOPIC_DEFAULT_PULSE : app_client->app_config->topic_path_pulse;
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

void gcp_app_connected_callback(gcp_client_handle_t client, void *user_context)
{
    ESP_LOGD(TAG, "[gcp_client_connected_callback]");
    gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;
    gcp_app_config_t *app_config = app_client->app_config;
    if (app_config->state_update_period_ms > 0)
    {
        change_timer_period(app_client->state_update_timer, app_config->state_update_period_ms);
    }
    if (app_config->pulse_update_period_ms > 0)
    {
        change_timer_period(app_client->device_pulse_timer, app_config->pulse_update_period_ms);
    }
    if (app_config->connected_callback != NULL)
    {
        app_config->connected_callback(app_client, app_config->user_context);
    }
}

void gcp_app_disconnected_callback(gcp_client_handle_t client, void *user_context)
{
    ESP_LOGD(TAG, "[gcp_app_disconnected_callback]");
    gcp_app_handle_t app_client = (gcp_app_handle_t)user_context;
    stop_timer(app_client->state_update_timer);
    stop_timer(app_client->device_pulse_timer);

    if (app_client->app_config->disconnected_callback != NULL)
    {
        app_client->app_config->disconnected_callback(app_client, app_client->app_config->user_context);
    }
}

static gcp_app_config_t *deep_copy_config(gcp_app_config_t *app_config)
{
    gcp_app_config_t *config_copy = calloc(1, sizeof(*config_copy));
    memcpy(config_copy, app_config, sizeof(gcp_app_config_t));
    config_copy->device_identifiers = calloc(1, sizeof(gcp_device_identifiers_t));
    memcpy(config_copy->device_identifiers, app_config->device_identifiers, sizeof(gcp_device_identifiers_t));
    return config_copy;
}

esp_err_t gcp_app_destroy(gcp_app_handle_t app)
{
    xEventGroupSetBits(app->app_event_group, GCP_EVENT_APP_TASK_END_BIT);
    gcp_client_destroy(app->gcp_client);
    delete_timer_from_config(&app->state_update_timer);
    delete_timer_from_config(&app->device_pulse_timer);
    free(app->app_config->device_identifiers);
    free(app->app_config);
    free(app);
    app = NULL;
    return ESP_OK;
}

static void init_timers(gcp_app_handle_t app)
{
    /* state */
    if (app->app_config->state_update_period_ms == 0)
    {
        app->app_config->state_update_period_ms = APP_CONFIG_DEFAULT_STATE_PERIOD_MS;
    }
    create_timer_in_config(app, &app->state_update_timer, "state_update_timer", app->app_config->state_update_period_ms);

    /* pulse */
    if (app->app_config->pulse_update_period_ms == 0)
    {
        app->app_config->pulse_update_period_ms = APP_CONFIG_DEFAULT_PULSE_PERIOD_MS;
    }
    create_timer_in_config(app, &app->device_pulse_timer, "device_pulse_timer", app->app_config->pulse_update_period_ms);
}

gcp_app_handle_t gcp_app_init(gcp_app_config_t *app_config)
{
    ESP_LOGD(TAG, "[gcp_app_init] started");
    gcp_app_handle_t new_app = calloc(1, sizeof(*new_app));
    new_app->app_config = deep_copy_config(app_config);
    new_app->app_event_group = xEventGroupCreate();
    init_timers(new_app);

    gcp_client_config_t gcp_client_config = {
        .cmd_callback = &gcp_app_command_callback,
        .config_callback = &gcp_app_config_callback,
        .connected_callback = &gcp_app_connected_callback,
        .disconnected_callback = &gcp_app_disconnected_callback,
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
    if (err == ESP_OK)
    {
        xTaskCreate(&gcp_app_task, "gcp_app_task", 4096, client, 2, NULL);
    }
    return err;
}

esp_err_t gcp_app_send_telemetry(gcp_app_handle_t client, const char *topic, const char *msg)
{
    return gcp_send_telemetry(client->gcp_client, topic, msg);
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
