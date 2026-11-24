#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArTokenizer ArTokenizer;

typedef struct ArEncoding {
    int64_t* ids;
    size_t* starts;
    size_t* ends;
    size_t* tokenOffsets;
    uint8_t* tokenText;
    size_t tokenTextLen;
    size_t len;
} ArEncoding;

typedef struct ArBuffer {
    uint8_t* data;
    size_t len;
} ArBuffer;

ArTokenizer* ar_tokenizer_create(const char* tokenizerJsonPath);
void ar_tokenizer_destroy(ArTokenizer* tokenizer);
size_t ar_tokenizer_vocab_size(const ArTokenizer* tokenizer);
int ar_tokenizer_encode(
    const ArTokenizer* tokenizer,
    const uint8_t* text,
    size_t textLen,
    ArEncoding** encoding);
void ar_encoding_free(ArEncoding* encoding);
int ar_tokenizer_decode(
    const ArTokenizer* tokenizer,
    const int64_t* ids,
    size_t len,
    ArBuffer* decoded);
void ar_buffer_free(ArBuffer* buffer);

#ifdef __cplusplus
}
#endif
