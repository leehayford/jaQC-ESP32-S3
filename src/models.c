#include "models.h"
#include "model_config.h"
#include "model_op_state.h"
#include "util_flash.h"

#include "esp_check.h"

static const char *TAG = "MODELS";

nvs_handle_t s_cfg_nvs = 0;
nvs_handle_t s_ops_nvs = 0;
nvs_handle_t s_opp_nvs = 0;
nvs_handle_t s_comm_nvs = 0;

cfg_t s_cfg;
ops_t s_ops;
// opp_t s_opp;
// comm_t s_comm;

// Initialize non-volatile storage and open model namespaces: 
//  cfg, ops, opp, comm, alert.
esp_err_t models_init(void) {

    // Initialize non-volatile storage
    ESP_RETURN_ON_ERROR(flash_init(), TAG, "failed to initialize flash");

    // Open namespaces
    ESP_RETURN_ON_ERROR(flash_open(NS_CFG, &s_cfg_nvs), TAG, "failed to open cfg namespace");
    ESP_RETURN_ON_ERROR(flash_open(NS_OPS, &s_ops_nvs), TAG, "failed to open ops namespace");
    ESP_RETURN_ON_ERROR(flash_open(NS_OPP, &s_opp_nvs), TAG, "failed to open opp namespace");
    ESP_RETURN_ON_ERROR(flash_open(NS_COMM, &s_comm_nvs), TAG, "failed to open comm namespace");

    // Validate / load default values
    ESP_RETURN_ON_ERROR(cfg_validate(&s_cfg), TAG, "cfg validation failed");
    // ESP_RETURN_ON_ERROR(ops_validate(&s_ops), TAG, "ops validation failed");
    // ESP_RETURN_ON_ERROR(opp_validate(&s_opp), TAG, "opp validation failed");

    return ESP_OK;
}


/* TODO: 
esp_err_t validate_model(char *name) {}
*/

/* TODO: on NET_EVENT_WIFI_STA_GOT_IP
    - s_cfg.wifi_ssid = get_wifi_ssid()
    - s_cfg.wifi_pass = get_wifi_pass()
    - store s_cfg in NVS 
    
    - s_ops.ip_addr = wifi_get_ip_str() 
    - store s_ops in NVS
*/