# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(greybus_sdio_test)

target_sources(app PRIVATE src/main.c)

# GREYBUS_SDIO requires generic GREYBUS support
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/include)

if("${BOARD}" STREQUAL "native_sim")
  target_sources(app PRIVATE src/sdhc_stub.c)
  list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)
endif()
