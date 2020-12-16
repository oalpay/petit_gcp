#ifndef GCP_APP__H
#define GCP_APP__H

#ifdef __cplusplus
extern "C"
{
#endif
#include "gcp_client.h"
#include "stdint.h"
#include "stdbool.h"
#include "esp_err.h"
#include "cJSON.h"

    struct gcp_app_client_t;
    typedef struct gcp_app_client_t *gcp_app_handle_t;

    typedef cJSON *gcp_app_config_handle_t;
    typedef cJSON *gcp_app_state_handle_t;

    typedef void (*gcp_app_config_callback_t)(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context);
    typedef void (*gcp_app_command_callback_t)(gcp_app_handle_t client, char *topic, char *cmd, void *user_context);
    typedef void (*gcp_app_state_callback_t)(gcp_app_handle_t client,gcp_app_state_handle_t state, void *user_context);
    typedef void (*gcp_app_connected_callback_t)(gcp_app_handle_t client, void *user_context);
    typedef void (*gcp_app_disconnected_callback_t)(gcp_app_handle_t client, void *user_context);

    #define APP_CONFIG_DEFAULT_STATE_PERIOD_MS 2000
    #define APP_CONFIG_DEFAULT_PULSE_PERIOD_MS 5*60*1000

    typedef struct
    {
        gcp_device_identifiers_t *device_identifiers;
        gcp_jwt_callback_t jwt_callback;
        gcp_app_config_callback_t config_callback;
        gcp_app_command_callback_t cmd_callback;
        gcp_app_state_callback_t state_callback;
        uint32_t state_update_period_ms; /* default is 2 seconds. Assign -1 to turn it off. Lowest GCP allows is 1 second */
        gcp_app_connected_callback_t connected_callback;
        gcp_app_disconnected_callback_t disconnected_callback;
        char *topic_path_log;
        char *topic_path_pulse;
        uint32_t pulse_update_period_ms; /* default is 5 minutes. Assign -1 to turn it off. Lowest GCP allows is 1 second */
        void *user_context;
        const char * ota_server_cert_pem; /* use_global_ca_store is not supported at the moment */
    } gcp_app_config_t;

    gcp_app_handle_t gcp_app_init(gcp_app_config_t *app_config);

    esp_err_t gcp_app_start(gcp_app_handle_t gcp_app);

    esp_err_t gcp_app_send_telemetry(gcp_app_handle_t gcp_app, const char *topic, const char *msg);

    esp_err_t gcp_app_destroy(gcp_app_handle_t gcp_app);

    esp_err_t gcp_app_logf(gcp_app_handle_t client, char *format, ...);

    esp_err_t gcp_app_log(gcp_app_handle_t client, char *message);

#ifdef __cplusplus
}
#endif

#endif