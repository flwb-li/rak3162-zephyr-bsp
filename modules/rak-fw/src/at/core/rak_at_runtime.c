/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_port.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#define RAK_AT_LINE_MAX_LEN 96
#define RAK_AT_QUEUE_DEPTH  16

LOG_MODULE_REGISTER(rak_at_rt, LOG_LEVEL_INF);

struct rak_at_line_msg {
	char line[RAK_AT_LINE_MAX_LEN];
};

K_MSGQ_DEFINE(at_line_queue, sizeof(struct rak_at_line_msg), RAK_AT_QUEUE_DEPTH, 4);
K_THREAD_STACK_DEFINE(at_worker_stack, 3072);

static struct k_thread at_worker_thread;
static bool runtime_started;
static struct rak_at_line_msg pending_line;
static size_t pending_line_len;
static atomic_t dropped_line_count;

static void reset_pending_line(void)
{
	pending_line_len = 0U;
	memset(&pending_line, 0, sizeof(pending_line));
}

static void enqueue_pending_line(void)
{
	if (pending_line_len == 0U) {
		return;
	}

	pending_line.line[pending_line_len] = '\0';

	if (k_msgq_put(&at_line_queue, &pending_line, K_NO_WAIT) != 0) {
		(void)atomic_inc(&dropped_line_count);
	}

	reset_pending_line();
}

static void handle_rx_char(uint8_t c)
{
	if ((c == '\r') || (c == '\n')) {
		enqueue_pending_line();
		return;
	}

	if (c == '\0') {
		return;
	}

	if ((c == '\b') || (c == 0x7F)) {
		if (pending_line_len > 0U) {
			pending_line_len--;
		}
		return;
	}

	if ((c < 0x20U) || (c > 0x7EU)) {
		return;
	}

	if ((c == 'T') && (pending_line_len >= 2U) &&
	    (pending_line.line[pending_line_len - 1U] == 'A')) {
		char prev2 = pending_line.line[pending_line_len - 2U];
		bool delimiter = !((prev2 >= 'A' && prev2 <= 'Z') || (prev2 >= '0' && prev2 <= '9'));

		if (delimiter) {
			pending_line.line[pending_line_len - 1U] = '\0';
			pending_line_len -= 1U;
			enqueue_pending_line();
			pending_line.line[pending_line_len++] = 'A';
			pending_line.line[pending_line_len++] = 'T';
			return;
		}
	}

	if (pending_line_len < (sizeof(pending_line.line) - 1U)) {
		pending_line.line[pending_line_len++] = (char)c;
	}
}

static void at_rx_chunk_handler(const uint8_t *data, size_t len, bool idle_boundary, void *user_data)
{
	ARG_UNUSED(user_data);

	if ((data != NULL) && (len > 0U)) {
		for (size_t i = 0U; i < len; ++i) {
			handle_rx_char(data[i]);
		}
	}

	if (idle_boundary) {
		if (!(pending_line_len == 1U && pending_line.line[0] == 'A')) {
			enqueue_pending_line();
		}
	}
}

static void at_worker_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct rak_at_line_msg msg;

	while (1) {
		if (k_msgq_get(&at_line_queue, &msg, K_FOREVER) == 0) {
			atomic_val_t dropped = atomic_set(&dropped_line_count, 0);

			if (rak_at_is_echo_enabled()) {
				rak_at_port_puts(msg.line);
				rak_at_port_puts("\r\n");
			}

			rak_at_process_line(msg.line);

			if (dropped > 0) {
				rak_at_port_puts("\r\nERROR:AT_QUEUE_FULL\r\n");
				LOG_WRN("Dropped %d AT line(s) because worker queue was full",
					(int)dropped);
			}
		}
	}
}

int rak_at_start(void)
{
	if (runtime_started) {
		return 0;
	}

	reset_pending_line();
	atomic_clear(&dropped_line_count);

	(void)rak_at_port_set_rx_handler(at_rx_chunk_handler, NULL);

	k_thread_create(&at_worker_thread, at_worker_stack, K_THREAD_STACK_SIZEOF(at_worker_stack),
			at_worker_task, NULL, NULL, NULL, 7, 0, K_NO_WAIT);

	runtime_started = true;
	LOG_INF("RAK AT runtime started");
	return 0;
}

void rak_at_rx_purge(void)
{
	struct rak_at_line_msg drop;

	reset_pending_line();
	while (k_msgq_get(&at_line_queue, &drop, K_NO_WAIT) == 0) {
		/* discard lines assembled from wake/framing noise */
	}
	(void)atomic_set(&dropped_line_count, 0);
}
