# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ra6m5_coremark_spi)

set(REQUIRED_SDK_VERSION 1.0.1)
if(NOT SDK_VERSION VERSION_EQUAL REQUIRED_SDK_VERSION)
  message(FATAL_ERROR "This project needs Zephyr SDK ${REQUIRED_SDK_VERSION}. Install it with 'west sdk install --version 1.0.1 -t arm-zephyr-eabi'.")
endif()

set(COREMARK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/coremark)

target_sources(app PRIVATE
  src/main.c
  src/SPI/spi.c
  src/UART/uart.c
  src/PWM/pwm.c
  src/ADC/adc.c
  src/QSPI/qspi.c

  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/drivers/ra/fsp/src/r_adc/r_adc.c

  ${COREMARK_DIR}/port/core_portme.c

  ${COREMARK_DIR}/core_list_join.c
  ${COREMARK_DIR}/core_main.c
  ${COREMARK_DIR}/core_matrix.c
  ${COREMARK_DIR}/core_state.c
  ${COREMARK_DIR}/core_util.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SPI
  ${CMAKE_CURRENT_SOURCE_DIR}/src/UART
  ${COREMARK_DIR}
  ${COREMARK_DIR}/port
)

set_source_files_properties(${COREMARK_DIR}/core_main.c
  PROPERTIES COMPILE_DEFINITIONS "main=coremark_main")
