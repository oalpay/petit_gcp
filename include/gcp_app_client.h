#ifndef GCP_APP_CLIENT__H
#define GCP_APP_CLIENT__H

#include "gcp_app.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"

#define CONFIG_STATE_PERIOD_MS_MIN 2000
#define CONFIG_DEVICE_PULSE_PERIOD_MS 60 * 1000 * 5
#define GCP_EVENT_DEVICE_PULSE_BIT BIT2
#define GCP_EVENT_STATE_UPDATE_BIT BIT1
#define GCP_EVENT_APP_TASK_END_BIT BIT3

struct gcp_app_client_t
{
    gcp_client_handle_t gcp_client;
    gcp_app_config_t *app_config;
    xTimerHandle state_update_timer;
    uint32_t state_update_period_ms;
    xTimerHandle device_pulse_timer;
    EventGroupHandle_t app_event_group;
};

#endif