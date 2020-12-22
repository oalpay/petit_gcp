#include "gcp_client.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include <mbedtls/error.h>
#include <mqtt_client.h>
#include <string.h>
#include "cJSON.h"

#define TAG "GCP_CLIENT"

#define DEVICE_TELEMETRY_TOPIC_FORMAT "/devices/%s/events/%s"
#define DEVICE_STATE_TOPIC_FORMAT "/devices/%s/state"
#define DEVICE_COMMAND_TOPIC_FORMAT "/devices/%s/commands/#"
#define DEVICE_CONFIG_TOPIC_FORMAT "/devices/%s/config"
#define MQTT_BRIDGE_URI "mqtts://mqtt.googleapis.com:8883"
#define MQTT_CLIENT_ID_FORMAT "projects/%s/locations/%s/registries/%s/devices/%s"

#define GCP_MQTT_RETRY_PERIOD_MS 60000
#define GCP_EVENT_STATE_UPDATE_BIT BIT1
#define GCP_EVENT_MQTT_CONNECTED_BIT BIT3
#define GCP_EVENT_MQTT_DISCONNECT_BIT BIT4

struct gcp_client_t
{
    gcp_client_config_t *client_config;
    esp_mqtt_client_handle_t mqtt_client;
    char *topic_config;
    char *topic_cmd;
    char *topic_state;
};

static esp_err_t mqtt_evet_connected(esp_mqtt_event_handle_t event)
{
    ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
    gcp_client_handle_t gcp_client = event->user_context;
    esp_mqtt_client_subscribe(event->client, gcp_client->topic_config, 1);
    esp_mqtt_client_subscribe(event->client, gcp_client->topic_cmd, 1);
    if (gcp_client->client_config->connected_callback != NULL)
    {
        gcp_client->client_config->connected_callback(gcp_client, gcp_client->client_config->user_context);
    }
    return ESP_OK;
}

static esp_err_t mqtt_evet_disconnected(esp_mqtt_event_handle_t event)
{
    ESP_LOGD(TAG, "MQTT_EVENT_DISCONNECTED");
    gcp_client_handle_t gcp_client = event->user_context;
    if (gcp_client->client_config->disconnected_callback != NULL)
    {
        gcp_client->client_config->disconnected_callback(gcp_client, gcp_client->client_config->user_context);
    }
    return ESP_OK;
}

static esp_err_t mqtt_data_received(esp_mqtt_event_handle_t event)
{
    ESP_LOGD(TAG, "MQTT_EVENT_DATA: MSG_ID=%d, TOPIC=%.*s, DATA=%.*s", event->msg_id, event->topic_len, event->topic, event->data_len, event->data);
    char *data_buffer = (char *)calloc(event->data_len + 1, sizeof(char));
    sprintf(data_buffer, "%.*s", event->data_len, event->data);
    gcp_client_handle_t gcp_client = event->user_context;
    if (strcmp(event->topic, gcp_client->topic_config) == 0)
    {
        if (gcp_client->client_config->config_callback != NULL)
        {
            gcp_client->client_config->config_callback(gcp_client, data_buffer, gcp_client->client_config->user_context);
        }
    }
    else
    {
        if (gcp_client->client_config->cmd_callback != NULL)
        {
            gcp_client->client_config->cmd_callback(gcp_client, event->topic, data_buffer, gcp_client->client_config->user_context);
        }
    }
    free(data_buffer);
    return ESP_OK;
}

static esp_err_t gcp_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_err_t result = ESP_OK;
    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;
    case MQTT_EVENT_CONNECTED:
        result = mqtt_evet_connected(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        result = mqtt_evet_disconnected(event);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED: topic=%.*s", event->topic_len, event->topic);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED: topic=%.*s", event->topic_len, event->topic);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED: topic=%.*s", event->topic_len, event->topic);
        break;
    case MQTT_EVENT_DATA:
        result = mqtt_data_received(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_ANY:
        ESP_LOGE(TAG, "MQTT_EVENT_ANY");
        break;
    }
    return result;
}

