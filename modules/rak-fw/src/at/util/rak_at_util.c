/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_at/rak_at_util.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/util.h>

bool rak_at_hex_valid(const char *s, size_t exact_len)
{
	if ((s == NULL) || (strlen(s) != exact_len)) {
		return false;
	}

	return rak_at_hex_valid_n(s, exact_len);
}

bool rak_at_hex_valid_n(const char *s, size_t n)
{
	if (s == NULL) {
		return false;
	}

	for (size_t i = 0; i < n; i++) {
		if (isxdigit((unsigned char)s[i]) == 0) {
			return false;
		}
	}

	return true;
}

void rak_at_str_to_upper(char *s)
{
	if (s == NULL) {
		return;
	}

	while (*s != '\0') {
		*s = (char)toupper((unsigned char)*s);
		s++;
	}
}

int rak_at_parse_hex_bytes(const char *hex, size_t hex_len, uint8_t *buf, size_t buf_len)
{
	if (!rak_at_hex_valid(hex, hex_len)) {
		return -EINVAL;
	}

	if (hex2bin(hex, hex_len, buf, buf_len) != buf_len) {
		return -EINVAL;
	}

	return 0;
}

void rak_at_bytes_to_hex_upper(const uint8_t *src, size_t len, char *dst)
{
	static const char hex[] = "0123456789ABCDEF";

	if ((src == NULL) || (dst == NULL)) {
		return;
	}

	for (size_t i = 0; i < len; i++) {
		dst[i * 2U] = hex[(src[i] >> 4) & 0x0F];
		dst[i * 2U + 1U] = hex[src[i] & 0x0F];
	}
	dst[len * 2U] = '\0';
}

int rak_at_parse_ulong(const char *s, unsigned long *out)
{
	char *end = NULL;
	unsigned long v;

	if ((s == NULL) || (s[0] == '\0') || (out == NULL)) {
		return -EINVAL;
	}

	/* Digits only: no sign, whitespace, or leading junk. */
	if (((unsigned char)s[0] < '0') || ((unsigned char)s[0] > '9')) {
		return -EINVAL;
	}

	errno = 0;
	v = strtoul(s, &end, 10);
	if ((errno != 0) || (end == s) || (*end != '\0')) {
		return -EINVAL;
	}

	*out = v;
	return 0;
}
