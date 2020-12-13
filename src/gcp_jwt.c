#include "gcp_jwt.h"
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <time.h>

static const char *TAG = "GCP_JWT";
/**
 * Return a string representation of an mbedtls error code
 */
static char *mbedtlsError(int errnum)
{
    static char buffer[200];
    mbedtls_strerror(errnum, buffer, sizeof(buffer));
    return buffer;
} // mbedtlsError

/**
 * Create a JWT token for GCP.
 * For full details, perform a Google search on JWT.  However, in summary, we build two strings.  One that represents the
 * header and one that represents the payload.  Both are JSON and are as described in the GCP and JWT documentation.  Next
 * we base64url encode both strings.  Note that is distinct from normal/simple base64 encoding.  Once we have a string for
 * the base64url encoding of both header and payload, we concatenate both strings together separated by a ".".   This resulting
 * string is then signed using RSASSA which basically produces an SHA256 message digest that is then signed.  The resulting
 * binary is then itself converted into base64url and concatenated with the previously built base64url combined header and
 * payload and that is our resulting JWT token.
 * @param projectId The GCP project.
 * @param privateKey The PEM or DER of the private key.
 * @param privateKeySize The size in bytes of the private key.
 * @returns A JWT token for transmission to GCP.
 */
char *create_GCP_JWT(const char *projectId, const char *privateKey, size_t privateKeySize)
{
    ESP_LOGD(TAG, "[create_GCP_JWT] start");
    static const char *JWT_BASE_64_HEADER = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9"; // {"alg":"RS256","typ":"JWT"}"

    time_t now;
    time(&now);
    uint32_t iat = now;                // Set the time now.
    uint32_t exp = iat + 60 * 60 * 24; // Set the expiry time.

    char *payload;
    asprintf(&payload, "{\"iat\":%d,\"exp\":%d,\"aud\":\"%s\"}", iat, exp, projectId);
    ESP_LOGD(TAG, "[create_GCP_JWT] payload: %s", payload);

    unsigned char *base64Payload;
    size_t base_64_payload_size;
    mbedtls_base64_encode(NULL, 0, &base_64_payload_size, (const unsigned char *)payload, strlen((char *)payload));
    base64Payload = calloc(base_64_payload_size, sizeof(*base64Payload));
    mbedtls_base64_encode(base64Payload, base_64_payload_size, &base_64_payload_size, (const unsigned char *)payload, strlen((char *)payload));
    ESP_LOGD(TAG, "[create_GCP_JWT] base64 payload: %s", base64Payload);

    free(payload);

    char *headerAndPayload;
    asprintf(&headerAndPayload, "%s.%s", JWT_BASE_64_HEADER, base64Payload);
    ESP_LOGD(TAG, "[create_GCP_JWT] headerAndPayload: %s", headerAndPayload);

    free(base64Payload);

    // At this point we have created the header and payload parts, converted both to base64 and concatenated them
    // together as a single string.  Now we need to sign them using RSASSA
    uint8_t digest[32];
    int rc = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (unsigned char *)headerAndPayload, strlen((char *)headerAndPayload), digest);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to mbedtls_md: %d (-0x%x): %s\n", rc, -rc, mbedtlsError(rc));
        return NULL;
    }

    mbedtls_pk_context pk_context;
    mbedtls_pk_init(&pk_context);
    rc = mbedtls_pk_parse_key(&pk_context, (const unsigned char *) privateKey, privateKeySize, NULL, 0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to mbedtls_pk_parse_key: %d (-0x%x): %s\n", rc, -rc, mbedtlsError(rc));
        abort();
    }

    size_t sig_len = mbedtls_pk_get_len(&pk_context);
    uint8_t oBuf[sig_len];
    size_t retSize;
    rc = mbedtls_pk_sign(&pk_context, MBEDTLS_MD_SHA256, digest, sizeof(digest), oBuf, &retSize, NULL, NULL);
    mbedtls_pk_free(&pk_context);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to mbedtls_pk_sign: %d (-0x%x): %s\n", rc, -rc, mbedtlsError(rc));
        abort();
    }

    unsigned char *base64Signature;
    size_t base_64_signature_size;
    mbedtls_base64_encode(NULL, 0, &base_64_signature_size, oBuf, retSize);
    base64Signature = calloc(base_64_signature_size, sizeof(*base64Signature));
    mbedtls_base64_encode(base64Signature, base_64_signature_size, &base_64_signature_size, oBuf, retSize);

    char *retData;
    asprintf(&retData, "%s.%s", headerAndPayload, base64Signature);
    free(headerAndPayload);
    free(base64Signature);
    ESP_LOGD(TAG, "[create_GCP_JWT] jwt: %s", retData);
    return retData;
}