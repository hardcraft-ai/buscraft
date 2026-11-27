// Minimal mbed TLS: AES-CTR and SHA-256 for encrypting the stored Wi-Fi settings.
#pragma once

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_SHA256_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEWER_TABLES
