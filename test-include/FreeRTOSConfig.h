/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.  See
 * https://www.FreeRTOS.org/a00110.html
 *----------------------------------------------------------*/

#define configUSE_PREEMPTION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_16_BIT_TICKS 0
#define configTICK_RATE_HZ (1000)
#define configMINIMAL_STACK_SIZE (PTHREAD_STACK_MIN)
#define configTOTAL_HEAP_SIZE ((size_t) (16 * 1024 * 1024))
#define configMAX_TASK_NAME_LEN (32)
#define configCHECK_FOR_STACK_OVERFLOW 1
#define configIDLE_SHOULD_YIELD 1

/* The following 2  memory allocation schemes are possible for this demo:
 *
 * 1. Dynamic Only.
 *    #define configSUPPORT_STATIC_ALLOCATION  0
 *    #define configSUPPORT_DYNAMIC_ALLOCATION 1
 *
 * 2. Static and Dynamic.
 *    #define configSUPPORT_STATIC_ALLOCATION  1
 *    #define configSUPPORT_DYNAMIC_ALLOCATION 1
 *
 * Static only configuration is not possible for this demo as it utilizes
 * dynamic allocation.
 */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1

#define configRECORD_STACK_HIGH_ADDRESS 1

#define configMAX_PRIORITIES (7)

#define INCLUDE_vTaskDelete 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskAbortDelay 1
#define INCLUDE_vTaskSuspend 1

extern void vAssertCalled(const char* const pcFileName,
    unsigned long ulLine);

/* It is a good idea to define configASSERT() while developing.  configASSERT()
 * uses the same semantics as the standard C assert() macro.  Don't define
 * configASSERT() when performing code coverage tests though, as it is not
 * intended to asserts() to fail, some some code is intended not to run if no
 * errors are present. */
#define configASSERT(x) \
    if ((x) == 0)       \
    vAssertCalled(__FILE__, __LINE__)

/* Prototype for the function used to print out.  In this case it prints to the
 * console before the network is connected then a UDP port after the network has
 * connected. */
extern void vLoggingPrintf(const char* pcFormatString,
    ...);

/* Set to 1 to print out debug messages.  If ipconfigHAS_DEBUG_PRINTF is set to
 * 1 then FreeRTOS_debug_printf should be defined to the function used to print
 * out the debugging messages. */
#define ipconfigHAS_DEBUG_PRINTF 1
#if (ipconfigHAS_DEBUG_PRINTF == 1)
#define FreeRTOS_debug_printf(X) vLoggingPrintf X
#endif

/* Set to 1 to print out non debugging messages, for example the output of the
 * FreeRTOS_netstat() command, and ping replies.  If ipconfigHAS_PRINTF is set to 1
 * then FreeRTOS_printf should be set to the function used to print out the
 * messages. */
#define ipconfigHAS_PRINTF 1
#if (ipconfigHAS_PRINTF == 1)
#define FreeRTOS_printf(X) vLoggingPrintf X
#endif
#endif /* FREERTOS_CONFIG_H */
