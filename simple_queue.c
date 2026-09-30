/***************************************************************************//**
* \file simple_queue.c
* \version 1.0
*
* \brief Implements simple queue.
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

#include "simple_queue.h"
#include <stdlib.h>
#include <string.h>
#include "cy_syslib.h"

typedef struct {
    uint32_t itemSize;
    uint32_t depth;
    uint8_t *buf;           /* storage: depth * itemSize */
    volatile uint32_t head; /* next write index */
    volatile uint32_t tail; /* next read index */
} simplequeue_t;

static simplequeue_t queues[MAX_SIMPLE_QUEUES];
static bool used[MAX_SIMPLE_QUEUES];

sq_handle_t sq_create(uint32_t itemSize, uint32_t depth)
{
    for (int i = 0; i < MAX_SIMPLE_QUEUES; ++i) 
    {
        if (!used[i]) 
        {
            used[i] = true;
            queues[i].itemSize = itemSize;
            queues[i].depth = depth;
            queues[i].buf = (uint8_t*)malloc(itemSize * depth);
            if (queues[i].buf == NULL) 
            {
                used[i] = false;
                return NULL;
            }
            queues[i].head = 0;
            queues[i].tail = 0;
            return (sq_handle_t)&queues[i];
        }
    }
    return NULL;
}

void sq_delete(sq_handle_t queue)
{
    if (queue == NULL) return;
    simplequeue_t *s = (simplequeue_t*)queue;
    for (int i = 0; i < MAX_SIMPLE_QUEUES; ++i) 
    {
        if (&queues[i] == s) {
            free(queues[i].buf);
            queues[i].buf = NULL;
            used[i] = false;
            return;
        }
    }
}

static inline uint32_t next_idx(uint32_t idx, uint32_t depth) 
{
    return (idx + 1) % depth;
}

bool sq_send_from_isr(sq_handle_t queue, const void *item)
{
    if (queue == NULL || item == NULL) return false;
    simplequeue_t *s = (simplequeue_t*)queue;
    uint32_t head = s->head;
    uint32_t next = next_idx(head, s->depth);
    if (next == s->tail) {
        /* queue full */
        return false;
    }
    uint8_t *dst = s->buf + (head * s->itemSize);
    memcpy(dst, item, s->itemSize);

    s->head = next;
    return true;
}

bool sq_send(sq_handle_t queue, const void *item)
{
    uint32_t primask = Cy_SysLib_EnterCriticalSection();
    bool res = sq_send_from_isr(queue, item);
    Cy_SysLib_ExitCriticalSection(primask);
    return res;
}

bool sq_receive(sq_handle_t queue, void *item)
{
    if (queue == NULL || item == NULL) 
    {
        return false;
    }

    simplequeue_t *s = (simplequeue_t*)queue;
    if (s->tail == s->head)
    {
        return false; /* empty */
    }

    uint8_t *src = s->buf + (s->tail * s->itemSize);
    memcpy(item, src, s->itemSize);
    s->tail = next_idx(s->tail, s->depth);
    return true;
}

uint32_t sq_messages_waiting(sq_handle_t queue)
{
    if (queue == NULL) return 0;
    simplequeue_t *s = (simplequeue_t*)queue;
    uint32_t head = s->head;
    uint32_t tail = s->tail;
    if (head >= tail) return head - tail;
    return s->depth - (tail - head);
}

/*[]*/
