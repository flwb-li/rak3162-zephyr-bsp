/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_UTIL_H_
#define RAK_AT_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rak_at_hex_valid(const char *s, size_t exact_len);
bool rak_at_hex_valid_n(const char *s, size_t n);
void rak_at_str_to_upper(char *s);
int rak_at_parse_hex_bytes(const char *hex, size_t hex_len, uint8_t *buf, size_t buf_len);
void rak_at_bytes_to_hex_upper(const uint8_t *src, size_t len, char *dst);

/**
 * Parse a full decimal string into @p out.
 * Rejects empty, sign, whitespace, trailing junk, and overflow.
 * @return 0 on success, -EINVAL otherwise.
 */
int rak_at_parse_ulong(const char *s, unsigned long *out);

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_UTIL_H_ */
