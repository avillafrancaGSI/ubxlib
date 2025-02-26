/*
 * Copyright 2019-2023 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Only #includes of u_* and the C standard library are allowed here,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/OS must be brought in through u_port* to maintain
 * portability.
 */

/** @file
 * @brief Tests for the GNSS "general" API: these should pass on all
 * platforms where one or preferably two UARTs are available.  No
 * GNSS module is actually used in this set of tests.
 * IMPORTANT: see notes in u_cfg_test_platform_specific.h for the
 * naming rules that must be followed when using the U_PORT_TEST_FUNCTION()
 * macro.
 */

#ifdef U_CFG_OVERRIDE
# include "u_cfg_override.h" // For a customer's configuration override
#endif

#include "stddef.h"    // NULL, size_t etc.
#include "stdint.h"    // int32_t etc.
#include "stdbool.h"
#include "string.h"    // memcmp()

#include "u_cfg_sw.h"
#include "u_cfg_os_platform_specific.h"
#include "u_cfg_app_platform_specific.h"
#include "u_cfg_test_platform_specific.h"

#include "u_error_common.h"

#include "u_port.h"
#include "u_port_os.h"
#include "u_port_heap.h"
#include "u_port_debug.h"
#include "u_port_uart.h"
#include "u_port_i2c.h"
#include "u_port_spi.h"

#include "u_test_util_resource_check.h"

#include "u_gnss_module_type.h"
#include "u_gnss_type.h"
#include "u_gnss.h"

#if (U_CFG_APP_GNSS_I2C >= 0) && defined(U_GNSS_TEST_I2C_ADDRESS_EXTRA)
# include "u_gnss_pwr.h"  // So that we can do something with the extra address
# include "u_gnss_info.h" // To print something GNSS-module specific, show that we're not accidentally using address 0x42
# include "u_gnss_msg.h"  // uGnssMsgReceiveStatStreamLoss()
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The string to put at the start of all prints from this test.
 */
#define U_TEST_PREFIX "U_GNSS_TEST: "

/** Print a whole line, with terminator, prefixed for this test file.
 */
#define U_TEST_PRINT_LINE(format, ...) uPortLog(U_TEST_PREFIX format "\n", ##__VA_ARGS__)

#if (U_CFG_APP_GNSS_I2C >= 0) && defined(U_GNSS_TEST_I2C_ADDRESS_EXTRA)
# ifndef U_GNSS_TEST_BUFFER_SIZE_BYTES
/** The buffer to use when comparing version strings
 */
#  define U_GNSS_TEST_BUFFER_SIZE_BYTES 1024
# endif
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/** Streaming handle for one GNSS module (could be UART or I2C).
 */
static int32_t gStreamAHandle = -1;

/** The type of streaming handle A.
 */
static uGnssTransportType_t gTransportTypeA = U_GNSS_TRANSPORT_NONE;

/** UART handle for another GNSS module.
 */
static int32_t gUartBHandle = -1;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

/** Basic test: initialise and then de-initialise a GNSS chip.
 *
 * IMPORTANT: see notes in u_cfg_test_platform_specific.h for the
 * naming rules that must be followed when using the
 * U_PORT_TEST_FUNCTION() macro.
 */
U_PORT_TEST_FUNCTION("[gnss]", "gnssInitialisation")
{
    U_PORT_TEST_ASSERT(uPortInit() == 0);
    U_PORT_TEST_ASSERT(uGnssInit() == 0);
    uGnssDeinit();
    uPortDeinit();
}

#if (U_CFG_TEST_UART_A >= 0) || (U_CFG_APP_GNSS_I2C >= 0) || (U_CFG_APP_GNSS_SPI >= 0)
/** Add a streaming GNSS instance, e.g. UART or I2C or SPI,
 * and remove it again.
 */