static void gcp_mqtt_connect(gcp_client_handle_t gcp_client)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = MQTT_BRIDGE_URI;
    mqtt_cfg.event_handle = gcp_mqtt_event_handler;
    //mqtt_cfg.cert_pem = (const char *)iot_google_pem_key_start;
    mqtt_cfg.username = "unuser";
    char *jwt = gcp_client->client_config->jwt_callback(gcp_client->client_config->device_identifiers->project_id);
    mqtt_cfg.password = jwt;
    gcp_device_identifiers_t *device_identifiers = gcp_client->client_config->device_identifiers;
    char client_id[200];
    sprintf(client_id, MQTT_CLIENT_ID_FORMAT, device_identifiers->project_id, device_identifiers->region, device_identifiers->registery, device_identifiers->device_id);
    ESP_LOGD(TAG, "[gcp_mqtt_connect] client_id:%s", client_id);
    mqtt_cfg.client_id = client_id;
    mqtt_cfg.user_context = gcp_client;
    gcp_client->mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_start(gcp_client->mqtt_client));
    //free
    free(jwt);
}

static gcp_client_config_t *deep_copy_config(gcp_client_config_t *client_config)
{
    gcp_client_config_t *config_copy = calloc(1, sizeof(gcp_client_config_t));
    config_copy->cmd_callback = client_config->cmd_callback;
    config_copy->config_callback = client_config->config_callback;
    config_copy->jwt_callback = client_config->jwt_callback;
    config_copy->connected_callback = client_config->connected_callback;
    config_copy->disconnected_callback = client_config->disconnected_callback;
    config_copy->device_identifiers = calloc(1, sizeof(gcp_device_identifiers_t));
    config_copy->user_context = client_config->user_context;
    memcpy(config_copy->device_identifiers, client_config->device_identifiers, sizeof(gcp_device_identifiers_t));
    return config_copy;
}

static void setup_topic_strings(gcp_client_handle_t client)
{
    asprintf(&client->topic_config, DEVICE_CONFIG_TOPIC_FORMAT, client->client_config->device_identifiers->device_id);
    asprintf(&client->topic_cmd, DEVICE_COMMAND_TOPIC_FORMAT, client->client_config->device_identifiers->device_id);
    asprintf(&client->topic_state, DEVICE_STATE_TOPIC_FORMAT, client->client_config->device_identifiers->device_id);
}

esp_err_t gcp_client_destroy(gcp_client_handle_t client)
{
    esp_mqtt_client_destroy(client->mqtt_client);
    free(client->client_config->device_identifiers);
    free(client->client_config);
    free(client);
    client = NULL;
    return ESP_OK;
}

gcp_client_handle_t gcp_client_init(gcp_client_config_t *client_config)
{
    gcp_client_handle_t new_client = calloc(1, sizeof(*new_client));
    new_client->client_config = deep_copy_config(client_config);
    setup_topic_strings(new_client);
    return new_client;
}

esp_err_t gcp_client_start(gcp_client_handle_t client)
{
    ESP_LOGD(TAG, "[gcp_client_start] starting gcp client for device %s", client->client_config->device_identifiers->device_id);
    assert(client != NULL);
    gcp_mqtt_connect(client);
    return ESP_OK;
}

esp_err_t gcp_send_state(gcp_client_handle_t client, const char *state)
{
    esp_err_t result = esp_mqtt_client_publish(client->mqtt_client, client->topic_state, state, 0, 1, 1);
    return result > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t gcp_send_telemetry(gcp_client_handle_t client, const char *topic, const char *msg)
{
    char *device_topic;
    asprintf(&device_topic, DEVICE_TELEMETRY_TOPIC_FORMAT, client->client_config->device_identifiers->device_id, topic);
    ESP_LOGI(TAG, "[gcp_send_telemetry] topic:%s, msg:%s", device_topic, msg);
    esp_err_t result = esp_mqtt_client_publish(client->mqtt_client, device_topic, msg, 0, 1, 1);
    free(device_topic);
    return result > 0 ? ESP_OK : ESP_FAIL;
}