/***************************************************************************//**
* \file simple_queue.h
* \version 1.0
*
* \brief    Defines the messages and constants used in the Ring queue
*           implementation for no-RTOS environment
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
#ifndef SIMPLE_QUEUE_H
#define SIMPLE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

/* Global variables */
typedef void* sq_handle_t;

/* Macros */
#define MAX_SIMPLE_QUEUES 8

/* Function prototypes */
/** Create a queue with item size and depth. Returns handle or NULL. */
sq_handle_t sq_create(uint32_t itemSize, uint32_t depth);

/** Delete a queue. */
void sq_delete(sq_handle_t queue);

/** Send item from ISR context. Returns true if enqueued. */
bool sq_send_from_isr(sq_handle_t queue, const void *item);

/** Send item. Returns true if enqueued. */
bool sq_send(sq_handle_t queue, const void *item);

/** Receive item with simple polling. Returns true if received. */
bool sq_receive(sq_handle_t queue, void *item);

/** Check how many messages are waiting. */
uint32_t sq_messages_waiting(sq_handle_t queue);

#endif /* SIMPLE_QUEUE_H */