U_PORT_TEST_FUNCTION("[gnss]", "gnssAddStream")
{
    uDeviceHandle_t gnssHandleA;
# if (U_CFG_APP_GNSS_SPI >= 0) && (U_CFG_APP_GNSS_I2C < 0)
#  ifdef U_CFG_TEST_GNSS_SPI_SELECT_INDEX
    uCommonSpiControllerDevice_t device = U_COMMON_SPI_CONTROLLER_DEVICE_INDEX_DEFAULTS(
                                              U_CFG_TEST_GNSS_SPI_SELECT_INDEX);
#  else
    uCommonSpiControllerDevice_t device = U_COMMON_SPI_CONTROLLER_DEVICE_DEFAULTS(
                                              U_CFG_APP_PIN_GNSS_SPI_SELECT);
#  endif
# endif
# if ((U_CFG_APP_GNSS_SPI < 0) && (U_CFG_APP_GNSS_I2C < 0)) || (U_CFG_TEST_UART_B >= 0)
    uDeviceHandle_t dummyHandle;
# endif
    uGnssTransportHandle_t transportHandleA;
# if (U_CFG_TEST_UART_B >= 0)
    uDeviceHandle_t gnssHandleB;
    uGnssTransportHandle_t transportHandleB;
# endif
    uGnssTransportType_t transportType = U_GNSS_TRANSPORT_NONE;
    uGnssTransportHandle_t transportHandle;
    int32_t errorCode;
    int32_t resourceCount;
    bool printUbxMessagesDefault;

    // Whatever called us likely initialised the
    // port so deinitialise it here to obtain the
    // correct initial heap size
    uPortDeinit();
    resourceCount = uTestUtilGetDynamicResourceCount();

    U_PORT_TEST_ASSERT(uPortInit() == 0);

# if (U_CFG_APP_GNSS_I2C >= 0)
    U_PORT_TEST_ASSERT(uPortI2cInit() == 0);

    gStreamAHandle = uPortI2cOpen(U_CFG_APP_GNSS_I2C,
                                  U_CFG_APP_PIN_GNSS_SDA,
                                  U_CFG_APP_PIN_GNSS_SCL,
                                  true);
    U_PORT_TEST_ASSERT(gStreamAHandle >= 0);
    gTransportTypeA = U_GNSS_TRANSPORT_I2C;
    transportHandleA.i2c = gStreamAHandle;
# elif (U_CFG_APP_GNSS_SPI >= 0)
    U_PORT_TEST_ASSERT(uPortSpiInit() == 0);
    gStreamAHandle = uPortSpiOpen(U_CFG_APP_GNSS_SPI,
                                  U_CFG_APP_PIN_GNSS_SPI_MOSI,
                                  U_CFG_APP_PIN_GNSS_SPI_MISO,
                                  U_CFG_APP_PIN_GNSS_SPI_CLK,
                                  true);
    U_PORT_TEST_ASSERT(gStreamAHandle >= 0);
    U_PORT_TEST_ASSERT(uPortSpiControllerSetDevice(gStreamAHandle, &device) == 0);
    gTransportTypeA = U_GNSS_TRANSPORT_SPI;
    transportHandleA.spi = gStreamAHandle;
# else
#  ifdef U_CFG_TEST_UART_PREFIX
    U_PORT_TEST_ASSERT(uPortUartPrefix(U_PORT_STRINGIFY_QUOTED(U_CFG_TEST_UART_PREFIX)) == 0);
#  endif
    gStreamAHandle = uPortUartOpen(U_CFG_TEST_UART_A,
                                   U_CFG_TEST_BAUD_RATE,
                                   NULL,
                                   U_GNSS_UART_BUFFER_LENGTH_BYTES,
                                   U_CFG_TEST_PIN_UART_A_TXD,
                                   U_CFG_TEST_PIN_UART_A_RXD,
                                   U_CFG_TEST_PIN_UART_A_CTS,
                                   U_CFG_TEST_PIN_UART_A_RTS);
    U_PORT_TEST_ASSERT(gStreamAHandle >= 0);
    gTransportTypeA = U_GNSS_TRANSPORT_UART;
    transportHandleA.uart = gStreamAHandle;
# endif

    U_PORT_TEST_ASSERT(uGnssInit() == 0);

    U_TEST_PRINT_LINE("adding a GNSS instance on streaming port...");
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         gTransportTypeA, transportHandleA,
                         -1, false, &gnssHandleA);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);
    transportHandle.uart = -1;
    transportHandle.i2c = -1;
    U_PORT_TEST_ASSERT(uGnssGetTransportHandle(gnssHandleA,
                                               &transportType,
                                               &transportHandle) == 0);
    switch (gTransportTypeA) {
        case U_GNSS_TRANSPORT_UART:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART);
            U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleA.uart);
