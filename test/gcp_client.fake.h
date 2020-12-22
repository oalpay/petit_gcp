#ifndef _FAKE_GCP_CLIENT_H
#define _FAKE_GCP_CLIENT_H

#include "fff.h"
#include "gcp_client.h"

FAKE_VALUE_FUNC(gcp_client_handle_t, gcp_client_init, gcp_client_config_t *);
FAKE_VALUE_FUNC(esp_err_t, gcp_client_start, gcp_client_handle_t);
FAKE_VALUE_FUNC(esp_err_t, gcp_send_state, gcp_client_handle_t, const char *);
FAKE_VALUE_FUNC(esp_err_t, gcp_send_telemetry, gcp_client_handle_t,  const char *, const char *);
FAKE_VALUE_FUNC(esp_err_t, gcp_client_destroy, gcp_client_handle_t);

#endif