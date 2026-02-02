/*
 * Copyright (c) 2024 BeagleBoard.org
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT test_sdhc_stub

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sdhc_stub, CONFIG_SDHC_LOG_LEVEL);

struct sdhc_stub_config {
    struct sdhc_host_props props;
};

static int sdhc_stub_reset(const struct device *dev)
{
    return 0;
}

static int sdhc_stub_request(const struct device *dev,
                             struct sdhc_command *cmd,
                             struct sdhc_data *data)
{
    /* Mock successful responses */
    cmd->response[0] = 0x00000900; /* Ready for data */
    cmd->response[1] = 0;
    cmd->response[2] = 0;
    cmd->response[3] = 0;
    
    return 0;
}

static int sdhc_stub_set_io(const struct device *dev, struct sdhc_io *ios)
{
    return 0;
}

static int sdhc_stub_get_card_present(const struct device *dev)
{
    return 1;
}

static int sdhc_stub_get_host_props(const struct device *dev,
                                    struct sdhc_host_props *props)
{
    const struct sdhc_stub_config *config = dev->config;

    *props = config->props;
    return 0;
}

static DEVICE_API(sdhc, sdhc_stub_api) = {
    .reset = sdhc_stub_reset,
    .request = sdhc_stub_request,
    .set_io = sdhc_stub_set_io,
    .get_card_present = sdhc_stub_get_card_present,
    .get_host_props = sdhc_stub_get_host_props,
};

#define SDHC_STUB_INIT(n)                                                      \
    static const struct sdhc_stub_config sdhc_stub_config_##n = {              \
        .props = {                                                             \
            .f_max = DT_INST_PROP(n, max_bus_freq),                            \
            .f_min = DT_INST_PROP(n, min_bus_freq),                            \
            .power_delay = 0,                                                  \
            .host_caps = {                                                     \
                .vol_180_support = 1,                                          \
                .vol_300_support = 1,                                          \
                .vol_330_support = 1,                                          \
                .suspend_res_support = 0,                                      \
                .sdma_support = 0,                                             \
                .high_speed_support = 1,                                       \
                .adma_2_support = 0,                                           \
                .uahs_support = 0,                                             \
                .bus_8_bit_support = 1,                                        \
                .bus_4_bit_support = 1,                                        \
                .sdr104_support = 0,                                           \
                .sdr50_support = 0,                                            \
                .ddr50_support = 0,                                            \
                .slot_type = 0,                                                \
            },                                                                 \
            .max_current_330 = 200,                                            \
            .max_current_300 = 200,                                            \
            .max_current_180 = 200,                                            \
        },                                                                     \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL,                                 \
                          &sdhc_stub_config_##n, POST_KERNEL,                  \
                          CONFIG_SDHC_INIT_PRIORITY, &sdhc_stub_api);

DT_INST_FOREACH_STATUS_OKAY(SDHC_STUB_INIT)
