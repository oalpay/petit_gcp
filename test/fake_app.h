#ifndef _FAKE_APP_H
#define _FAKE_APP_H
#include "gcp_app.h"

void app_connected_callback(gcp_app_handle_t client, void *user_context);
FAKE_VOID_FUNC(app_connected_callback, gcp_app_handle_t , void *);

void app_disconnected_callback(gcp_app_handle_t client, void *user_context);
FAKE_VOID_FUNC(app_disconnected_callback, gcp_app_handle_t , void *);

void app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context);
FAKE_VOID_FUNC(app_config_callback, gcp_app_handle_t , gcp_app_config_handle_t , void *);

void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context);
FAKE_VOID_FUNC(app_command_callback, gcp_app_handle_t , char *, char *, void *);

void app_get_state_callback(gcp_app_handle_t client, gcp_app_state_handle_t state, void *user_context);
FAKE_VOID_FUNC(app_get_state_callback, gcp_app_handle_t , gcp_app_state_handle_t , void *);

char *app_jwt_callback(const char *project_id);
FAKE_VALUE_FUNC( char*, app_jwt_callback, const char *);

#endif