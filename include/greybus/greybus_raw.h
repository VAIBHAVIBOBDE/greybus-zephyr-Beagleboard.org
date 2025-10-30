/*
 * Copyright (c) 2025 Ayush Singh, BeagleBoard.org
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GREYBUS_RAW_H_
#define _GREYBUS_RAW_H_

#include <stdint.h>

/**
 * Callback for greybus raw protocol.
 *
 * @param len
 * @param data
 * @param priv
 *
 * @returns gb_operation_result
 */
typedef uint8_t (*greybus_raw_cb_t)(uint32_t len, const uint8_t *data, void *priv);

/**
 * Register handler for raw protocol.
 *
 * @param cb: Callback called when data is recieved from AP.
 * @param priv: Pointer to private data that is passed when invoking the callback.
 *
 * @returns > 0 in case of success. This id will be used to send any date to AP.
 * @returns < 0 in case of error.
 */
int greybus_raw_register(greybus_raw_cb_t cb, void *priv);

/**
 * Send data to AP.
 *
 * @param id: ID obtained from greybus_raw_register.
 * @param len
 * @param data
 *
 * @returns 0 in case of success.
 * @returns < 0 in case of error.
 */
int greybus_raw_send_data(uint16_t id, uint32_t len, const uint8_t *data);

#endif // _GREYBUS_RAW_H_
