/*
 * Copyright (c) 2024 BeagleBoard.org
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "greybus/greybus_messages.h"
#include <zephyr/ztest.h>
#include <greybus/greybus.h>
#include <zephyr/drivers/sdhc.h>

/* Mock specific setup if needed, or rely on real hardware */

struct gb_msg_with_cport gb_transport_get_message(void);

ZTEST_SUITE(greybus_sdio_tests, NULL, NULL, NULL, NULL, NULL);

/* Helper to normalize retrieving the response */
static struct gb_msg_with_cport get_response_checked(uint8_t type, uint16_t min_payload_len)
{
	struct gb_msg_with_cport msg = gb_transport_get_message();

	zassert(gb_message_is_success(msg.msg), "Request failed");
	zassert_equal(gb_message_type(msg.msg), type, "Invalid response type");
	zassert_true(gb_message_payload_len(msg.msg) >= min_payload_len, "Payload too small");

	return msg;
}

ZTEST(greybus_sdio_tests, test_protocol_version)
{
	struct gb_msg_with_cport resp;
	const struct gb_sdio_proto_version_response *resp_data;
	struct gb_message *msg = gb_message_request_alloc(0, GB_SDIO_TYPE_PROTOCOL_VERSION, false);

	/* Send to CPort 0 (or whatever CPort SDIO is bound to in manifest, assuming 0/1 for test setup) */
	/* Note: In integration tests, we manually inject the message into the handler */
	greybus_rx_handler(1, msg); 
	
	resp = get_response_checked(GB_RESPONSE(GB_SDIO_TYPE_PROTOCOL_VERSION), sizeof(*resp_data));
	resp_data = (const struct gb_sdio_proto_version_response *)resp.msg->payload;

	zassert_equal(resp_data->major, 0, "Invalid Major Version");
	zassert_equal(resp_data->minor, 1, "Invalid Minor Version");

	gb_message_dealloc(resp.msg);
}

ZTEST(greybus_sdio_tests, test_get_capabilities)
{
	struct gb_msg_with_cport resp;
	const struct gb_sdio_get_caps_response *resp_data;
	struct gb_message *msg = gb_message_request_alloc(0, GB_SDIO_TYPE_GET_CAPABILITIES, false);

	greybus_rx_handler(1, msg);
	
	resp = get_response_checked(GB_RESPONSE(GB_SDIO_TYPE_GET_CAPABILITIES), sizeof(*resp_data));
	resp_data = (const struct gb_sdio_get_caps_response *)resp.msg->payload;

	/* Verify some basic caps - dependent on what sdhc_get_host_props returns */
	/* We check if caps are non-zero/reasonable */
	
	/* Just ensure we got a valid response structure back */
	zassert_true(resp_data->max_blk_size > 0, "Max block size should be > 0");

	gb_message_dealloc(resp.msg);
}
