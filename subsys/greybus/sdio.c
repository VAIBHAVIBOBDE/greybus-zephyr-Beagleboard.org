/*
 * Copyright (c) 2015 Google, Inc.
 * Copyright (c) 2024 BeagleBoard.org
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <greybus/greybus.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(greybus_sdio, CONFIG_GREYBUS_LOG_LEVEL);

#define GB_SDIO_VERSION_MAJOR 0
#define GB_SDIO_VERSION_MINOR 1

#define MAX_BLOCK_SIZE_0 512
#define MAX_BLOCK_SIZE_1 1024
#define MAX_BLOCK_SIZE_2 2048

struct gb_sdio_info {
	unsigned int cport;
	const struct device *sdhc_dev;
	struct sdhc_command deferred_cmd;
	bool has_deferred_cmd;
};

static uint16_t scale_max_sd_block_length(uint16_t value)
{
	if (value < MAX_BLOCK_SIZE_0) {
		return 0;
	} else if (value < MAX_BLOCK_SIZE_1) {
		return MAX_BLOCK_SIZE_0;
	} else if (value < MAX_BLOCK_SIZE_2) {
		return MAX_BLOCK_SIZE_1;
	}
	return MAX_BLOCK_SIZE_2;
}

static uint8_t gb_sdio_protocol_version(struct gb_operation *operation)
{
	struct gb_sdio_proto_version_response *response;

	response = gb_operation_alloc_response(operation, sizeof(*response));
	if (!response) {
		return GB_OP_NO_MEMORY;
	}

	response->major = GB_SDIO_VERSION_MAJOR;
	response->minor = GB_SDIO_VERSION_MINOR;

	return GB_OP_SUCCESS;
}

static uint8_t gb_sdio_protocol_get_capabilities(struct gb_operation *operation)
{
	struct gb_sdio_get_caps_response *response;
	struct gb_bundle *bundle;
	struct gb_sdio_info *info;
	struct sdhc_host_props props = {0};
	uint16_t max_data_size;
	uint32_t caps = 0;
	int ret;

	bundle = gb_operation_get_bundle(operation);
	info = bundle->priv;

	ret = sdhc_get_host_props(info->sdhc_dev, &props);
	if (ret) {
		return gb_errno_to_op_result(ret);
	}

	response = gb_operation_alloc_response(operation, sizeof(*response));
	if (!response) {
		return GB_OP_NO_MEMORY;
	}

	max_data_size = GB_MAX_PAYLOAD_SIZE - sizeof(struct gb_sdio_transfer_response);
	max_data_size = scale_max_sd_block_length(max_data_size);
	if (!max_data_size) {
		return GB_OP_INVALID;
	}

	/* Map Zephyr Caps to Greybus Caps - Best Effort */
	if (props.host_caps.bus_4_bit_support) caps |= GB_SDIO_CAP_4_BIT_DATA;
	if (props.host_caps.bus_8_bit_support) caps |= GB_SDIO_CAP_8_BIT_DATA;
	if (props.host_caps.high_speed_support) caps |= GB_SDIO_CAP_SD_HS | GB_SDIO_CAP_MMC_HS;
	if (props.host_caps.vol_330_support) caps |= GB_SDIO_CAP_HS200_1_2V; /* Partial mapping */

	response->caps = sys_cpu_to_le32(caps);
	response->ocr = sys_cpu_to_le32(0x00FF8000); /* Placeholder: 2.7-3.6V */
	response->f_min = sys_cpu_to_le32(props.f_min);
	response->f_max = sys_cpu_to_le32(props.f_max);
	
	/* Use calculated max values */
	response->max_blk_count = sys_cpu_to_le16(max_data_size / 512); 
	response->max_blk_size = sys_cpu_to_le16(512);

	return GB_OP_SUCCESS;
}

