# petit_gco

Micro framework for writing applications on ESP32 devices connected to Google Cloud Platform.

## Features

 - Inverstion of control just pass your callbacks for device configuration and commands
 - cJSON configuraion and state objects
 - Automatic state updates 
 - Cloud logging
 - OTA updates

## Getting Started
```c

static void app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context)
{
    char *pretty_config = cJSON_Print(config);
    ESP_LOGI(TAG, "[app_config_callback] app_config:%s \n", pretty_config);
    free(pretty_config);
}

static void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context)
{
    ESP_LOGI(TAG, "[app_command_callback] topic:%s, cmd:%s", topic, command);
}

static void app_get_state_callback(gcp_app_handle_t client, gcp_app_state_handle_t state, void *user_context)
{
    ESP_LOGI(TAG, "[app_get_state_callback]");
    cJSON_AddStringToObject(state, "desire", "objet petit");
}

gcp_device_identifiers_t default_gcp_device_identifiers = {
        .registery = REGISTERY,
        .region = REGION,
        .project_id = PROJECT_ID,
        .device_id = DEVICE_ID};

gcp_app_config_t gcp_app_config = {
        .cmd_callback = &app_command_callback,
        .config_callback = &app_config_callback,
        .state_callback = &app_get_state_callback, /* will be called periodically and new state will be sent on if there is change */
        .device_identifiers = &default_gcp_device_identifiers,
        .jwt_callback = &jwt_callback};

gcp_app_handle_t petit_app = gcp_app_init(&gcp_app_config);
gcp_app_start(petit_app);
```

## Google Cloud IoT Device Config Format

```json
{
   "app_config":{ 
      "test":"app_config"
   },
   "device_config":{
      "state_period_ms":5000 /* change state update check period */
   }
}
```
### Config objects
- **app_config**: this object will be passed to your gcp_app_config_t.config_callback
- **device_config**: this object is reserved for petit_gcp framework  

## OTA
```json
{
   "app_config":{ 
      "test":"app_config"
   },
  "device_config":{
      "firmware":{
         "url":"https://storage.googleapis.com/your_elegant_application_binary",
         "version":"0_18" 
      },
      "state_period_ms":5000
   }
}
```

### OTA params

- **version**:  your applications version will be read from [esp_app_desc_t](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#app-version) and if it is different from the configuration then the firmware pointed at the url will be burned to your device
- **url**: firmware url