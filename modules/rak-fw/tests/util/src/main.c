/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <rak_at/rak_at_util.h>

ZTEST(rak_at_util, test_hex_valid_exact)
{
	zassert_true(rak_at_hex_valid("0011223344556677", 16), "valid deveui hex");
	zassert_false(rak_at_hex_valid("001122334455667", 16), "too short");
	zassert_false(rak_at_hex_valid("001122334455667G", 16), "non-hex");
}

ZTEST(rak_at_util, test_parse_hex_bytes)
{
	uint8_t buf[8];
	char out[17];

	zassert_ok(rak_at_parse_hex_bytes("0011223344556677", 16, buf, sizeof(buf)));
	zassert_equal(buf[0], 0x00);
	zassert_equal(buf[1], 0x11);
	zassert_equal(buf[7], 0x77);

	rak_at_bytes_to_hex_upper(buf, sizeof(buf), out);
	zassert_mem_equal(out, "0011223344556677", 16);
}

ZTEST(rak_at_util, test_parse_appkey_len)
{
	uint8_t key[16];

	zassert_ok(rak_at_parse_hex_bytes("00112233445566778899AABBCCDDEEFF", 32, key,
					  sizeof(key)));
	zassert_equal(key[15], 0xFF);
	zassert_equal(rak_at_parse_hex_bytes("001122", 6, key, sizeof(key)), -EINVAL);
}

ZTEST(rak_at_util, test_parse_ulong)
{
	unsigned long v = 99UL;

	zassert_ok(rak_at_parse_ulong("0", &v));
	zassert_equal(v, 0UL);
	zassert_ok(rak_at_parse_ulong("123", &v));
	zassert_equal(v, 123UL);

	zassert_equal(rak_at_parse_ulong("", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong("abc", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong("1x", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong("-1", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong("+1", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong(" 1", &v), -EINVAL);
	zassert_equal(rak_at_parse_ulong(NULL, &v), -EINVAL);
}

ZTEST_SUITE(rak_at_util, NULL, NULL, NULL, NULL, NULL);
