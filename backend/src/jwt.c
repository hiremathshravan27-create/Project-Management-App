
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

#include "jwt.h"

// IMPORTANT: Replace this with a securely generated random secret.
// Keep it private and never commit it to GitHub.
static const char *getJWTSecret(void) {
    return getenv("JWT_SECRET");
}

#define TOKEN_LIFETIME 3600

// Base64 URL encoding
static int base64UrlEncode(
    const unsigned char *input,
    int inputLength,
    char *output,
    int outputSize
) {
    int encodedLength = 4 * ((inputLength + 2) / 3);

    unsigned char *temporary = malloc(encodedLength + 1);

    if (temporary == NULL)
        return 0;

    EVP_EncodeBlock(temporary, input, inputLength);

    int j = 0;

    for (int i = 0; temporary[i] != '\0'; i++) {

        char c = temporary[i];

        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
        else if (c == '=')
            continue;

        if (j >= outputSize - 1) {
            free(temporary);
            return 0;
        }

        output[j++] = c;
    }

    output[j] = '\0';

    free(temporary);
    return 1;
}

// Base64 URL decoding
static int base64UrlDecode(
    const char *input,
    unsigned char *output,
    int outputSize
) {
    int length = strlen(input);

    if (length == 0 || length % 4 == 1)
        return -1;

    int paddedLength = length + (4 - length % 4) % 4;

    char *temporary = malloc(paddedLength + 1);

    if (temporary == NULL)
        return -1;

    for (int i = 0; i < length; i++) {

        char c = input[i];

        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        else if (
            !((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9'))
        ) {
            free(temporary);
            return -1;
        }

        temporary[i] = c;
    }

    for (int i = length; i < paddedLength; i++)
        temporary[i] = '=';

    temporary[paddedLength] = '\0';

    int decodedLength = EVP_DecodeBlock(
        output,
        (unsigned char *)temporary,
        paddedLength
    );

    free(temporary);

    if (decodedLength < 0)
        return -1;

    decodedLength -= (paddedLength - length);

    if (decodedLength < 0 || decodedLength > outputSize)
        return -1;

    return decodedLength;
}

// Generate JWT
int generateJWT(int userId, char *token, int tokenSize) {

    if (userId <= 0 || token == NULL || tokenSize <= 0)
        return 0;

    char header[] =
        "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

    char payload[256];

    time_t now = time(NULL);

    snprintf(
        payload,
        sizeof(payload),
        "{\"userId\":%d,\"iat\":%lld,\"exp\":%lld}",
        userId,
        (long long)now,
        (long long)(now + TOKEN_LIFETIME)
    );

    char encodedHeader[128];
    char encodedPayload[512];

    if (!base64UrlEncode(
        (unsigned char *)header,
        strlen(header),
        encodedHeader,
        sizeof(encodedHeader)
    ))
        return 0;

    if (!base64UrlEncode(
        (unsigned char *)payload,
        strlen(payload),
        encodedPayload,
        sizeof(encodedPayload)
    ))
        return 0;

    char message[1024];

    int messageLength = snprintf(
        message,
        sizeof(message),
        "%s.%s",
        encodedHeader,
        encodedPayload
    );

    if (messageLength < 0 || messageLength >= sizeof(message))
        return 0;

    unsigned char signature[EVP_MAX_MD_SIZE];

    unsigned int signatureLength = 0;

    const char *secret = getJWTSecret();

if (secret == NULL || strlen(secret) < 64)
    return 0;

if (HMAC(
    EVP_sha256(),
    secret,
    strlen(secret),
        (unsigned char *)message,
        messageLength,
        signature,
        &signatureLength
    ) == NULL)
        return 0;

    char encodedSignature[128];

    if (!base64UrlEncode(
        signature,
        signatureLength,
        encodedSignature,
        sizeof(encodedSignature)
    ))
        return 0;

    int finalLength = snprintf(
        token,
        tokenSize,
        "%s.%s",
        message,
        encodedSignature
    );

    return finalLength >= 0 && finalLength < tokenSize;
}

// Verify JWT
int verifyJWT(const char *token, int *userId) {

    if (token == NULL || userId == NULL)
        return 0;

    char tokenCopy[2048];

    if (strlen(token) >= sizeof(tokenCopy))
        return 0;

    strcpy(tokenCopy, token);

    char *firstDot = strchr(tokenCopy, '.');

    if (firstDot == NULL)
        return 0;

    *firstDot = '\0';

    char *secondPart = firstDot + 1;

    char *secondDot = strchr(secondPart, '.');

    if (secondDot == NULL)
        return 0;

    *secondDot = '\0';

    char *signaturePart = secondDot + 1;

    if (strchr(signaturePart, '.') != NULL)
        return 0;

    if (strcmp(tokenCopy, "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9") != 0)
        return 0;

    char message[1024];

    int messageLength = snprintf(
        message,
        sizeof(message),
        "%s.%s",
        tokenCopy,
        secondPart
    );

    if (messageLength < 0 || messageLength >= sizeof(message))
        return 0;

    unsigned char suppliedSignature[64];

    int suppliedLength = base64UrlDecode(
        signaturePart,
        suppliedSignature,
        sizeof(suppliedSignature)
    );

    if (suppliedLength != 32)
        return 0;

    unsigned char expectedSignature[EVP_MAX_MD_SIZE];

    unsigned int expectedLength = 0;

    if (HMAC(
        EVP_sha256(),
        getJWTSecret(),
        strlen(getJWTSecret()),
        (unsigned char *)message,
        messageLength,
        expectedSignature,
        &expectedLength
    ) == NULL)
        return 0;

    if (expectedLength != (unsigned int)suppliedLength)
        return 0;

    if (CRYPTO_memcmp(
        suppliedSignature,
        expectedSignature,
        expectedLength
    ) != 0)
        return 0;

    unsigned char decodedPayload[512];

    int payloadLength = base64UrlDecode(
        secondPart,
        decodedPayload,
        sizeof(decodedPayload) - 1
    );

    if (payloadLength <= 0)
        return 0;

    decodedPayload[payloadLength] = '\0';

    int extractedUserId = 0;
    long long expiration = 0;

    if (sscanf(
        (char *)decodedPayload,
        "{\"userId\":%d,\"iat\":%*lld,\"exp\":%lld}",
        &extractedUserId,
        &expiration
    ) != 2)
        return 0;

    if (extractedUserId <= 0 || expiration <= (long long)time(NULL))
        return 0;

    *userId = extractedUserId;

    return 1;
}
