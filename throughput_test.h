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

#ifndef THROUGHPUT_TEST_H
#define THROUGHPUT_TEST_H

#include <stdint.h>
#ifndef THROUGHPUT_TEST_ENABLE
/* Enable throughput test by default so vendor command 0xB5 runs it. */
#define THROUGHPUT_TEST_ENABLE 1
#endif

/* Default test parameters - overridable via Makefile */
#ifndef THROUGHPUT_TEST_TOTAL_BYTES
#define THROUGHPUT_TEST_TOTAL_BYTES (50U * 1024U * 1024U)
#endif

#ifndef THROUGHPUT_TEST_CHUNK_SIZE
#define THROUGHPUT_TEST_CHUNK_SIZE (16U * 1024U)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Run SMIF throughput test. 'total_bytes' and 'chunk_size' override defaults when non-zero. */
void Run_Smif_Throughput_Measure(uint32_t total_bytes, uint32_t chunk_size);

#ifdef __cplusplus
}
#endif

#endif /* THROUGHPUT_TEST_H */
