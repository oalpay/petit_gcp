#ifndef WIFI_HELPER__H
#define WIFI_HELPER__H

#include <stdlib.h>

typedef struct{
    char ssid[20];
    char passphrase[20];
} wifi_credentials_t;

void wifi_helper_start(wifi_credentials_t *wifi_credentials);
void wifi_helper_set_global_ca_store(const unsigned char *pem_key, size_t pem_key_size);
void wifi_wait_connection();

#endif