#if defined(_WIN32) || (defined(__ZEPHYR__) && defined(CONFIG_UART_NATIVE_POSIX))
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_USB);
#else
# ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_UART1);
# else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
# endif
#endif
            break;
        case U_GNSS_TRANSPORT_UART_2:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART_2);
            U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleA.uart);
#if defined(_WIN32) || (defined(__ZEPHYR__) && defined(CONFIG_UART_NATIVE_POSIX))
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_USB);
#else
# ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_UART2);
# else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
# endif
#endif
            break;
        case U_GNSS_TRANSPORT_I2C:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_I2C);
            U_PORT_TEST_ASSERT(transportHandle.i2c == transportHandleA.i2c);
#ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_I2C);
#else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
#endif
            break;
        case U_GNSS_TRANSPORT_SPI:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_SPI);
            U_PORT_TEST_ASSERT(transportHandle.spi == transportHandleA.spi);
#ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_SPI);
#else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
#endif
            break;
        default:
            U_PORT_TEST_ASSERT(false);
            break;
    }

    printUbxMessagesDefault = uGnssGetUbxMessagePrint(gnssHandleA);
    uGnssSetUbxMessagePrint(gnssHandleA, !printUbxMessagesDefault);
    if (printUbxMessagesDefault) {
        U_PORT_TEST_ASSERT(!uGnssGetUbxMessagePrint(gnssHandleA));
    } else {
        U_PORT_TEST_ASSERT(uGnssGetUbxMessagePrint(gnssHandleA));
    }

# if (U_CFG_APP_GNSS_I2C < 0) && (U_CFG_APP_GNSS_SPI < 0)
    U_TEST_PRINT_LINE("adding another instance on the same UART"
                      " port, should fail...");
    // This time we use U_GNSS_TRANSPORT_UART_2, just for variety;
    // it should make no difference which one we use, both should
    // fail since transportHandleA is the same.
    U_PORT_TEST_ASSERT(uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                                U_GNSS_TRANSPORT_UART_2,
                                transportHandleA,
                                -1, false, &dummyHandle) < 0);
    // Close it and re-open using U_GNSS_TRANSPORT_UART_2:
    // this should work
    uGnssRemove(gnssHandleA);
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         U_GNSS_TRANSPORT_UART_2, transportHandleA,
                         -1, false, &gnssHandleA);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);
    transportHandle.uart = -1;
    transportHandle.i2c = -1;
    U_PORT_TEST_ASSERT(uGnssGetTransportHandle(gnssHandleA,
                                               &transportType,
                                               &transportHandle) == 0);
    U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART_2);
    U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleA.uart);
# endif

# if (U_CFG_TEST_UART_B >= 0)
    // If we have a second UART port, add a second GNSS API on it
#  ifdef U_CFG_TEST_UART_PREFIX
    U_PORT_TEST_ASSERT(uPortUartPrefix(U_PORT_STRINGIFY_QUOTED(U_CFG_TEST_UART_PREFIX)) == 0);
