/*
 * Copyright (c) 2015 Google Inc.
 * Copyright (c) 2025 BeagleBoard.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Fabien Parent <fparent@baylibre.com>
 * Author: Ayush Singh <ayush@beagleboard.org>
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/usb/uhc.h>

#include <greybus/greybus.h>
#include <greybus/greybus_protocols.h>

LOG_MODULE_REGISTER(greybus_usb, CONFIG_GREYBUS_LOG_LEVEL);

static const struct device *usbdev;

static uint8_t gb_usb_protocol_version(struct gb_operation *operation)
{
	struct gb_usb_proto_version_response *response;

	response = gb_operation_alloc_response(operation, sizeof(*response));
	if (!response) {
		return GB_OP_NO_MEMORY;
	}

	response->major = GB_USB_VERSION_MAJOR;
	response->minor = GB_USB_VERSION_MINOR;
	return GB_OP_SUCCESS;
}

static uint8_t gb_usb_hcd_stop(struct gb_operation *operation)
{
	int ret;

	LOG_DBG("%s()", __func__);

	if (!usbdev) {
		return GB_OP_UNKNOWN_ERROR;
	}

	ret = uhc_disable(usbdev);
	if (ret < 0) {
		LOG_ERR("Failed to stop HCD: %d", ret);
		return GB_OP_UNKNOWN_ERROR;
	}

	return GB_OP_SUCCESS;
}

static uint8_t gb_usb_hcd_start(struct gb_operation *operation)
{
	int ret;

	LOG_DBG("%s()", __func__);

	if (!usbdev) {
		return GB_OP_UNKNOWN_ERROR;
	}

	ret = uhc_enable(usbdev);
	if (ret < 0) {
		LOG_ERR("Failed to start HCD: %d", ret);
		return GB_OP_UNKNOWN_ERROR;
	}

	return GB_OP_SUCCESS;
}

static uint8_t gb_usb_hub_control(struct gb_operation *operation)
{
	struct gb_usb_hub_control_response *response;
	struct gb_usb_hub_control_request *request = gb_operation_get_request_payload(operation);
	struct usb_setup_packet setup;
	uint16_t wLength;
	int ret;

	if (gb_operation_get_request_payload_size(operation) < sizeof(*request)) {
		return GB_OP_INVALID;
	}

	setup.bmRequestType = (uint8_t)(sys_le16_to_cpu(request->typeReq) & 0xFF);
	setup.bRequest = (uint8_t)(sys_le16_to_cpu(request->typeReq) >> 8);
	setup.wValue = sys_le16_to_cpu(request->wValue);
	setup.wIndex = sys_le16_to_cpu(request->wIndex);
	setup.wLength = sys_le16_to_cpu(request->wLength);
	
	wLength = setup.wLength;

	response = gb_operation_alloc_response(operation, sizeof(*response) + wLength);
	if (!response) {
		return GB_OP_NO_MEMORY;
	}

	LOG_DBG("%s(Req: %x, Val: %x, Idx: %x, Len: %x)", __func__, setup.bRequest, setup.wValue, setup.wIndex, wLength);

	/* 
	 * Note: Zephyr UHC API usually handles Root Hub control internally or 
	 * doesn't expose a direct API to inject setup packets to the root hub 
	 * from an external source purely via API.
	 * However, many UHC logic implementations have a handling function.
	 * For now, we will assume uhc_root_ctrl or similar is available or 
	 * we might need to rely on a custom mapping.
	 * 
	 * IF `uhc_root_ctrl` is not available publically, this might fail to compile.
	 * But we must try to hook it up.
	 * 
	 * Alternatively, we might need to handle this via `uhc_ep_enqueue` to control pipe?
	 * DWC2/etc might behave differently.
	 * 
	 * Let's try `uhc_custom_request` or similar if it exists? No.
	 * 
	 * FIXME: If uhc_root_ctrl is not available, this needs to be adapted.
	 */
	
	// ret = uhc_root_ctrl(usbdev, &setup, response->buf); 
    // Commenting out the direct call to avoid compilation error if API doesn't exist
    // and instead returning not supported until verified.
    // Or better, let's just log and return SUCCESS for 0-length request to fake it?
    
    // For now, fail gently or pretend success if it's strictly required?
    // The previous NuttX code did: device_usb_hcd_hub_control(...)
    
    // We will return an error to indicate it's not implemented yet, 
    // unless we find the API.
    LOG_WRN("hub_control not fully implemented for Zephyr UHC");
    ret = -ENOTSUP;

	if (ret) {
		return GB_OP_UNKNOWN_ERROR;
	}

	return GB_OP_SUCCESS;
}

static int gb_usb_init(unsigned int cport, struct gb_bundle *bundle)
{
	/* 
	 * Find the USB host controller.
	 * This assumes a node label 'usb0' or similar exists and is enabled.
	 * Ideally this should be configurable via Kconfig.
	 */
#if DT_NODE_EXISTS(DT_NODELABEL(usb0)) && DT_NODE_HAS_COMPAT(DT_NODELABEL(usb0), zephyr_uhc)
	usbdev = DEVICE_DT_GET(DT_NODELABEL(usb0));
#elif DT_HAS_CHOSEN(zephyr_usb_host)
	usbdev = DEVICE_DT_GET(DT_CHOSEN(zephyr_usb_host));
#else
	/* Fallback or manual lookup needed */
    // Try to find any device with UHC API?
	usbdev = NULL; 
#endif

	if (!usbdev || !device_is_ready(usbdev)) {
		LOG_ERR("USB Host Device not found or not ready");
		return -ENODEV;
	}

	return 0;
}

static void gb_usb_exit(unsigned int cport, struct gb_bundle *bundle)
{
	if (usbdev) {
	    uhc_disable(usbdev);
	}
}

static uint8_t gb_usb_handler(uint8_t type, struct gb_operation *opr)
{
	switch (type) {
	case GB_USB_TYPE_PROTOCOL_VERSION:
		return gb_usb_protocol_version(opr);
	case GB_USB_TYPE_HCD_STOP:
		return gb_usb_hcd_stop(opr);
	case GB_USB_TYPE_HCD_START:
		return gb_usb_hcd_start(opr);
	case GB_USB_TYPE_HUB_CONTROL:
		return gb_usb_hub_control(opr);
	default:
		LOG_ERR("Invalid type: %u", type);
		return GB_OP_INVALID;
	}
}

static struct gb_driver usb_driver = {
	.init = gb_usb_init,
	.exit = gb_usb_exit,
	.op_handler = gb_usb_handler,
};

void gb_usb_register(int cport, int bundle)
{
	gb_register_driver(cport, bundle, &usb_driver);
}

