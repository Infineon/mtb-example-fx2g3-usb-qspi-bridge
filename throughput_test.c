/***************************************************************************//**
* \file throughput_test.c
* \version 1.0
*
* \brief Implements QSPI throughput test
*
*******************************************************************************
* \copyright
* (c) (2026), Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.
*
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "throughput_test.h"
#include "cy_usb_usbd.h"
#include "cy_debug.h"
#include "qspi.h"
#include <string.h>

#if THROUGHPUT_TEST_ENABLE

void Run_Smif_Throughput_Measure(uint32_t total_bytes, uint32_t chunk_size)
{
    const uint32_t TOTAL_BYTES = (total_bytes == 0) ? THROUGHPUT_TEST_TOTAL_BYTES : total_bytes;
    const uint32_t CHUNK_SIZE = (chunk_size == 0) ? THROUGHPUT_TEST_CHUNK_SIZE : chunk_size;
    static uint8_t test_buf[THROUGHPUT_TEST_CHUNK_SIZE];
    // cy_en_smif_status_t st = CY_SMIF_SUCCESS;
    uint32_t transferred = 0;
    uint32_t t0, t1;

    Cy_Debug_AddToLog(1, "ThroughputTest: start total=%u chunk=%u\r\n", (uint32_t)TOTAL_BYTES, (uint32_t)CHUNK_SIZE);
    for (uint32_t i = 0; i < CHUNK_SIZE; ++i) test_buf[i] = (uint8_t)(i & 0xFF);

    /* Write phase */
    transferred = 0;
    t0 = Cy_USBD_GetTimerTick();
    while (transferred < TOTAL_BYTES) {
        uint32_t toWrite = CHUNK_SIZE;
        if ((TOTAL_BYTES - transferred) < toWrite)
            toWrite = (TOTAL_BYTES - transferred);
        bool status = QspiTransmitData(test_buf, toWrite);
        if(status == false) {
            DBG_APP_ERR("ThroughputTest: write failed at %u\r\n", (uint32_t)transferred);
            break;
        }
        transferred += toWrite;
    }
    t1 = Cy_USBD_GetTimerTick();
    if (t1 <= t0) t1 = t0 + 1;
    {
        uint32_t ms = (uint32_t)(t1 - t0);
        /* Compute MB/s (MB=1024*1024)*/
            uint32_t mb_x100 = 0u;
            if (ms > 0u) {
                    /* mb_x100 = ((transferred bytes) * 100000) / (1024*1024*ms) but rearranged to avoid overflow */
                    uint64_t tmp = ((uint64_t)transferred * 100000ULL);
                    tmp /= ((uint64_t)1024U * 1024U * (uint64_t)ms);
                    mb_x100 = (uint32_t)tmp;
            }
            Cy_Debug_AddToLog(1, "WRITE: bytes=%u time=%u ms -> %u.%u MB/s\r\n", (unsigned int)transferred, (unsigned int)ms, (unsigned int)(mb_x100 / 100u), (unsigned int)(mb_x100 % 100u));
        }

    /* Read phase */
    transferred = 0;
    t0 = Cy_USBD_GetTimerTick();
    while (transferred < TOTAL_BYTES) {
        uint32_t toRead = CHUNK_SIZE;
        if ((TOTAL_BYTES - transferred) < toRead) toRead = (TOTAL_BYTES - transferred);
        bool status = QspiReceiveData(test_buf, toRead);
        if(status == false) {
            DBG_APP_ERR("ThroughputTest: read failed at %u\r\n", (uint32_t)transferred);
            break;
        }
        transferred += toRead;
    }
    t1 = Cy_USBD_GetTimerTick();
    if (t1 <= t0) t1 = t0 + 1;
    {
        uint32_t ms = (uint32_t)(t1 - t0);
        uint32_t mb_x100 = 0u;
        if (ms > 0u) {
            uint64_t tmp = ((uint64_t)transferred * 100000ULL);
            tmp /= ((uint64_t)1024U * 1024U * (uint64_t)ms);
            mb_x100 = (uint32_t)tmp;
        }
        Cy_Debug_AddToLog(1, "READ: bytes=%u time=%u ms -> %u.%u MB/s\r\n", (unsigned int)transferred, (unsigned int)ms, (unsigned int)(mb_x100 / 100u), (unsigned int)(mb_x100 % 100u));
    }

    DBG_APP_INFO("ThroughputTest: complete\r\n");
}
#else
/* no operation when disabled */
void Run_Smif_Throughput_Measure(uint32_t total_bytes, uint32_t chunk_size)
{
    (void)total_bytes;
    (void)chunk_size;
	Cy_Debug_AddToLog(1, "ThroughputTest: Macro is disabled\r\n");
}

#endif /* THROUGHPUT_TEST_ENABLE */