#  endif
    gUartBHandle = uPortUartOpen(U_CFG_TEST_UART_B,
                                 U_CFG_TEST_BAUD_RATE,
                                 NULL,
                                 U_GNSS_UART_BUFFER_LENGTH_BYTES,
                                 U_CFG_TEST_PIN_UART_B_TXD,
                                 U_CFG_TEST_PIN_UART_B_RXD,
                                 U_CFG_TEST_PIN_UART_B_CTS,
                                 U_CFG_TEST_PIN_UART_B_RTS);
    U_PORT_TEST_ASSERT(gUartBHandle >= 0);
    transportHandleB.uart = gUartBHandle;

    U_TEST_PRINT_LINE("adding a GNSS instance on UART %d...", U_CFG_TEST_UART_B);
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         U_GNSS_TRANSPORT_UART,
                         transportHandleB,
                         -1, false, &gnssHandleB);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);
    transportType = U_GNSS_TRANSPORT_NONE;
    transportHandle.uart = -1;
    U_PORT_TEST_ASSERT(uGnssGetTransportHandle(gnssHandleB,
                                               &transportType,
                                               &transportHandle) == 0);
    U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART);
    U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleB.uart);
    if (printUbxMessagesDefault) {
        U_PORT_TEST_ASSERT(uGnssGetUbxMessagePrint(gnssHandleB));
    } else {
        U_PORT_TEST_ASSERT(!uGnssGetUbxMessagePrint(gnssHandleB));
    }

    U_TEST_PRINT_LINE("adding another instance on the same UART, should fail...");
    U_PORT_TEST_ASSERT(uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                                U_GNSS_TRANSPORT_UART,
                                transportHandleB, -1, false,
                                &dummyHandle) < 0);

    // Don't remove this one, let uGnssDeinit() do it
# endif

    U_TEST_PRINT_LINE("removing first GNSS instance...");
    uGnssRemove(gnssHandleA);

    U_TEST_PRINT_LINE("adding it again...");
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         gTransportTypeA,
                         transportHandleA,
                         -1, false, &gnssHandleA);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);
    transportType = U_GNSS_TRANSPORT_NONE;
    transportHandle.uart = -1;
    transportHandle.i2c = -1;
    U_PORT_TEST_ASSERT(uGnssGetTransportHandle(gnssHandleA,
                                               &transportType,
                                               &transportHandle) == 0);
    switch (gTransportTypeA) {
        case U_GNSS_TRANSPORT_UART:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART);
            U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleA.uart);
#if defined(_WIN32) || (defined(__ZEPHYR__) && defined(CONFIG_UART_NATIVE_POSIX))
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_USB);
#else
# ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_UART1);
# else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
# endif
#endif
            break;
        case U_GNSS_TRANSPORT_UART_2:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_UART_2);
            U_PORT_TEST_ASSERT(transportHandle.uart == transportHandleA.uart);
#if defined(_WIN32) || (defined(__ZEPHYR__) && defined(CONFIG_UART_NATIVE_POSIX))
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_USB);
#else
# ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_UART2);
# else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
# endif
#endif
            break;
        case U_GNSS_TRANSPORT_I2C:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_I2C);
            U_PORT_TEST_ASSERT(transportHandle.i2c == transportHandleA.i2c);
#ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_I2C);
#else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
#endif
            break;
        case U_GNSS_TRANSPORT_SPI:
            U_PORT_TEST_ASSERT(transportType == U_GNSS_TRANSPORT_SPI);
            U_PORT_TEST_ASSERT(transportHandle.spi == transportHandleA.spi);
#ifndef U_CFG_GNSS_PORT_NUMBER
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_GNSS_PORT_SPI);
#else
            U_PORT_TEST_ASSERT(uGnssGetPortNumber(gnssHandleA) == U_CFG_GNSS_PORT_NUMBER);
#endif
            break;
        default:
            U_PORT_TEST_ASSERT(false);
            break;
    }

    U_TEST_PRINT_LINE("deinitialising GNSS API...");
    uGnssDeinit();

    U_TEST_PRINT_LINE("removing stream...");
    switch (gTransportTypeA) {
        case U_GNSS_TRANSPORT_UART:
            uPortUartClose(gStreamAHandle);
            break;
        case U_GNSS_TRANSPORT_UART_2:
            uPortUartClose(gStreamAHandle);
            break;
        case U_GNSS_TRANSPORT_I2C:
            uPortI2cClose(gStreamAHandle);
            break;
        case U_GNSS_TRANSPORT_SPI:
            uPortSpiClose(gStreamAHandle);
            break;
        default:
            break;
    }
    gStreamAHandle = -1;

# if (U_CFG_TEST_UART_B >= 0)
    uPortUartClose(gUartBHandle);
    gUartBHandle = -1;
