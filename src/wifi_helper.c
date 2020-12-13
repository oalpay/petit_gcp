#include "wifi_helper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/apps/sntp.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_event.h"

#include <string.h>

#define TAG "WIFI_HELPER"

#define SNTP_INIT_BIT BIT1
#define GOT_IP_BIT BIT0

EventGroupHandle_t get_wifi_event_group()
{
    static EventGroupHandle_t g_wifi_event_group;
    if (g_wifi_event_group == NULL)
    {
        g_wifi_event_group = xEventGroupCreate();
    }
    return g_wifi_event_group;
}
void wifi_wait_connection()
{
    xEventGroupWaitBits(get_wifi_event_group(), SNTP_INIT_BIT, false, true, portMAX_DELAY);
}

void initialize_time(void)
{
    if (sntp_enabled())
    {
        ESP_LOGI(TAG, "[initialize_time] restarting SNTP");
        sntp_restart();
        return;
    }
    ESP_LOGI(TAG, "[initialize_time] initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time1.google.com");
    sntp_setservername(1, "pool.ntp.org");
    sntp_init();

    time_t now;
    uint16_t retry_count = 0;
    do
    {
        ESP_LOGD(TAG, "Waiting for sntp retry_count: %d", retry_count);
        vTaskDelay(1000 / portTICK_RATE_MS);
        time(&now);
        retry_count += 1;
        if (retry_count == 100)
        {
            ESP_LOGE(TAG, "SNTP max retry reached");
            esp_restart();
        }
    } while (now < 946685089 /* 1.1.2000 */);
    xEventGroupSetBits(get_wifi_event_group(), SNTP_INIT_BIT);
    ESP_LOGI(TAG, "The current epoch time is: %d", (uint32_t)now);
    setenv("TZ", "CST6CDT", 1);
    tzset();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        xEventGroupClearBits(get_wifi_event_group(), GOT_IP_BIT);
        esp_wifi_connect();
        break;
    default:
        break;
    }
}
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(get_wifi_event_group(), GOT_IP_BIT);
    initialize_time();
}
static void lost_ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "lost ip");
    xEventGroupClearBits(get_wifi_event_group(), GOT_IP_BIT);
}

void wifi_helper_set_global_ca_store(const unsigned char *pem_key, size_t pem_key_size)
{
    ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
    ESP_ERROR_CHECK(esp_tls_set_global_ca_store(pem_key, pem_key_size));
}

void wifi_helper_start(wifi_credentials_t *wifi_credentials)
{
    ESP_LOGI(TAG, "[wifi_helper_start] ssid:%s, pass:%s", wifi_credentials->ssid, wifi_credentials->passphrase);
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &lost_ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t sta_config = {};
    strcpy((char *)sta_config.sta.ssid, wifi_credentials->ssid);
    strcpy((char *)sta_config.sta.password, wifi_credentials->passphrase);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_start finished.");
}
