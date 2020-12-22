# petit_gcp

Micro framework for writing applications on ESP32 devices connected to Google Cloud Platform with MQTT protocol.

## Features

 - Inverstion of control, just pass your callbacks for device configuration, state and commands
 - cJSON config and state objects
 - Periodic state updates 
 - OTA updates with versioning
 - Cloud logging

## Installing

### Using Platform IO
```bash
pio lib install "oalpay/petit-gcp"
```
### Other
 Copy *include/\*.h* and *src/\*.c* files to your project. 

## Getting Started
```c

static void app_config_callback(gcp_app_handle_t client, gcp_app_config_handle_t config, void *user_context)
{
    char *pretty_config = cJSON_Print(config);
    ESP_LOGI(TAG, "[app_config_callback] app_config:%s \n", pretty_config);
    free(pretty_config);
    gcp_app_logf(client, "love is %s", command);
}

static void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context)
{
    ESP_LOGI(TAG, "[app_command_callback] topic:%s, cmd:%s", topic, command);
    gcp_app_send_telemetry(client, "topic_love", "1");
}

/* add your applications state and forget about it. This callback will be called periodically with an empty state object. If there is a change from the previos call your new state will be sent to GCP IoT state topic */
static void app_get_state_callback(gcp_app_handle_t client, gcp_app_state_handle_t state, void *user_context)
{
    ESP_LOGI(TAG, "[app_get_state_callback]");
    cJSON_AddStringToObject(state, "desire", "objet petit");
    cJSON_AddNumberToObject(state, "GPIO_18", gpio_get_level(18));
}

/* set your JWT TOKEN */
static void jwt_callback(const char *project_id, char* jtw_token_buffer)
{
    char *token = create_GCP_JWT(project_id, (const char *)gcp_jwt_private_pem_key_start, gcp_jwt_private_pem_key_end - gcp_jwt_private_pem_key_start);
    strncpy(jtw_token_buffer,token, JWT_TOKEN_BUFFER_SIZE); //BUFFER SIZE
    free(token);
}

void app_main() {
   /* initializers 
    ....
   */

   gcp_device_identifiers_t default_gcp_device_identifiers = {
        .registery = REGISTERY,
        .region = REGION,
        .project_id = PROJECT_ID,
        .device_id = DEVICE_ID};

   gcp_app_config_t gcp_app_config = { 
        .cmd_callback = &app_command_callback, /* wildcard command topic messages will be delivered to this callback from MQTT_TASK thread */
        .config_callback = &app_config_callback, /* called from MQTT_TASK thread when config is received from GCP */
        .state_callback = &app_get_state_callback, /* called from GCP_APP thread and new state will be sent on if there is change from the previous state */
        .device_identifiers = &default_gcp_device_identifiers,
        .jwt_callback = &jwt_callback};

   gcp_app_handle_t petit_app = gcp_app_init(&gcp_app_config);
   gcp_app_start(petit_app);
}  
```

## Google Cloud IoT Device Config Format
```json
{
   "app_config":{ 
      "test":"app_config"
   },
   "device_config":{
      "state_period_ms":5000,
      "pulse_period_ms":60000,
      "tz": "GMT-3"
   }
}
```

### Config objects
- **app_config**: this object will be passed to your gcp_app_config_t.config_callback
- **device_config**: this object is reserved for petit_gcp framework   
  - **state_period_ms**: how often state updates will be checked and sent if there is a change
  - **pulse_period_ms**: how often hearth pulse signals will be sent
  - **tz**: set timezone of the device 

## Google Cloud IoT Device State 
Example function **app_get_state_callback** above will generate and send this state object to GCP

```json
{
   "app_state":{
      "desire":"objet petit",
      "GPIO_18":1
   },
   "device_state":{
      "firmware":"0_18",
      "state_period_ms":5000,
      "reset_reason":1
   }
}
```
## Cloud OTA Updates
```json
{
   "app_config":{ 
      "test":"app_config"
   },
  "device_config":{
      "firmware":{
         "url":"https://your_elegant_application.bin",
         "version":"0_18" 
      },
      "state_period_ms":5000
   }
}
```

### Cloud OTA params

- **version**:  your applications version will be read from [esp_app_desc_t](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#app-version) and if it is different from the configuration then the firmware pointed at the url will be burned to your device
- **url**: firmware url (make sure pass the server certificates to gcp_app_config_t.ota_server_cert_pem)
  
## Cloud Logging

Logs will be sent as telemetry messages, you can change the default logging topic by passing a relative path in **gcp_app_config_t.topic_path_log** e.g. **topic_path_log="my_log_path/is_better"**
```c
static void app_command_callback(gcp_app_handle_t client, char *topic, char *command, void *user_context)
{
    gcp_app_logf(client, "love is %s", command);
    gcp_app_log(client, "Love is giving something you don't have to someone who doesn't want it.");
}

```

## Pulse

Framework will sent periodic telemetry messages to **pulse** topic you can change the default pulse topic in **gcp_app_config_t.topic_path_pule** e.g. **topic_path_pule="my_pulse/is_better"**