static uint8_t gb_sdio_protocol_set_ios(struct gb_operation *operation)
{
	struct gb_sdio_set_ios_request *request;
	struct gb_bundle *bundle;
	struct gb_sdio_info *info;
	struct sdhc_io ios = {0};
	int ret;

	bundle = gb_operation_get_bundle(operation);
	info = bundle->priv;

	request = gb_operation_get_request_payload(operation);

	if (gb_operation_get_request_payload_size(operation) < sizeof(*request)) {
		LOG_ERR("dropping short message");
		return GB_OP_INVALID;
	}

	ios.clock = sys_le32_to_cpu(request->clock);
	
	switch(request->bus_mode) {
		case GB_SDIO_BUSMODE_OPENDRAIN: ios.bus_mode = SDHC_BUSMODE_OPENDRAIN; break;
		case GB_SDIO_BUSMODE_PUSHPULL: ios.bus_mode = SDHC_BUSMODE_PUSHPULL; break;
		default: ios.bus_mode = SDHC_BUSMODE_PUSHPULL;
	}

	switch(request->power_mode) {
		case GB_SDIO_POWER_OFF: ios.power_mode = SDHC_POWER_OFF; break;
		case GB_SDIO_POWER_UP: ios.power_mode = SDHC_POWER_ON; break;
		case GB_SDIO_POWER_ON: ios.power_mode = SDHC_POWER_ON; break;
		default: ios.power_mode = SDHC_POWER_OFF;
	}

	switch(request->bus_width) {
		case GB_SDIO_BUS_WIDTH_1: ios.bus_width = SDHC_BUS_WIDTH1BIT; break;
		case GB_SDIO_BUS_WIDTH_4: ios.bus_width = SDHC_BUS_WIDTH4BIT; break;
		case GB_SDIO_BUS_WIDTH_8: ios.bus_width = SDHC_BUS_WIDTH8BIT; break;
		default: ios.bus_width = SDHC_BUS_WIDTH1BIT;
	}
	
	switch(request->timing) {
		case GB_SDIO_TIMING_LEGACY: ios.timing = SDHC_TIMING_LEGACY; break;
		case GB_SDIO_TIMING_SD_HS: ios.timing = SDHC_TIMING_HS; break;
		case GB_SDIO_TIMING_MMC_HS: ios.timing = SDHC_TIMING_HS; break;
		default: ios.timing = SDHC_TIMING_LEGACY;
	}
	
	/* Voltage mapping approximated */
	switch(request->signal_voltage) {
		case GB_SDIO_SIGNAL_VOLTAGE_330: ios.signal_voltage = SD_VOL_3_3_V; break;
		case GB_SDIO_SIGNAL_VOLTAGE_180: ios.signal_voltage = SD_VOL_1_8_V; break;
		case GB_SDIO_SIGNAL_VOLTAGE_120: ios.signal_voltage = SD_VOL_1_2_V; break;
		default: ios.signal_voltage = SD_VOL_3_3_V;
	}

	ret = sdhc_set_io(info->sdhc_dev, &ios);
	if (ret) {
		return gb_errno_to_op_result(ret);
	}

	return GB_OP_SUCCESS;
}

static uint8_t gb_sdio_protocol_command(struct gb_operation *operation)
{
	struct gb_sdio_command_request *request;
	struct gb_sdio_command_response *response;
	struct gb_bundle *bundle;
	struct gb_sdio_info *info;
	struct sdhc_command cmd = {0};
	int i, ret;
	uint16_t data_blocks;

	bundle = gb_operation_get_bundle(operation);
	info = bundle->priv;

	request = gb_operation_get_request_payload(operation);

	if (gb_operation_get_request_payload_size(operation) < sizeof(*request)) {
		LOG_ERR("dropping short message");
		return GB_OP_INVALID;
	}

	data_blocks = sys_le16_to_cpu(request->data_blocks);

	cmd.opcode = request->cmd;
	cmd.arg = sys_le32_to_cpu(request->cmd_arg);
	
	/* Map flags - simplified mapping */
	if (request->cmd_flags & GB_SDIO_RSP_PRESENT) {
		if (request->cmd_flags & GB_SDIO_RSP_136) {
			cmd.response_type = SD_RSP_TYPE_R2;
		} else if (request->cmd_flags & GB_SDIO_RSP_BUSY) {
			cmd.response_type = SD_RSP_TYPE_R1b;
		} else {
			cmd.response_type = SD_RSP_TYPE_R1;
		}
	} else {
		cmd.response_type = SD_RSP_TYPE_NONE;
	}

	if (data_blocks > 0) {
		/* Defer execution */
		info->deferred_cmd = cmd;
		info->has_deferred_cmd = true;
		
		response = gb_operation_alloc_response(operation, sizeof(*response));
		if (!response) {
			return GB_OP_NO_MEMORY;
		}
		
		/* Spoof Success Response for now */
		memset(response->resp, 0, sizeof(response->resp));
		/* R1 Ready for Data */
		response->resp[0] = sys_cpu_to_le32(0x00000900); 

		return GB_OP_SUCCESS;
	}

	/* Immediate Execution */
	ret = sdhc_request(info->sdhc_dev, &cmd, NULL);
	if (ret) {
		LOG_ERR("sdhc_request failed: %d", ret);
		return gb_errno_to_op_result(ret);
	}

	response = gb_operation_alloc_response(operation, sizeof(*response));
	if (!response) {
		return GB_OP_NO_MEMORY;
	}

	for (i = 0; i < 4; i++) {
		response->resp[i] = sys_cpu_to_le32(cmd.response[i]);
	}

	return GB_OP_SUCCESS;
}

