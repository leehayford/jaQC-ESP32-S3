#ifndef UTIL_FLASH_H
#define UTIL_FLASH_H

#include "util_err.h"

#include "esp_err.h"
#include "nvs_flash.h" 

#include <stdbool.h>

#define FLASH_PARTITION "nvs"

#ifdef __cplusplus
extern "C" {
#endif

// static const char *H_TAG = "UTIL_FLASH_H";

esp_err_t flash_init(void);
esp_err_t flash_open(char*, nvs_handle_t*);
esp_err_t flash_commit(nvs_handle_t);
esp_err_t flash_list_namespase_keys(const char*);

// Type-specific getters
esp_err_t flash_get_bool    (nvs_handle_t,   const char*,    bool*,       size_t);
esp_err_t flash_get_int     (nvs_handle_t,   const char*,    int*,        size_t);
esp_err_t flash_get_i8      (nvs_handle_t,   const char*,    int8_t*,     size_t);
esp_err_t flash_get_i32     (nvs_handle_t,   const char*,    int32_t*,    size_t);
esp_err_t flash_get_u8      (nvs_handle_t,   const char*,    uint8_t*,    size_t);
esp_err_t flash_get_u32     (nvs_handle_t,   const char*,    uint32_t*,   size_t);
esp_err_t flash_get_float   (nvs_handle_t,   const char*,    float*,      size_t);
esp_err_t flash_get_str     (nvs_handle_t,   const char*,    char*,       size_t);
esp_err_t flash_get_blob    (nvs_handle_t,   const char*,    void*,       size_t);

// Generic getter macro dispatcher using _Generic
#define FLASH_GET(handle, key, dest) _Generic(((dest)+0), \
    bool *:             flash_get_bool,     \
    int *:              flash_get_int,      \
    int8_t *:           flash_get_i8,       \
    int32_t *:          flash_get_i32,      \
    uint8_t *:          flash_get_u8,       \
    uint32_t *:         flash_get_u32,      \
    float *:            flash_get_float,    \
    char *:             flash_get_str,      \
    const char *:       flash_get_str       \
)((handle), (key), (dest), sizeof((dest)))
/* wifi_ui_state_t *:  flash_get_i32       \*/


#define FLASH_GET_BLOB(handle, key, buf) \
    flash_get_blob((handle), (key), (buf), sizeof(buf))

// Macro to check and return error
// You need to pass the address of scalar fields so _Generic can match the pointer type.
//
//  &out->scalar_field
//      bool        --> FLASH_CHECK(  "some_bool",    &out->some_bool     );
//      int         --> FLASH_CHECK(  "some_int",     &out->some_int      ); 
//      int32_t     --> FLASH_CHECK(  "some_i32",     &out->some_i32      );
//      float       --> FLASH_CHECK(  "some_float",   &out->some_float    ); 
//      
//  out->non_scalar_field
//      char[x]     --> FLASH_CHECK(  "some_char",    out->some_char      );
//    
#define FLASH_CHECK(handle, key, dest) do { \
    esp_err_t err = FLASH_GET((handle), (key), (dest)); \
    if (err != ESP_OK) { \
        LOG_ERR("UTIL_FLASH_H", (err), "failed to get key '%s': ", (key)); \
        return err; \
    } \
} while (0)

// Type-specific setters
esp_err_t flash_set_bool    (nvs_handle_t,  const char*,    bool);
esp_err_t flash_set_int     (nvs_handle_t,  const char*,    int);
esp_err_t flash_set_i8      (nvs_handle_t,  const char*,    int8_t);
esp_err_t flash_set_i32     (nvs_handle_t,  const char*,    int32_t);
esp_err_t flash_set_u8      (nvs_handle_t,  const char*,    uint8_t);
esp_err_t flash_set_u32     (nvs_handle_t,  const char*,    uint32_t);
esp_err_t flash_set_float   (nvs_handle_t,  const char*,    float);
esp_err_t flash_set_str     (nvs_handle_t,  const char*,    const char*);

// Generic setter macro dispatcher using _Generic
#define FLASH_SET(handle, key, value) _Generic((value), \
    bool:               flash_set_bool,     \
    int :               flash_set_int,      \
    int8_t *:           flash_set_i8,       \
    int32_t:            flash_set_i32,      \
    uint8_t *:          flash_set_u8,       \
    uint32_t *:         flash_set_u32,      \
    float:              flash_set_float,    \
    char *:             flash_set_str,      \
    const char *:       flash_set_str       \
)((handle), (key), (value))

/*char *:             flash_set_str,      \*/
/*  wifi_ui_state_t:    flash_get_i32       \ */


#define FLASH_TRY_SET(handle, key, value) do { \
    esp_err_t err = FLASH_SET((handle), (key), (value)); \
    if (err != ESP_OK) { \
        LOG_ERR("UTIL_FLASH_H", (err), "failed to set key '%s': ", (key)); \
        return err; \
    } \
} while (0)


#ifdef __cplusplus
}
#endif


#endif // UTIL_FLASH_H