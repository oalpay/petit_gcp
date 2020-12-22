#ifndef GCP_APP_INTERNAL__H
#define GCP_APP_INTERNAL__H

#include "gcp_app.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "freertos/event_groups.h"

#define GCP_EVENT_DEVICE_PULSE_BIT BIT2
#define GCP_EVENT_STATE_UPDATE_BIT BIT1
#define GCP_EVENT_APP_TASK_END_BIT BIT3

struct gcp_app_client_t
{
    gcp_client_handle_t gcp_client;
    gcp_app_config_t *app_config;
    xTimerHandle state_update_timer;
    xTimerHandle device_pulse_timer;
    EventGroupHandle_t app_event_group;
};

void gcp_app_connected_callback(gcp_client_handle_t client, void *user_context);
void gcp_app_config_callback(gcp_client_handle_t client, gcp_client_config_handle_t config, void *user_context);
void gcp_app_command_callback(gcp_client_handle_t client, char *topic, char *command, void *user_context);
void gcp_app_disconnected_callback(gcp_client_handle_t client, void *user_context);

#endif