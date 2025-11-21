#ifndef MODELS_H
#define MODELS_H

#include "model_config.h"
#include "model_op_state.h"
#include "nvs_flash.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NS_CFG "cfg"
#define NS_OPP "opp"
#define NS_OPS "ops"
#define NS_COMM "comm"

// Declare NVS handles for each model
extern nvs_handle_t s_cfg_nvs;
extern nvs_handle_t s_opp_nvs;
extern nvs_handle_t s_ops_nvs;
extern nvs_handle_t s_comm_nvs;

extern cfg_t s_cfg;
extern ops_t s_ops;

// Initialization function
esp_err_t models_init(void);


#ifdef __cplusplus
}
#endif

#endif // MODELS_H
