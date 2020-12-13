#ifndef __GCP_OTA_H__
#define __GCP_OTA_H__

void gcp_ota_get_running_app_version(char *version);
void gcp_ota_update_firmware(char *firmware_url);

#endif