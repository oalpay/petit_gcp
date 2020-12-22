#ifndef GCP_CLIENT__H
#define GCP_CLIENT__H

#ifdef __cplusplus
extern "C"
{
#endif

#include "stdint.h"
#include "stdbool.h"
#include "esp_err.h"
    struct gcp_client_t;
    typedef struct gcp_client_t *gcp_client_handle_t;

    typedef char *gcp_client_config_handle_t;
    typedef char *gcp_client_state_handle_t;

    /* return a heap allocated string, this method will call free on the returned object */
    typedef char *(*gcp_jwt_callback_t)(const char *project_id);
    typedef void (*gcp_client_config_callback_t)(gcp_client_handle_t client, gcp_client_config_handle_t config, void *user_context);
    typedef void (*gcp_client_command_callback_t)(gcp_client_handle_t client, char *topic, char *cmd, void *user_context);
    typedef void (*gcp_client_connected_callback_t)(gcp_client_handle_t client, void *user_context);
    typedef void (*gcp_client_disconnected_callback_t)(gcp_client_handle_t client, void *user_context);

    typedef struct
    {
        char registery[20];
        char region[20];
        char project_id[20];
        char device_id[20];
    } gcp_device_identifiers_t;

    typedef struct
    {
        gcp_device_identifiers_t *device_identifiers;
        gcp_jwt_callback_t jwt_callback;
        gcp_client_config_callback_t config_callback;
        gcp_client_command_callback_t cmd_callback;
        gcp_client_connected_callback_t connected_callback;
        gcp_client_disconnected_callback_t disconnected_callback;
        void *user_context;
    } gcp_client_config_t;

    gcp_client_handle_t gcp_client_init(gcp_client_config_t *client_config);

    esp_err_t gcp_client_start(gcp_client_handle_t client);

    esp_err_t gcp_send_state(gcp_client_handle_t client, const char *state);

    esp_err_t gcp_send_telemetry(gcp_client_handle_t client, const char *topic, const char *msg);

    esp_err_t gcp_client_destroy(gcp_client_handle_t client);

#ifdef __cplusplus
}
#endif

#endif