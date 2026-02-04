/*
 * Copyright (c) 2015 Google, Inc.
 * Copyright (c) 2025 Ayush Singh, BeagleBoard.org
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GREYBUS_HID_GB_H_
#define _GREYBUS_HID_GB_H_

#include <zephyr/types.h>
#include <zephyr/device.h>

struct hid_descriptor {
	uint8_t length;
	uint16_t report_desc_length;
	uint16_t hid_version;
	uint16_t product_id;
	uint16_t vendor_id;
	uint8_t country_code;
};

/* HID Report Types */
#ifndef GB_HID_INPUT_REPORT
#define GB_HID_INPUT_REPORT   0
#endif
#ifndef GB_HID_OUTPUT_REPORT
#define GB_HID_OUTPUT_REPORT  1
#endif
#ifndef GB_HID_FEATURE_REPORT
#define GB_HID_FEATURE_REPORT 2
#endif

typedef int (*hid_cb_t)(const struct device *dev, void *data, uint8_t report_type, uint8_t *report,
			uint16_t len);

/* API structure to be implemented by the underlying HID driver */
struct device_hid_api {
	int (*get_descriptor)(const struct device *dev, struct hid_descriptor *desc);
	int (*get_report_descriptor)(const struct device *dev, uint8_t *desc);
	int (*power_on)(const struct device *dev);
	int (*power_off)(const struct device *dev);
	int (*get_report)(const struct device *dev, uint8_t report_type, uint8_t report_id,
			  uint8_t *data, uint16_t len);
	int (*get_report_length)(const struct device *dev, uint8_t report_type, uint8_t report_id);
    int (*get_max_report_length)(const struct device *dev, uint8_t report_type);
	int (*set_report)(const struct device *dev, uint8_t report_type, uint8_t report_id,
			  uint8_t *data, uint16_t len);
	int (*register_callback)(const struct device *dev, void *data, hid_cb_t callback);
	int (*unregister_callback)(const struct device *dev);
};

#endif /* _GREYBUS_HID_GB_H_ */