# endif

    uPortSpiDeinit();
    uPortI2cDeinit();
    uPortDeinit();

    // Check for resource leaks
    uTestUtilResourceCheck(U_TEST_PREFIX, NULL, true);
    resourceCount = uTestUtilGetDynamicResourceCount() - resourceCount;
    U_TEST_PRINT_LINE("we have leaked %d resources(s).", resourceCount);
    U_PORT_TEST_ASSERT(resourceCount <= 0);
}
#endif

#if (U_CFG_APP_GNSS_I2C >= 0) && defined(U_GNSS_TEST_I2C_ADDRESS_EXTRA)
/** Test using an alternate I2C address.
 */
U_PORT_TEST_FUNCTION("[gnss]", "gnssI2cAddress")
{
    uGnssTransportHandle_t transportHandle;
    uDeviceHandle_t gnssHandle[2];
    char *buffer[2];
    int32_t size[2];
    char *pTmp;
    int32_t y;
    int32_t errorCode;
    int32_t resourceCount;

    // Whatever called us likely initialised the
    // port so deinitialise it here to obtain the
    // correct initial heap size
    uPortDeinit();
    resourceCount = uTestUtilGetDynamicResourceCount();

    U_PORT_TEST_ASSERT(uPortInit() == 0);
    U_PORT_TEST_ASSERT(uPortI2cInit() == 0);

    U_TEST_PRINT_LINE("testing using an alternate I2C address (0x%02x).",
                      U_GNSS_TEST_I2C_ADDRESS_EXTRA);
    gStreamAHandle = uPortI2cOpen(U_CFG_APP_GNSS_I2C,
                                  U_CFG_APP_PIN_GNSS_SDA,
                                  U_CFG_APP_PIN_GNSS_SCL,
                                  true);
    U_PORT_TEST_ASSERT(gStreamAHandle >= 0);
    gTransportTypeA = U_GNSS_TRANSPORT_I2C;
    transportHandle.i2c = gStreamAHandle;

    U_PORT_TEST_ASSERT(uGnssInit() == 0);

    U_TEST_PRINT_LINE("adding a first GNSS instance on I2C port %d,"
                      " I2C address 0x%02x...", U_CFG_APP_GNSS_I2C,
                      U_GNSS_I2C_ADDRESS);
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         U_GNSS_TRANSPORT_I2C, transportHandle,
                         -1, false, &gnssHandle[0]);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);

    uGnssSetUbxMessagePrint(gnssHandle[0], true);
    U_PORT_TEST_ASSERT(uGnssGetI2cAddress(gnssHandle[0]) == U_GNSS_I2C_ADDRESS);

    // Power-up the first device
    U_TEST_PRINT_LINE("powering on first GNSS device at I2C address 0x%02x...", U_GNSS_I2C_ADDRESS);
    U_PORT_TEST_ASSERT(uGnssPwrOn(gnssHandle[0]) == 0);

    U_TEST_PRINT_LINE("adding a second GNSS instance at I2C address 0x%02x...",
                      U_GNSS_TEST_I2C_ADDRESS_EXTRA);
    errorCode = uGnssAdd(U_GNSS_MODULE_TYPE_M8,
                         U_GNSS_TRANSPORT_I2C, transportHandle,
                         -1, false, &gnssHandle[1]);
    U_PORT_TEST_ASSERT_EQUAL((int32_t) U_ERROR_COMMON_SUCCESS, errorCode);

    uGnssSetUbxMessagePrint(gnssHandle[1], true);

    // Get/set the I2C address
    U_PORT_TEST_ASSERT(uGnssGetI2cAddress(gnssHandle[1]) == U_GNSS_I2C_ADDRESS);
    U_PORT_TEST_ASSERT(uGnssSetI2cAddress(gnssHandle[1], U_GNSS_TEST_I2C_ADDRESS_EXTRA) == 0);
    U_PORT_TEST_ASSERT(uGnssGetI2cAddress(gnssHandle[1]) == U_GNSS_TEST_I2C_ADDRESS_EXTRA);

    // Now power the second device up
    U_TEST_PRINT_LINE("powering on second GNSS device at I2C address 0x%02x...",
                      U_GNSS_TEST_I2C_ADDRESS_EXTRA);
    U_PORT_TEST_ASSERT(uGnssPwrOn(gnssHandle[1]) == 0);

    U_TEST_PRINT_LINE("making sure the version strings are different...");
    // Get the firmware version strings of both and diff them, just to
    // make sure we are talking to different chips
    buffer[0] = (char *) pUPortMalloc(U_GNSS_TEST_BUFFER_SIZE_BYTES);
    buffer[1] = (char *) pUPortMalloc(U_GNSS_TEST_BUFFER_SIZE_BYTES);
    U_PORT_TEST_ASSERT(buffer[0] != NULL);
    U_PORT_TEST_ASSERT(buffer[1] != NULL);
    size[0] = uGnssInfoGetFirmwareVersionStr(gnssHandle[0], buffer[0], U_GNSS_TEST_BUFFER_SIZE_BYTES);
    size[1] = uGnssInfoGetFirmwareVersionStr(gnssHandle[1], buffer[1], U_GNSS_TEST_BUFFER_SIZE_BYTES);
    U_PORT_TEST_ASSERT(size[0] > 0);
    U_PORT_TEST_ASSERT(size[1] > 0);
    for (size_t x = 0; x < sizeof(buffer) / sizeof(buffer[0]); x++) {
        U_TEST_PRINT_LINE("GNSS chip %d version string is:", x + 1);
        pTmp = buffer[x];
        while (pTmp < buffer[x] + size[x]) {
            y = strlen(pTmp);
            if (y > 0) {
                U_TEST_PRINT_LINE("\"%s\".", pTmp);
                pTmp += y;
            } else {
                pTmp++;
            }
        }
    }
    y = size[0];
    if (y > size[1]) {
        y = size[1];
    }
    U_PORT_TEST_ASSERT(memcmp(buffer[0], buffer[1], y) != 0);

    U_TEST_PRINT_LINE("powering off both GNSS chips...");
    U_PORT_TEST_ASSERT(uGnssPwrOff(gnssHandle[1]) == 0);
    U_PORT_TEST_ASSERT(uGnssPwrOff(gnssHandle[0]) == 0);

    // Free memory
    uPortFree(buffer[0]);
    uPortFree(buffer[1]);

    // Check that we haven't dropped any incoming data
    y = uGnssMsgReceiveStatStreamLoss(gnssHandle);
    U_TEST_PRINT_LINE("%d byte(s) lost at the input to the ring-buffer during that test.", y);
    U_PORT_TEST_ASSERT(y == 0);

    U_TEST_PRINT_LINE("deinitialising GNSS API...");
    uGnssDeinit();

    U_TEST_PRINT_LINE("removing stream...");
    uPortI2cClose(gStreamAHandle);
    gStreamAHandle = -1;

    uPortI2cDeinit();
    uPortDeinit();

    // Check for resource leaks
    uTestUtilResourceCheck(U_TEST_PREFIX, NULL, true);
    resourceCount = uTestUtilGetDynamicResourceCount() - resourceCount;
    U_TEST_PRINT_LINE("we have leaked %d resources(s).", resourceCount);
    U_PORT_TEST_ASSERT(resourceCount <= 0);
}
#endif

/** Clean-up to be run at the end of this round of tests, just
 * in case there were test failures which would have resulted
 * in the deinitialisation being skipped.
 */
U_PORT_TEST_FUNCTION("[gnss]", "gnssCleanUp")
{
    uGnssDeinit();
    if (gStreamAHandle >= 0) {
        switch (gTransportTypeA) {
            case U_GNSS_TRANSPORT_UART:
                uPortUartClose(gStreamAHandle);
                break;
            case U_GNSS_TRANSPORT_I2C:
                uPortI2cClose(gStreamAHandle);
                break;
            case U_GNSS_TRANSPORT_SPI:
                uPortSpiClose(gStreamAHandle);
                break;
            default:
                break;
        }
    }
    if (gUartBHandle >= 0) {
        uPortUartClose(gUartBHandle);
    }

    uPortSpiDeinit();
    uPortI2cDeinit();
    uPortDeinit();
    // Printed for information: asserting happens in the postamble
    uTestUtilResourceCheck(U_TEST_PREFIX, NULL, true);
}

// End of file