static uint8_t gb_sdio_protocol_transfer(struct gb_operation *operation)
{
	struct gb_sdio_transfer_request *request;
	struct gb_sdio_transfer_response *response;
	struct gb_bundle *bundle;
	struct gb_sdio_info *info;
	struct sdhc_data data = {0};
	int ret;
	uint16_t blocks;
	uint16_t blksz;

	bundle = gb_operation_get_bundle(operation);
	info = bundle->priv;

	request = gb_operation_get_request_payload(operation);

	if (gb_operation_get_request_payload_size(operation) < sizeof(*request)) {
		LOG_ERR("dropping short message");
		return GB_OP_INVALID;
	}
	
	blocks = sys_le16_to_cpu(request->data_blocks);
	blksz = sys_le16_to_cpu(request->data_blksz);
	
	if (!info->has_deferred_cmd) {
		LOG_ERR("Transfer request without deferred command");
		return GB_OP_INVALID;
	}

	data.block_size = blksz;
	data.blocks = blocks;

	if (request->data_flags & GB_SDIO_DATA_WRITE) {
		if (!request->data) {
			return GB_OP_INVALID;
		}
		data.data = request->data;
		data.bytes_transferred = 0;
		
		ret = sdhc_request(info->sdhc_dev, &info->deferred_cmd, &data);
		if (ret) {
			info->has_deferred_cmd = false;
			return gb_errno_to_op_result(ret);
		}
		
		response = gb_operation_alloc_response(operation, sizeof(*response));
		if (!response) {
			return GB_OP_NO_MEMORY;
		}
		response->data_blocks = sys_cpu_to_le16(blocks);
		response->data_blksz = sys_cpu_to_le16(blksz);

	} else if (request->data_flags & GB_SDIO_DATA_READ) {
		response = gb_operation_alloc_response(
			operation, sizeof(*response) + blocks * blksz);
		if (!response) {
			return GB_OP_NO_MEMORY;
		}
		data.data = response->data;
		data.bytes_transferred = 0;
		
		ret = sdhc_request(info->sdhc_dev, &info->deferred_cmd, &data);
		if (ret) {
			info->has_deferred_cmd = false;
			return gb_errno_to_op_result(ret);
		}
		
		response->data_blocks = sys_cpu_to_le16(blocks);
		response->data_blksz = sys_cpu_to_le16(blksz);
	} else {
		return GB_OP_INVALID;
	}
	
	info->has_deferred_cmd = false;
	return GB_OP_SUCCESS;
}

static int gb_sdio_init(unsigned int cport, struct gb_bundle *bundle)
{
	struct gb_sdio_info *info;
	const struct device *dev;

	DEBUGASSERT(bundle);

	dev = device_get_binding(CONFIG_GREYBUS_SDIO_CONTROLLER_NAME);
	if (!dev) {
		LOG_ERR("SDHC Device not found");
		return -ENODEV;
	}
	
	if (!device_is_ready(dev)) {
		LOG_ERR("SDHC Device not ready");
		return -ENODEV;
	}

	info = zalloc(sizeof(*info));
	if (info == NULL) {
		return -ENOMEM;
	}

	info->cport = cport;
	info->sdhc_dev = dev;
	info->has_deferred_cmd = false;

	bundle->priv = info;

	return 0;
}

static void gb_sdio_exit(unsigned int cport, struct gb_bundle *bundle)
{
	struct gb_sdio_info *info;

	DEBUGASSERT(bundle);
	info = bundle->priv;

	DEBUGASSERT(cport == info->cport);

	free(info);
	bundle->priv = NULL;
}

static uint8_t gb_sdio_handler(uint8_t type, struct gb_operation *opr)
{
	switch (type) {
	case GB_SDIO_TYPE_PROTOCOL_VERSION:
		return gb_sdio_protocol_version(opr);
	case GB_SDIO_TYPE_GET_CAPABILITIES:
		return gb_sdio_protocol_get_capabilities(opr);
	case GB_SDIO_TYPE_SET_IOS:
		return gb_sdio_protocol_set_ios(opr);
	case GB_SDIO_TYPE_COMMAND:
		return gb_sdio_protocol_command(opr);
	case GB_SDIO_TYPE_TRANSFER:
		return gb_sdio_protocol_transfer(opr);
	default:
		LOG_ERR("Invalid type: %u", type);
		return GB_OP_INVALID;
	}
}

static struct gb_driver sdio_driver = {
	.init = gb_sdio_init,
	.exit = gb_sdio_exit,
	.op_handler = gb_sdio_handler,
};

void gb_sdio_register(int cport, int bundle)
{
	gb_register_driver(cport, bundle, &sdio_driver);
}
