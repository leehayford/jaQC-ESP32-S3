#include "model_sample.h"
#include "util_err.h"
#include "mbedtls/base64.h"

cJSON *sample_to_json(const sample_t *s) {
    if (!s) return NULL;
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "hw_class",   s->hw_class);
    cJSON_AddStringToObject(root, "hw_version", s->hw_version);
    cJSON_AddStringToObject(root, "serial",     s->serial);
    cJSON_AddNumberToObject(root, "seq",        (double)s->seq);
    cJSON_AddNumberToObject(root, "ts_ms",      (double)s->timestamp_ms);
    cJSON_AddNumberToObject(root, "fs",         (double)s->sample_rate_hz);

    // base64-encode 512B blob
    size_t out_len = 0;
    unsigned char *out = NULL;
    // int rc = mbedtls_base64_encode(NULL, 0, &out_len, s->blob, sizeof(s->blob));
    mbedtls_base64_encode(NULL, 0, &out_len, s->blob, sizeof(s->blob));
    out = (unsigned char*)malloc(out_len + 1);
    if (out && mbedtls_base64_encode(out, out_len, &out_len, s->blob, sizeof(s->blob)) == 0) {
        out[out_len] = '\0';
        cJSON_AddStringToObject(root, "blob_b64", (const char*)out);
        free(out);
    } else {
        cJSON_AddStringToObject(root, "blob_b64", "");
        if (out) free(out);
    }
    return root;
}
