/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_fw/board.h>

static const struct rak_fw_board_ops *board_ops;
static const struct rak_fw_cfg_ops *cfg_ops;

void rak_fw_set_board_ops(const struct rak_fw_board_ops *ops)
{
	board_ops = ops;
}

const struct rak_fw_board_ops *rak_fw_board_ops(void)
{
	return board_ops;
}

void rak_fw_set_cfg_ops(const struct rak_fw_cfg_ops *ops)
{
	cfg_ops = ops;
}

const struct rak_fw_cfg_ops *rak_fw_cfg_ops(void)
{
	return cfg_ops;
}
