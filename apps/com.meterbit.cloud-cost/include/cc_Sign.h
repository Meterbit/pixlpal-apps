/**
 * @file cc_Sign.h
 * @brief SHA-256, HMAC-SHA-256 and AWS Signature Version 4, for Cloud Cost Meter's AWS Cost Explorer request (the
 *        app's kit has no hashing). Plain C++ without the app framework, so it is tested on a computer too.
 */
#ifndef CC_SIGN_H
#define CC_SIGN_H

#include <stddef.h>
#include <stdint.h>

struct Cc_Sha256_t { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; size_t used; };

void cc_Sha256_Begin(Cc_Sha256_t& s);
void cc_Sha256_Add(Cc_Sha256_t& s, const void* data, size_t n);
void cc_Sha256_End(Cc_Sha256_t& s, uint8_t out[32]);
void cc_Sha256(const void* data, size_t n, uint8_t out[32]);
void cc_Hmac_Sha256(const void* key, size_t keyN, const void* data, size_t n, uint8_t out[32]);
/** 32 bytes as 64 lower-case hex digits and a 0. */
void cc_Hex(const uint8_t in[32], char out[65]);

/**
 * The Authorization header of an AWS SigV4 POST to https://<host>/ with the headers content-type, host, x-amz-date
 * and x-amz-target (signed in that order).
 * @param amzDate  "20261121T120000Z" (the x-amz-date header's value); its first 8 characters are the scope's date.
 * @return false if @p out is too small.
 */
bool cc_Aws_Authorization(const char* accessKey, const char* secretKey, const char* region, const char* service,
                          const char* host, const char* contentType, const char* target, const char* amzDate,
                          const char* body, size_t bodyN, char* out, size_t outN);

#endif
