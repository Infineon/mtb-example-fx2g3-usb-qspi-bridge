/***************************************************************************//**
* \file usb_bridge_device.c
* \version 1.0
*
* Implements USB to QSPI bridge with dedicated tasks
* for optimal throughput.
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

#include "cy_hbdma_mgr.h"
#include "cy_pdl.h"
#include "cy_usb_common.h"
#include "cy_usbhs_cal_drv.h"
#include "cy_usb_usbd.h"
#include "usb_bridge_device.h"
#include "usb_app.h"
#include "cy_fault_handlers.h"
#include "cy_debug.h"
#include "qspi.h"
#include "throughput_test.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* SystemCoreClock is provided by CMSIS/system files; used to convert DWT cycles to microseconds */
//extern uint32_t SystemCoreClock;

#if PRIME_ENABLE
#define WRITE_PRIME_ENABLE 1
#define READ_PRIME_ENABLE 1
#else
#define WRITE_PRIME_ENABLE 0
#define READ_PRIME_ENABLE 0
#endif

extern bool Cy_Bridge_HbDmaInit(void);
extern cy_stc_hbdma_mgr_context_t HBW_MgrCtxt;

extern bool Cy_USB_ConnectionEnable (cy_stc_usb_app_ctxt_t *pAppCtxt);
extern void OutEpDma_ISR(uint8_t endpNumber);
extern void InEpDma_ISR(uint8_t endpNumber);
extern cy_stc_hbdma_buf_mgr_t HBW_BufMgr;
cy_israddress GetEPInDmaIsr(uint8_t epNum);
cy_israddress GetEPOutDmaIsr(uint8_t epNum);

extern volatile uint16_t pktType;
extern volatile uint16_t pktLength;
extern uint32_t Ep0TestBuffer[1024U];
static volatile uint8_t run_smif_throughput_request = 0;

extern void Run_Smif_Throughput_Measure(uint32_t total_bytes, uint32_t chunk_size);

#if !READ_PRIME_ENABLE
/* Deferred buffers for non-primed START_READ (ISR-safe ring) */
#define READ_DEFER_RING_SIZE 16
/* Initial seed count when starting deferred reads */
#define READ_DEFER_SEED_COUNT 6
static volatile uint8_t read_defer_head = 0;
static volatile uint8_t read_defer_tail = 0;
static cy_stc_hbdma_buff_status_t read_defer_bufs[READ_DEFER_RING_SIZE];
/* Counters for deferred read ring */
static volatile uint32_t read_defer_seed_attempts = 0;
static volatile uint32_t read_defer_seed_success = 0;
static volatile uint32_t read_defer_overflow = 0;
static volatile uint32_t read_defer_commits = 0;
static volatile uint32_t read_defer_commit_fail = 0;
#endif

#define WRITE_DEFER_RING_SIZE 256
/* Max entries to enqueue in one ISR event */
#define WRITE_DEFER_SEED_COUNT 6

/* Ring buffer storage and counters for non-primed mode */
#if !WRITE_PRIME_ENABLE
static volatile uint8_t write_defer_head = 0;
static volatile uint8_t write_defer_tail = 0;
static cy_stc_hbdma_buff_status_t write_defer_bufs[WRITE_DEFER_RING_SIZE];
/* Counters for deferred write ring */
static  volatile uint32_t write_defer_enqueue_attempts = 0;
static  volatile uint32_t write_defer_enqueue_success = 0;
static  volatile uint32_t write_defer_overflow = 0;
static  volatile uint32_t write_defer_transmits = 0;
static  volatile uint32_t write_defer_transmit_fail = 0;
/* Count of deferred write descriptors flushed on STOP */
static  volatile uint32_t write_defer_flushed = 0;
static volatile bool write_reset_pending = false;
#endif /* !WRITE_PRIME_ENABLE */

/* Cumulative QSPI transmit counters (ms and bytes) */
static volatile uint32_t qspi_write_calls = 0;
static volatile uint32_t qspi_write_ms_total = 0;
static volatile uint32_t qspi_write_bytes_total = 0;

/* Min/max latency (ms) and latencies. */
static volatile uint32_t qspi_write_ms_min = 0xFFFFFFFFu;
static volatile uint32_t qspi_write_ms_max = 0u;

/* Read-side millisecond counters */
static volatile uint32_t qspi_read_calls = 0;
static volatile uint32_t qspi_read_ms_total = 0;
static volatile uint32_t qspi_read_bytes_total = 0;
static volatile uint32_t qspi_read_ms_min = 0xFFFFFFFFu;
static volatile uint32_t qspi_read_ms_max = 0u;

/* Main-loop drain timing (ms) */
static  volatile uint32_t write_drain_calls = 0;
static  volatile uint32_t write_drain_ms_total = 0;
static  volatile uint32_t write_drain_last_ms = 0;

/* Priming counters */
static  volatile uint32_t read_prime_committed = 0;
static  volatile uint32_t write_prime_transmits = 0;

#if WRITE_PRIME_ENABLE
static volatile uint32_t write_prime_transmit_fail = 0;
#endif

#if (WRITE_PRIME_ENABLE || READ_PRIME_ENABLE)
/* HB-DMA manager status to string for log */
#if defined(__GNUC__)
static const char *HbdmaMgrStatusStr(cy_en_hbdma_mgr_status_t s) __attribute__((unused));
#else
static const char *HbdmaMgrStatusStr(cy_en_hbdma_mgr_status_t s);
#endif

static const char *HbdmaMgrStatusStr(cy_en_hbdma_mgr_status_t s)
{
    switch (s) {
        case CY_HBDMA_MGR_SUCCESS: return "SUCCESS";
        case CY_HBDMA_MGR_BAD_PARAM: return "BAD_PARAM";
        case CY_HBDMA_MGR_TIMEOUT: return "TIMEOUT";
        case CY_HBDMA_MGR_DRV_HW_ERROR: return "DRV_HW_ERROR";
        case CY_HBDMA_MGR_MEMORY_ERROR: return "MEMORY_ERROR";
        case CY_HBDMA_MGR_SEQUENCE_ERROR: return "SEQUENCE_ERROR";
        case CY_HBDMA_MGR_MEM_CORRUPTION: return "MEM_CORRUPTION";
        case CY_HBDMA_MGR_SOCK_BUSY: return "SOCK_BUSY";
        case CY_HBDMA_MGR_NOT_SUPPORTED: return "NOT_SUPPORTED";
        default: return "UNKNOWN_STATUS";
    }
}
#endif

/* Look-up table to set PLL parameters to get different clock frequencies. */
const uint8_t ClockParams[40][4] = {
{ 1,  1,  1, 1},         /*  0 MHz: invalid */
{ 1,  1,  1, 1},         /*  1 MHz: invalid */
{ 1,  1,  1, 1},         /*  2 MHz: invalid */
{ 1,  1,  1, 1},         /*  3 MHz: invalid */
{ 1,  1,  1, 1},         /*  4 MHz: invalid */
{ 1,  1,  1, 1},         /*  5 MHz: invalid */
{ 1,  1,  1, 1},         /*  6 MHz: invalid */
{ 1,  1,  1, 1},         /*  7 MHz: invalid */
{ 1,  1,  1, 1},         /*  8 MHz: invalid */
{ 1,  1,  1, 1},         /*  9 MHz: invalid */
{ 1, 1,  1, 1},        /* 10 MHz: invalid */
{ 1, 16, 22, 1},        /* 11 MHz */
{ 1, 16, 24, 1},        /* 12 MHz */
{ 1, 16, 26, 0},        /* 13 MHz */
{ 1, 16, 28, 0},        /* 14 MHz */
{ 1, 16, 30, 0},        /* 15 MHz */
{ 1, 16, 32, 0},        /* 16 MHz */
{ 1, 16, 34, 0},        /* 17 MHz */
{ 1, 16, 36, 0},        /* 18 MHz */
{ 1, 16, 38, 0},        /* 19 MHz */
{ 1, 16, 40, 0},        /* 20 MHz */
{ 1, 16, 42, 0},        /* 21 MHz */
{ 2, 8, 44, 1},        /* 22 MHz */
{ 2, 8, 46, 1},        /* 23 MHz */
{ 2, 8, 48, 1},        /* 24 MHz */
{ 2, 8, 50, 0},        /* 25 MHz */
{ 2, 8, 52, 0},        /* 26 MHz */
{ 2, 8, 54, 0},        /* 27 MHz */
{ 2, 8, 56, 0},        /* 28 MHz */
{ 2, 8, 58, 0},        /* 29 MHz */
{ 2, 8, 60, 0},        /* 30 MHz */
{ 2, 8, 62, 0},        /* 31 MHz */
{ 2, 8, 64, 0},         /* 32 MHz */
{ 2, 8, 66, 0},         /* 33 MHz */
{ 2, 8, 68, 0},         /* 34 MHz */
{ 2, 8, 70, 0},         /* 35 MHz */
{ 2, 8, 72, 0},         /* 36 MHz */
{ 2, 8, 74, 0},         /* 37 MHz */
{ 2, 8, 76, 0},         /* 38 MHz */
{ 2, 8, 78, 0}          /* 39 MHz */
};

/**
 * \name QspiBridgeSetClockFrequency
 * \brief Set QSPI clock frequency by configuring PLL1 and routing to SMIF.
 * \param freq_mhz Desired frequency in MHz (valid range: 11 to 39)
 * \retval None
 */
void
QspiBridgeSetClockFrequency (
        uint8_t freq_mhz)
{
    cy_stc_pll_manual_config_t pll_cfg;
    cy_en_sysclk_status_t pll_stat;

    /* Supported frequency range is 11 MHz to 39 MHz. */
    if ((freq_mhz > 10) && (freq_mhz < 40)) {

        Cy_Debug_AddToLog(3, "Setting freq to %dMHz\r\n", freq_mhz);

        /* Disable PLL1. */
        Cy_SysClk_PllDisable(2);

        /*
         * Final frequency = (8 MHz (IMO) * feedbackDiv) / (referenceDiv * outputDiv)
         * lfMode = !((frequency & outputDiv) >= 200)
         */
        pll_cfg.referenceDiv = ClockParams[freq_mhz][0];
        pll_cfg.outputDiv    = ClockParams[freq_mhz][1];
        pll_cfg.feedbackDiv  = ClockParams[freq_mhz][2];
        pll_cfg.lfMode       = ClockParams[freq_mhz][3];
        pll_cfg.outputMode   = CY_SYSCLK_FLLPLL_OUTPUT_AUTO;

        /* Configure PLL1 for the desired frequency and wait for it to lock. */
        pll_stat = Cy_SysClk_PllManualConfigure(2, &pll_cfg);
        Cy_SysClk_PllEnable(2, 0);
        while (!Cy_SysClk_PllLocked(2));

        DBG_APP_INFO("PLL params: %d %d %d stat:%x %x %x\r\n",
                pll_cfg.referenceDiv, pll_cfg.outputDiv, pll_cfg.feedbackDiv,
                pll_stat, SRSS->CLK_PLL_CONFIG[1], SRSS->CLK_PLL_STATUS[1]);
        
        /* Route CLK_HF[1] to PATH2 (PLL1) and set divider to 1 for SMIF */
        Cy_SysClk_ClkHfSetSource(1U, CY_SYSCLK_CLKHF_IN_CLKPATH2);
        Cy_SysClk_ClkHfSetDivider(1U, CY_SYSCLK_CLKHF_NO_DIVIDE);
        
        /* Verify clock routing */
        DBG_APP_INFO("HF1 Clock: source=%u divider=%u (CLKHF1_CTL=0x%08x)\r\n",
                (unsigned int)Cy_SysClk_ClkHfGetSource(1U),
                (unsigned int)Cy_SysClk_ClkHfGetDivider(1U),
                (unsigned int)SRSS->CLK_ROOT_SELECT[1]);
    }
}


/* Print buffer-manager statistics: start address, region size, used/free blocks */
static void Print_HbDma_BufMgr_Stats(const cy_stc_hbdma_buf_mgr_t *mgr)
{
    if (mgr == NULL) {
        Cy_Debug_AddToLog(1, "HBDMA BufMgr: mgr=NULL\r\n");
        return;
    }

    Cy_Debug_AddToLog(1, "HBDMA BufMgr: start=0x%x regionSize=%u statusSizeWords=%u searchPos=%u\r\n",
                      (uint32_t)mgr->startAddr, (uint32_t)mgr->regionSize, (uint32_t)mgr->statusSize, (uint32_t)mgr->searchPos);

    if (mgr->usedStatus == NULL || mgr->statusSize == 0) {
        Cy_Debug_AddToLog(1, "HBDMA BufMgr: usedStatus=NULL or statusSize=0\r\n");
        return;
    }

    /* Count bits set in usedStatus array */
    uint32_t used = 0;
    for (uint32_t i = 0; i < mgr->statusSize; ++i) {
        uint32_t v = mgr->usedStatus[i];
        while (v) {
            v &= (v - 1U);
            ++used;
        }
    }

    uint32_t totalBlocks = mgr->statusSize * 32U;
    uint32_t freeBlocks = (used > totalBlocks) ? 0U : (totalBlocks - used);
    Cy_Debug_AddToLog(1, "HBDMA BufMgr: totalBlocks=%u used=%u free=%u\r\n", totalBlocks, used, freeBlocks);
}

/* Print HBDMA channel details */
static void Print_HbDma_Channel_State(const cy_stc_hbdma_channel_t *ch)
{
    if (ch == NULL) {
        Cy_Debug_AddToLog(1, "HBDMA Channel: handle=NULL\r\n");
        return;
    }
    Cy_Debug_AddToLog(1, "HBDMA Channel: state=%u count=%u size=%u endpAddr=%u\r\n",
                      (uint32_t)ch->state, (uint32_t)ch->count, (uint32_t)ch->size, (uint32_t)ch->endpAddr);

    Cy_Debug_AddToLog(1, "  firstProdDscr=%u firstConsDscr=%u curProdDscr=%u curConsDscr=%u nextProd=%u lastProd=%u\r\n",
                      (uint32_t)ch->firstProdDscrIndex[0], (uint32_t)ch->firstConsDscrIndex[0],
                      (uint32_t)ch->curProdDscrIndex[0], (uint32_t)ch->curConsDscrIndex[0],
                      (uint32_t)ch->nextProdDscr, (uint32_t)ch->lastProdDscr);
					  
    Cy_Debug_AddToLog(1, "  commitCnt=%u discardCnt=%u overrideCnt=%u pendingEvt=%u\r\n",
                      (uint32_t)ch->commitCnt[0], (uint32_t)ch->discardCnt[0], (uint32_t)ch->overrideCnt, (uint32_t)ch->pendingEvtCnt);
    
    Cy_Debug_AddToLog(1, "  pProdDwDscr=%x pConsDwDscr=%x pCurEgressBuf=%x\r\n",
                      (uint32_t)(uintptr_t)ch->pProdDwDscr[0], (uint32_t)(uintptr_t)ch->pConsDwDscr[0], (uint32_t)(uintptr_t)ch->pCurEgressDataBuf[0]);
}

/* Print STOP summary */
static void Print_Qspi_StopSummary(const char *label,
                                  unsigned int calls,
                                  unsigned int bytes,
                                  unsigned int ms_total,
                                  unsigned int min_ms,
                                  unsigned int max_ms)
{
    uint32_t mb_x100 = 0u;
    if (ms_total > 0u) 
    {
        uint64_t tmp = ((uint64_t)bytes * 100000ULL);
        tmp /= ((uint64_t)1024U * 1024U * (uint64_t)ms_total);
        mb_x100 = (uint32_t)tmp;
    }

    if (bytes > 0u && (bytes % 16384U) == 0u) 
    {
        unsigned int transfers = bytes / 16384U;
        Cy_Debug_AddToLog(1, "\r\n%s SUMMARY: transfers=%u\r\n", label, transfers);
        Cy_Debug_AddToLog(1, "bytes=%u = %u * 16384 \r\n", bytes, transfers);
    } 
    else 
    {
        Cy_Debug_AddToLog(1, "\r\n%s SUMMARY: transfers=%u\r\n", label, calls);
        Cy_Debug_AddToLog(1, "Bytes=%u\r\n", bytes);
    }
    Cy_Debug_AddToLog(1, "Total time=%u ms\r\n", ms_total);
    Cy_Debug_AddToLog(1, "Throughput=%u.%u MB/s\r\n", (unsigned int)(mb_x100 / 100u), (unsigned int)(mb_x100 % 100u));
    //Cy_Debug_AddToLog(1, "Latency: avg=%u.%u ms, min=%u ms, max=%u ms\r\n", (unsigned int)avg_ms_int, (unsigned int)avg_ms_frac, (unsigned int)min_ms, (unsigned int)max_ms);

    //Latency: coarse wall-clock per-call latencies (ms) using Cy_USBD_GetTimerTick
#if 0
    {
        uint32_t mb_x100 = 0u;
        if (ms_total > 0u) {
            uint64_t tmp = ((uint64_t)bytes * 100000ULL);
            tmp /= ((uint64_t)1024U * 1024U * (uint64_t)ms_total);
            mb_x100 = (uint32_t)tmp;
        }

        Cy_Debug_AddToLog(1, "STOP_%s QSPI stats: calls=%u total_ms=%u bytes=%u throughput=%u.%u MB/s avg=%u.%u ms min=%u ms max=%u ms\r\n",
                         label,
                         calls,
                         ms_total,
                         bytes,
                         (unsigned int)(mb_x100 / 100u), (unsigned int)(mb_x100 % 100u),
                         (unsigned int)(avg_ms_x100 / 100u), (unsigned int)(avg_ms_x100 % 100u),
                         (unsigned int)min_ms, (unsigned int)max_ms);
    }
#endif
}

/* Print WRITE STOP summary */
static void Print_Qspi_WriteStop(void)
{
    Print_Qspi_StopSummary("WRITE", (unsigned int)qspi_write_calls, (unsigned int)qspi_write_bytes_total,
                          (unsigned int)qspi_write_ms_total, (unsigned int)qspi_write_ms_min, (unsigned int)qspi_write_ms_max);

    /* Reset all the metrics */
    qspi_write_calls = 0;
    qspi_write_bytes_total = 0;
    qspi_write_ms_total = 0;
    qspi_write_ms_min = 0xFFFFFFFFu;
    qspi_write_ms_max = 0u;
    
}

static void Print_Qspi_ReadStop(void)
{
    Print_Qspi_StopSummary("READ", (unsigned int)qspi_read_calls, (unsigned int)qspi_read_bytes_total,
                          (unsigned int)qspi_read_ms_total, (unsigned int)qspi_read_ms_min, (unsigned int)qspi_read_ms_max);

    /* Reset all the metrics */
    qspi_read_calls = 0;
    qspi_read_bytes_total = 0;
    qspi_read_ms_total = 0;
    qspi_read_ms_min = 0xFFFFFFFFu;
    qspi_read_ms_max = 0u;
}


/* HB-DMA channel callback: route command/data and manage deferred rings */
void HbDma_Cb (cy_stc_hbdma_channel_t *handle,
                          cy_en_hbdma_cb_type_t type,
                          cy_stc_hbdma_buff_status_t *pbufStat,
                          void *userCtx)
{
    cy_en_hbdma_mgr_status_t   status;
    cy_en_hbdma_mgr_status_t   mgrStat = CY_HBDMA_MGR_SUCCESS;

    (void)mgrStat;
    cy_stc_hbdma_buff_status_t buffStat;
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)userCtx;
    uint8_t epNum = 0;
    bool isOut = false;

    for (epNum = 1; epNum < CY_USB_NUM_ENDP_CONFIGURED; epNum++)
    {
        if (pAppCtxt->pOutEpDma[epNum] == handle) {
            isOut = true;
            break;
        }
        if (pAppCtxt->pInEpDma[epNum] == handle) {
            isOut = false;
            break;
        }
    }

    /* This event means a buffer with data from the USB host is available. */
    if (type == CY_HBDMA_CB_PROD_EVENT)
    {
        status = Cy_HBDma_Channel_GetBuffer(handle, &buffStat);
        if (status != CY_HBDMA_MGR_SUCCESS)
        {
            DBG_APP_ERR("QSPI Bridge: HB-DMA GetBuffer Error\r\n");
            return;
        }

        /* If we couldn't find the endpoint, try to repair the mapping. */
        if (epNum >= CY_USB_NUM_ENDP_CONFIGURED)
        {
            uint8_t i;
            DBG_APP_WARN("HbDma_Cb: Unknown DMA handle %p - attempting to locate across endpIn/Out arrays\r\n", (void*)handle);
            for (i = 1; i < CY_USB_NUM_ENDP_CONFIGURED; i++) {
                if (&(pAppCtxt->endpOutDma[i].hbDmaChannel) == handle) {
                    pAppCtxt->pOutEpDma[i] = handle;
                    epNum = i;
                    isOut = true;
                    DBG_APP_INFO("HbDma_Cb: Repaired mapping: pOutEpDma[%d] -> %p\r\n", (int)i, (void*)handle);
                    break;
                }
            }

            if (epNum >= CY_USB_NUM_ENDP_CONFIGURED) {
                DBG_APP_WARN("HbDma_Cb: Still unknown after scan. Discarding.\r\n");
                Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);
                return;
            }
        }

        /* Process based on endpoint number for OUT endpoints:
         * EP1: Command endpoint 
         * EP2: Data write endpoint (USB OUT -> QSPI)
         */
        if (isOut && (epNum == 1) && buffStat.count > 0)
        {
            /* Command endpoint - process QSPI bridge commands */
            uint8_t *cmdData = buffStat.pBuffer;
            uint8_t command = cmdData[0];
            switch (command)
            {
#if !WRITE_PRIME_ENABLE
               case VENDOR_CMD_START_WRITE:
                    DBG_APP_INFO("CMD: START_WRITE (Streaming)\r\n");
                    if (pAppCtxt->streamState == STATE_IDLE)
                    {
                        write_reset_pending = false;
                        pAppCtxt->streamState = STATE_CONT_WRITE;
                        /* Enable QSPI write streaming mode */
                        Cy_QSPI_StartWriteStream();
                        if (pAppCtxt->pOutEpDma[2] != NULL) {
                            cy_en_hbdma_chn_state_t chState = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pOutEpDma[2]);
                            Cy_Debug_AddToLog(1, "StartWrite: OUT EP2 channel state=%u\r\n", (uint32_t)chState);
                            if (chState == CY_HBDMA_CHN_ACTIVE) {
                                /* Nothing to do */
                            } else {
                                /* Try to enable; if sequence error, reset and retry once */
                                cy_en_hbdma_mgr_status_t en = Cy_HBDma_Channel_Enable(pAppCtxt->pOutEpDma[2], 0);
                                if (en == CY_HBDMA_MGR_SEQUENCE_ERROR) {
                                    Cy_Debug_AddToLog(1, "StartWrite: ENABLE -> SEQUENCE_ERROR, resetting channel and retrying\r\n");
                                    Cy_HBDma_Channel_Reset(pAppCtxt->pOutEpDma[2]);
                                    Cy_SysLib_DelayUs(50);
                                    en = Cy_HBDma_Channel_Enable(pAppCtxt->pOutEpDma[2], 0);
                                }
                                Cy_Debug_AddToLog(1, "StartWrite: Channel_Enable returned %d\r\n", (int)en);
                            }
                        } else {
                            Cy_Debug_AddToLog(1, "StartWrite: OUT EP2 channel handle NULL\r\n");
                        }
                        DBG_APP_INFO("CMD: QSPI write streaming enabled - Ready for EP2 data\r\n");
                    }
                    break;
#else
                case VENDOR_CMD_START_WRITE:
                    DBG_APP_INFO("CMD: START_WRITE (Streaming)\r\n");
                    if (pAppCtxt->streamState == STATE_IDLE)
                    {
                        pAppCtxt->streamState = STATE_CONT_WRITE;
                        Cy_QSPI_StartWriteStream();
                        DBG_APP_INFO("CMD: QSPI write streaming enabled - Ready for EP2 data\r\n");

                        if (pAppCtxt->pOutEpDma[2] != NULL)
                        {
                            /* Ensure channel is enabled before priming */
                            Cy_Debug_AddToLog(1, "WritePrime: pOutEpDma[2]=%p, pHbDmaMgrCtxt=%p\r\n", (void*)pAppCtxt->pOutEpDma[2], (void*)pAppCtxt->pHbDmaMgrCtxt);
                            /* Check channel state first to avoid SEQUENCE_ERROR when already ACTIVE */
                            {
                                cy_en_hbdma_chn_state_t chState = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pOutEpDma[2]);
                                Cy_Debug_AddToLog(1, "WritePrime: channel state=%u\r\n", (uint32_t)chState);
                                if (chState == CY_HBDMA_CHN_ACTIVE) {
                                    /* Reset an already-ACTIVE channel before priming. Skipping Channel_Enable when ACTIVE. */
                                    Cy_Debug_AddToLog(1, "WritePrime: channel is ACTIVE - resetting before priming\r\n");
                                    Cy_HBDma_Channel_Reset(pAppCtxt->pOutEpDma[2]);
                                    Cy_SysLib_DelayUs(50);
                                }
                                mgrStat = Cy_HBDma_Channel_Enable(pAppCtxt->pOutEpDma[2], 0);
                                Cy_Debug_AddToLog(1, "WritePrime: Channel_Enable returned %s (%d)\r\n", HbdmaMgrStatusStr(mgrStat), (int)mgrStat);
                            }
                            if (mgrStat == CY_HBDMA_MGR_SEQUENCE_ERROR) 
							{
                                Cy_Debug_AddToLog(1, "WritePrime: SEQUENCE_ERROR - channel state details:\r\n");
                                Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pOutEpDma[2]);
                                if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr)
                                {
                                    Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                                }
                                else
                                {
                                    Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                                }
                            }
                            Cy_SysLib_DelayUs(50);
                            //Cy_Debug_AddToLog(1, "WritePrime: OUT EP is host-driven; channel enabled, no immediate GetBuffer priming required\r\n");
                        }
                        else
                        {
                            DBG_APP_ERR("CMD: EP2 OUT channel not available (NULL handle)\r\n");
                        }
                    }
                    break;
#endif
                case VENDOR_CMD_STOP_WRITE:
                    DBG_APP_INFO("CMD: STOP_WRITE (Streaming)\r\n");
                    pAppCtxt->streamState = STATE_IDLE;
                    /* Disable QSPI write streaming mode */
                    Cy_QSPI_StopWriteStream();
#if !WRITE_PRIME_ENABLE
                    /* Flush any pending deferred write descriptors so they don't get sent after STOP */
                    while (write_defer_tail != write_defer_head) {
                        cy_stc_hbdma_buff_status_t fb = write_defer_bufs[write_defer_tail];
                        write_defer_tail = (uint8_t)((write_defer_tail + 1) & (WRITE_DEFER_RING_SIZE - 1));
                        Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pOutEpDma[2], &fb);
                        write_defer_flushed++;
                    }
#endif
#if !WRITE_PRIME_ENABLE
                    write_reset_pending = true;
#endif
                    
                    Print_Qspi_WriteStop();
                    DBG_APP_INFO("CMD: QSPI write streaming disabled\r\n");
                    break;
            
#if !READ_PRIME_ENABLE
                case VENDOR_CMD_START_READ:
                    DBG_APP_INFO("CMD: START_READ (Streaming)\r\n");
                    if (pAppCtxt->streamState == STATE_IDLE)
                    {
                        /* Ensure IN EP3 DMA channel is enabled before seeding deferred buffers. */
                        if (pAppCtxt->pInEpDma[3] != NULL) {
                            cy_en_hbdma_chn_state_t chState = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                            Cy_Debug_AddToLog(1, "StartRead: IN EP3 channel state=%u\r\n", (uint32_t)chState);
                            if (chState != CY_HBDMA_CHN_ACTIVE) {
                                cy_en_hbdma_mgr_status_t en = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[3], 0);
                                if (en == CY_HBDMA_MGR_SEQUENCE_ERROR) {
                                    Cy_Debug_AddToLog(1, "StartRead: ENABLE -> SEQUENCE_ERROR, resetting channel and retrying\r\n");
                                    Cy_HBDma_Channel_Reset(pAppCtxt->pInEpDma[3]);
                                    Cy_SysLib_DelayUs(50);
                                    en = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[3], 0);
                                }
                                Cy_Debug_AddToLog(1, "StartRead: Channel_Enable returned %d\r\n", (int)en);
                            }
                        } else {
                            Cy_Debug_AddToLog(1, "StartRead: IN EP3 channel handle NULL\r\n");
                        }
                        pAppCtxt->streamState = STATE_CONT_READ;
                        
						/* Enable QSPI read streaming mode */
                        Cy_QSPI_StartReadStream();
                        DBG_APP_INFO("CMD: QSPI read streaming enabled - Read data from EP3 \r\n");

                        if (pAppCtxt->pInEpDma[3] != NULL) {
                            for (int seed = 0; seed < READ_DEFER_SEED_COUNT; ++seed) {
                                cy_stc_hbdma_buff_status_t buff;
                                read_defer_seed_attempts++;
                                cy_en_hbdma_mgr_status_t s = Cy_HBDma_Channel_GetBuffer(pAppCtxt->pInEpDma[3], &buff);
                                if (s != CY_HBDMA_MGR_SUCCESS || buff.pBuffer == NULL) {
                                    break; /* no more buffers available right now */
                                }
                                uint8_t next = (uint8_t)((read_defer_head + 1) & (READ_DEFER_RING_SIZE - 1));
                                if (next != read_defer_tail) {
                                    /* copy buffer descriptor into ring */
                                    read_defer_bufs[read_defer_head] = buff;
                                    
                                    /* advance head atomically */
                                    read_defer_head = next;
                                    read_defer_seed_success++;
                                } else {
                                    /* ring full - release the buffer back and stop seeding */
                                    Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &buff);
                                    read_defer_overflow++;
                                    break;
                                }
                            }
                        } else {
                            DBG_APP_ERR("CMD: EP3 IN channel not available \r\n");
                        }
                    }
                    break;
#else
                case VENDOR_CMD_START_READ:
                    DBG_APP_INFO("CMD: START_READ (Streaming)\r\n");
                    if (pAppCtxt->streamState == STATE_IDLE)
                    {
                        pAppCtxt->streamState = STATE_CONT_READ;
                        /* Enable QSPI read streaming mode */
                        Cy_QSPI_StartReadStream();
                        
                        if (pAppCtxt->pInEpDma[3] != NULL) 
                        {
                            /* Ensure channel is enabled before priming */
                            Cy_Debug_AddToLog(1, "ReadPrime: pInEpDma[3]=%p, pHbDmaMgrCtxt=%p\r\n", (void*)pAppCtxt->pInEpDma[3], (void*)pAppCtxt->pHbDmaMgrCtxt);
                            /* Check channel state before enabling */
                            {
                                cy_en_hbdma_chn_state_t chState = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                Cy_Debug_AddToLog(1, "ReadPrime: channel state=%u\r\n", (uint32_t)chState);
                                if (chState == CY_HBDMA_CHN_ACTIVE) 
                                {
                                    Cy_Debug_AddToLog(1, "ReadPrime: channel ACTIVE - resetting before priming\r\n");
                                    Cy_HBDma_Channel_Reset(pAppCtxt->pInEpDma[3]);
                                    Cy_SysLib_DelayUs(50);
                                }
                                mgrStat = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[3], 0);
                                Cy_Debug_AddToLog(1, "ReadPrime: Channel_Enable returned %s (%d)\r\n", HbdmaMgrStatusStr(mgrStat), (int)mgrStat);
                            }
                            if (mgrStat == CY_HBDMA_MGR_SEQUENCE_ERROR) 
                            {
                                Cy_Debug_AddToLog(1, "ReadPrime: SEQUENCE_ERROR - channel details\r\n");
                                Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr) 
                                {
                                    Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                                } else 
                                {
                                    Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                                }
                            }
                            Cy_SysLib_DelayUs(50);
                            /*
                             * Prime the IN endpoint with multiple buffers to start high-speed data flow.
                             */
                            DBG_APP_INFO("CMD: Priming EP3 IN for high-speed streaming\r\n");
                            
                            /* Prime with multiple buffers for continuous high-speed flow.
                             * Stop priming when GetBuffer fails to avoid over-requesting
                             */
                            int primed = 0;
                            for (int i = 0; i < 12; i++)
                            {
                                cy_stc_hbdma_buff_status_t buff;
                                cy_en_hbdma_mgr_status_t status = Cy_HBDma_Channel_GetBuffer(pAppCtxt->pInEpDma[3], &buff);

                                if (status != CY_HBDMA_MGR_SUCCESS)
                                {
                                    bool recovered = false;
                                    const int maxRetries = 3;
                                    for (int r = 0; r < maxRetries; r++) 
                                    {
                                        cy_en_hbdma_chn_state_t chStateTry = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                        Cy_Debug_AddToLog(1, "ReadPrime: retry %d channel state=%u\r\n", r, (uint32_t)chStateTry);
                                        
                                        if (chStateTry != CY_HBDMA_CHN_ACTIVE) 
                                        {
                                            mgrStat = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[3], 0);
                                            Cy_Debug_AddToLog(1, "ReadPrime: Channel_Enable try %d returned %s (%d)\r\n", r, HbdmaMgrStatusStr(mgrStat), (int)mgrStat);
                                        } 
                                        else 
                                        {
                                            Cy_Debug_AddToLog(1, "ReadPrime: retry %d - channel already ACTIVE, skipping enable\r\n", r);
                                            mgrStat = CY_HBDMA_MGR_SUCCESS;
                                        }
                                        
                                        if (mgrStat == CY_HBDMA_MGR_SEQUENCE_ERROR) 
                                        {
                                            Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                        }
                                        Cy_SysLib_DelayUs(50);
                                        status = Cy_HBDma_Channel_GetBuffer(pAppCtxt->pInEpDma[3], &buff);
                                        if (status == CY_HBDMA_MGR_SUCCESS) 
                                        { 
                                            recovered = true; 
                                            break; 
                                        }
                                    }
                                    if (!recovered) 
                                    {
                                        Cy_Debug_AddToLog(1, "ReadPrime: channel-level recovery - resetting channel and attempting re-enable\r\n");
                                        Cy_HBDma_Channel_Reset(pAppCtxt->pInEpDma[3]);
                                        const int recoveryEnableRetries = 4;
                                        
                                        for (int er = 0; er < recoveryEnableRetries; ++er) 
                                        {
                                            cy_en_hbdma_mgr_status_t enStat = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[3], 0);
                                            Cy_Debug_AddToLog(1, "ReadPrime: Channel_Enable recovery try %d returned %s (%d)\r\n", er, HbdmaMgrStatusStr(enStat), (int)enStat);
                                            
                                            /* Channel state after enable attempt */
                                            cy_en_hbdma_chn_state_t postStateR = Cy_HBDma_Channel_GetChannelState((cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                            Cy_Debug_AddToLog(1, "ReadPrime: post-enable channel state=%u\r\n", (uint32_t)postStateR);
                                            
                                            if (enStat == CY_HBDMA_MGR_SEQUENCE_ERROR) 
                                            {
                                                Cy_Debug_AddToLog(1, "ReadPrime: SEQUENCE_ERROR during recovery: channel and bufmgr details:\r\n");
                                                Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                                if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr) 
                                                {
                                                    Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                                                } 
                                                else 
                                                {
                                                    Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                                                }
                                            }
                                            if (enStat == CY_HBDMA_MGR_SUCCESS) 
                                            {
                                                status = Cy_HBDma_Channel_GetBuffer(pAppCtxt->pInEpDma[3], &buff);
                                                if (status == CY_HBDMA_MGR_SUCCESS) 
                                                { 
                                                    recovered = true; 
                                                    break; 
                                                }
                                                else 
                                                {
                                                    Cy_Debug_AddToLog(1, "ReadPrime: GetBuffer after successful enable returned %s (%d)\r\n", HbdmaMgrStatusStr(status), (int)status);
                                                    Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[3]);
                                                    if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr) 
                                                    {
                                                        Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                                                    } 
                                                    else 
                                                    {
                                                        Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                                                    }
                                                }
                                            }
                                            Cy_SysLib_DelayUs(100);
                                        }
                                        if (!recovered) 
                                        {
                                            Cy_Debug_AddToLog(1, "ReadPrime: channel recovery failed\r\n");
                                        }
                                    }
                                    if (!recovered) 
                                    {
                                            Cy_Debug_AddToLog(1, "ReadPrime: No more buffers available after %d, status=%s (%d)\r\n", i, HbdmaMgrStatusStr(status), (int)status);
                                            if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr) 
                                            {
                                                Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                                            } 
                                            else 
                                            {
                                                Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                                            }
                                        break;
                                    }
                                }

                                if (buff.pBuffer == NULL)
                                {
                                    Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &buff);
                                    Cy_Debug_AddToLog(1, "ReadPrime: got a NULL buffer at i=%d - discarded\r\n", i);
                                    continue;
                                }

                                /* High-speed QSPI read into the buffer and commit it */
                                bool st = QspiReceiveData(buff.pBuffer, (uint16_t)buff.size);
                                if (st == false)
                                {
                                    Cy_Debug_AddToLog(1, "ReadPrime: QSPI read failed at i=%d\r\n", i);
                                    Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &buff);
                                    continue;
                                }
                                buff.count = buff.size;
                                status = Cy_HBDma_Channel_CommitBuffer(pAppCtxt->pInEpDma[3], &buff);
                                if (status != CY_HBDMA_MGR_SUCCESS)
                                {
                                    Cy_Debug_AddToLog(1, "ReadPrime: CommitBuffer failed at i=%d, status=%d\r\n", i, (int)status);
                                    /* Try to continue priming remaining buffers */
                                    continue;
                                }
                                primed++;
                                read_prime_committed++;
                            }
                            Cy_Debug_AddToLog(1, "ReadPrime: primed %d buffers for EP3 IN\r\n", primed);
                        }
                        else 
                        {
                            DBG_APP_ERR("CMD: EP3 IN channel not available (NULL handle)\r\n");
                        }
                    }
                    break;
#endif
                case VENDOR_CMD_STOP_READ:
                    DBG_APP_INFO("CMD: STOP_READ (Streaming)\r\n");
                    if (pAppCtxt->streamState == STATE_CONT_READ)
                    {
                        pAppCtxt->streamState = STATE_IDLE;
                        /* Disable QSPI read streaming mode */
                        Cy_QSPI_StopReadStream();
#if !READ_PRIME_ENABLE
                        /* Drop any reserved buffers in the deferred ring before resetting the channel. */
                        while (read_defer_tail != read_defer_head) {
                            cy_stc_hbdma_buff_status_t fb = read_defer_bufs[read_defer_tail];
                            read_defer_tail = (uint8_t)((read_defer_tail + 1) & (READ_DEFER_RING_SIZE - 1));
                            if (pAppCtxt->pInEpDma[3] != NULL) {
                                Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &fb);
                            }
                        }
#endif
                        /* Disable EP3 (read endpoint) */
                        if (pAppCtxt->pInEpDma[3] != NULL) 
                        {
                            Cy_HBDma_Channel_Reset(pAppCtxt->pInEpDma[3]);
                            DBG_APP_INFO("CMD: EP3 IN reset\r\n");
                        }

                        Print_Qspi_ReadStop();
                        DBG_APP_INFO("CMD: QSPI read streaming disabled\r\n");
                    }
                    break;

                case VENDOR_CMD_SMIF_STATUS:
                    /* Status command */
                        DBG_APP_INFO("SMIF status: Cmd FIFO=%u, TX FIFO=%u, RX FIFO=%u, Busy=%u\r\n",
                        (uint32_t)Cy_SMIF_GetCmdFifoStatus(SMIF_HW),
                        (uint32_t)Cy_SMIF_GetTxFifoStatus(SMIF_HW),
                        (uint32_t)Cy_SMIF_GetRxFifoStatus(SMIF_HW),
                        (uint32_t)Cy_SMIF_BusyCheck(SMIF_HW));
                    break;

                case VENDOR_CMD_SMIF_THROUGHPUT:
				        /* Set clock to 24 MHz and flag throughput test.
                         * The actual test runs in main loop to avoid blocking ISR or control path. */
                        DBG_APP_INFO("SMIF: Setting clock to 24 MHz for throughput test\r\n");
                        QspiBridgeSetClockFrequency(24);
                        run_smif_throughput_request = 1;
                        DBG_APP_INFO("SMIF: Throughput test will run in main loop\r\n");
                    break;

                case VENDOR_CMD_SET_SMIF_CLK_FREQ:
                    /* Set QSPI clock frequency - frequency value is second byte in command packet */
                    DBG_APP_INFO("CMD: SET_SMIF_CLK_FREQ\r\n");
                    
                    if (buffStat.count >= 2)
                    {
                        /* Second byte contains the frequency value */
                        uint8_t frequency = buffStat.pBuffer[1];
                        
                        DBG_APP_INFO("Setting QSPI clock frequency to %u MHz\r\n", (uint32_t)frequency);
                        
                        if(frequency < 0x0B || frequency > 0x27)
                        {
                            DBG_APP_WARN("Frequency %u MHz out of range (11-39 MHz)\r\n", (uint32_t)frequency);
                            frequency = 24; /* Default to 24 MHz */
                            DBG_APP_WARN("Going with default frequency of %u MHz\r\n", (uint32_t)frequency);
                        }

                        /* Set the clock frequency using the received value */
                        QspiBridgeSetClockFrequency(frequency);
                        
                        DBG_APP_INFO("QSPI clock frequency set to %u MHz\r\n", (uint32_t)frequency);
                    }
                    else
                    {
                        DBG_APP_ERR("Invalid SET_SMIF_CLK_FREQ packet (count=%u, expected 2)\r\n", (uint32_t)buffStat.count);
                    }
                    break;

                default:
                    DBG_APP_WARN("QSPI Bridge: Unknown command: 0x%02X\r\n", command);
                    break;
            }
            
            /* Always discard command buffer */
            Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);
        }
        else if (isOut && (epNum == 2) && pAppCtxt->streamState == STATE_CONT_WRITE && buffStat.count > 0)
        {
            /* 
             * Data write endpoint 
             */
            
            /* a counter to keep track of writes,
             * to avoid flooding the debug log
             */
            static uint32_t debug_count = 0;
            debug_count++;
            
            if ((debug_count % 1000) == 0)
            {
                Cy_Debug_AddToLog(1,"QSPI Write Path: count=%u, len=%u, state=%d\r\n", (uint32_t)debug_count, (uint32_t)buffStat.count, (int)pAppCtxt->streamState);
            } 

            if (buffStat.count > 0)
            {
#if !WRITE_PRIME_ENABLE
                /* Enqueue the outbound buffer into an ring for main-loop transmission. */
                write_defer_enqueue_attempts++;
                uint8_t nextw = (uint8_t)((write_defer_head + 1) & (WRITE_DEFER_RING_SIZE - 1));
                if (nextw != write_defer_tail) {
                    write_defer_bufs[write_defer_head] = buffStat;
                    /* advance head atomically */
                    write_defer_head = nextw;
                    write_defer_enqueue_success++;
                } else {
                    /* ring full - drop this buffer to avoid blocking in ISR */
                    write_defer_overflow++;
                    Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);
                }
#else
                /* Primed/legacy path: blocking transmit in callback (existing behavior) */
                {
                    uint32_t wt0 = Cy_USBD_GetTimerTick();
                    
                    bool status = QspiTransmitData(buffStat.pBuffer, (uint16_t)buffStat.count);
                    
                    uint32_t wt1 = Cy_USBD_GetTimerTick();
                    uint32_t wdt = (wt1 >= wt0) ? (wt1 - wt0) : (wt1 + (0xFFFFFFFFUL - wt0));

                    if (status == false) {
                        DBG_APP_ERR("QSPI WriteData failed at count=%u\r\n", (uint32_t)debug_count);
                        
                        /* Still discard so DMA can reuse buffer */
                        write_prime_transmit_fail++;
                        Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);

                    } else {
                        write_prime_transmits++;

                        qspi_write_calls++;
                        qspi_write_ms_total += wdt;
                        qspi_write_bytes_total += buffStat.count;
                        if (wdt < qspi_write_ms_min) qspi_write_ms_min = wdt;
                        if (wdt > qspi_write_ms_max) qspi_write_ms_max = wdt;
                        /* Discard the buffer to return it to the DMA manager */
                        cy_en_hbdma_mgr_status_t cstat = Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);
                        if (cstat != CY_HBDMA_MGR_SUCCESS) {
                            DBG_APP_ERR("WritePrime: DiscardBuffer failed %d\r\n", (int)cstat);
                        }
                    }
                }
#endif
            }
        }
        else
        {
            /* 
             * This path bypasses QSPI completely
             * If data is coming here instead of QSPI path it's USB speed.
             */
            static uint32_t discard_count = 0;
            discard_count++;
            
            if ((discard_count % 50) == 0)
            {
                DBG_APP_WARN("BYPASS QSPI: EP%d, isOut=%d, state=%d, count=%u, discard_total=%u\r\n", 
                             epNum, isOut ? 1 : 0, pAppCtxt->streamState, (uint32_t)buffStat.count, (uint32_t)discard_count);
            }
            
            Cy_HBDma_Channel_DiscardBuffer(handle, &buffStat);
        }
    }
    else if (type == CY_HBDMA_CB_CONS_EVENT)
    {
        /* IN endpoint for reads */
        if (!isOut && (epNum == 3) && pAppCtxt->streamState == STATE_CONT_READ)
        { 
#if !READ_PRIME_ENABLE
            /* Non-primed mode: do not perform blocking SMIF reads in callback.
             * Reserve the buffer and enqueue it into the read_defer ring
             * so the main loop can perform QspiReceiveData() and CommitBuffer().
             */
            cy_stc_hbdma_buff_status_t new_buff;
            status = Cy_HBDma_Channel_GetBuffer(handle, &new_buff);
            if (status == CY_HBDMA_MGR_SUCCESS) {
                read_defer_seed_attempts++;
                if (new_buff.pBuffer == NULL) {
                    Cy_HBDma_Channel_DiscardBuffer(handle, &new_buff);
                } else {
                    uint8_t next = (uint8_t)((read_defer_head + 1) & (READ_DEFER_RING_SIZE - 1));
                    if (next != read_defer_tail) {
                        read_defer_bufs[read_defer_head] = new_buff;
                        read_defer_head = next;
                        read_defer_seed_success++;
                    } else {
                        /* ring full - discard buffer */
                        Cy_HBDma_Channel_DiscardBuffer(handle, &new_buff);
                        read_defer_overflow++;
                    }
                }
            }
#else
            /* Primed mode: existing behavior (blocking read then commit) */
            cy_stc_hbdma_buff_status_t new_buff;
            status = Cy_HBDma_Channel_GetBuffer(handle, &new_buff);

            if (status == CY_HBDMA_MGR_SUCCESS)
            {
                if (new_buff.pBuffer == NULL)
                {
                    Cy_HBDma_Channel_DiscardBuffer(handle, &new_buff);
                    return;
                }

                {
                    uint32_t rt0 = Cy_USBD_GetTimerTick();
                    bool st = QspiReceiveData(new_buff.pBuffer, (uint16_t)new_buff.size);
                    uint32_t rt1 = Cy_USBD_GetTimerTick();
                    uint32_t rdt = (rt1 >= rt0) ? (rt1 - rt0) : (rt1 + (0xFFFFFFFFUL - rt0));
                    if (st == false)
                    {
                        Cy_Debug_AddToLog(1, "ReadCons: QSPI read failed\r\n");
                        Cy_HBDma_Channel_DiscardBuffer(handle, &new_buff);
                        return;
                    }
                    /* Update read instrumentation */
                    qspi_read_calls++;
                    qspi_read_ms_total += rdt;
                    qspi_read_bytes_total += new_buff.size;
                    if (rdt < qspi_read_ms_min) qspi_read_ms_min = rdt;
                    if (rdt > qspi_read_ms_max) qspi_read_ms_max = rdt;
                    
                }
                new_buff.count = new_buff.size;
                status = Cy_HBDma_Channel_CommitBuffer(handle, &new_buff);
            }
#endif
        }
    }
}
/* Main bridge device task handler: initialize, drain deferred rings, run tests */
void 
Cy_USB_BridgeDeviceTaskHandler (void *pTaskParam)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    cy_stc_usbd_app_msg_t queueMsg;

    /* endpAddress  will have endpNum and direction. */
    uint8_t endpAddr;
    uint32_t endpNum;
    uint32_t lpEntryTime = 0;

    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pTaskParam;

    bool xStatus;
    cy_en_hbdma_mgr_status_t mgrStat;

    DBG_APP_INFO("Bridge: Task Handler\r\n");
    DBG_APP_INFO("Bridge: Task started pAppCtxt=%p \r\n", (void*)pAppCtxt);
    
    if (pAppCtxt == NULL) {
        DBG_APP_ERR("AppCtxt is NULL \r\n");
        return;
    }
    
    /* Initialize state */
    pAppCtxt->streamState = STATE_IDLE;
    DBG_APP_INFO("STREAM STATE set to IDLE\r\n");

    /* Initialize the QSPI interface */
    QspiInterfaceInit();
    
    /* Enable USB-3 connection and wait until it is stable. */
    Cy_SysLib_Delay(250);

    /* If VBus is present, enable the USB connection. */
    pAppCtxt->vbusPresent =
    (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);
#if USBFS_LOGS_ENABLE
        Cy_SysLib_Delay(500);
#endif /* USBFS_LOGS_ENABLE */

    if (pAppCtxt->vbusPresent) {
        if (!pAppCtxt->usbConnectDone)
        {
            (void)Cy_USB_ConnectionEnable(pAppCtxt);
        }
    }

    do {
#if WATCHDOG_RESET_EN
        /* Kick The WDT to prevent RESET */
        KickWDT();
#endif /* WATCHDOG_RESET_EN */

#if LPM_ENABLE
        if ((pAppCtxt->isLpmEnabled == false) && (pAppCtxt->lpmEnableTime != 0)) {

            if (
                    (Cy_USBD_GetTimerTick() >= pAppCtxt->lpmEnableTime)
               )
            {
                pAppCtxt->isLpmEnabled  = true;
                pAppCtxt->lpmEnableTime = 0;
                Cy_USBD_LpmEnable(pAppCtxt->pUsbdCtxt);
            }
        }
#endif /* LPM_ENABLE */

        /*
         * Wait until some data is received from the queue.
         * Timeout after 100 ms.
         */
    xStatus = sq_receive(pAppCtxt->xQueue, &queueMsg);
    if (!xStatus) 
    {
        /* No message received: continue */
    } 
    else 
    {
        /* Message received - normalize common fields and dispatch */
        endpAddr = queueMsg.data[0];
        endpNum = (endpAddr & CY_USBD_ENDP_NUM_MASK);

        switch (queueMsg.type) {
            case CY_USB_VBUS_CHANGE_INTR:
                pAppCtxt->vbusChangeIntr = false;
                pAppCtxt->vbusPresent = (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);
                if (pAppCtxt->vbusPresent) {
                    if (!pAppCtxt->usbConnected) {
                        DBG_APP_INFO("USB: Enabling USB connection due to VBus detect\r\n");
                        Cy_USB_ConnectionEnable(pAppCtxt);
                    }
                } else {
                    if (pAppCtxt->usbConnected) {
                        DBG_APP_INFO("USB: Disabling USB connection due to VBus removal\r\n");
                        Cy_USB_ConnectionDisable(pAppCtxt);
                    }
                }
                break;

            case CY_USB_VBUS_CHANGE_DEBOUNCED:
                /* Check whether VBus state has changed. */
                pAppCtxt->vbusPresent = (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);

                if (pAppCtxt->vbusPresent) {
                    if (!pAppCtxt->usbConnected) {
                        DBG_APP_INFO("USB: Enabling USB connection due to VBus detect\r\n");
                        Cy_USB_ConnectionEnable(pAppCtxt);
                    }
                } else {
                    if (pAppCtxt->usbConnected) {
                        DBG_APP_INFO("USB: Disabling USB connection due to VBus removal\r\n");
                        Cy_USB_ConnectionDisable(pAppCtxt);
                    }
                }
                break;

            case CY_USB_BRIDGE_DEVICE_MSG_SETUP_DATA_XFER:
                DBG_APP_INFO("Bridge: Setup data transfer message received\r\n");
                DBG_APP_INFO("Bridge: DMA channels will be enabled later\r\n");
                break;

            case CY_USB_BRIDGE_DEVICE_MSG_START_DATA_XFER:
                DBG_APP_INFO("Bridge: START_DATA_XFER message received \r\n");
                /* Initialize all endpoint DMA channels for data transfer */
                for (endpNum = 0x01; endpNum < CY_USB_NUM_ENDP_CONFIGURED; endpNum++) {
                    if(pAppCtxt->pInEpDma[endpNum] != NULL) 
					{
                        DBG_APP_INFO("Enabling IN EP%d for data transfer \r\n", (int)endpNum);
                        mgrStat = Cy_HBDma_Channel_Enable(pAppCtxt->pInEpDma[endpNum], 0);
                        if (mgrStat != CY_HBDMA_MGR_SUCCESS) {
                            DBG_APP_ERR("Bridge: IN EP%d channel enable FAILED: %x\r\n", (int)endpNum, mgrStat);
                        } else {
                            DBG_APP_INFO("Bridge: IN EP%d channel enable status: %x\r\n", (int)endpNum, mgrStat);
                        }
                        
                        Cy_Debug_AddToLog(1, "DBG: IN EP%d handle=0x%x\r\n", (int)endpNum, (uint32_t)(uintptr_t)pAppCtxt->pInEpDma[endpNum]);
                        Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pInEpDma[endpNum]);
                    }
                    if(pAppCtxt->pOutEpDma[endpNum] != NULL) 
					{
                        DBG_APP_INFO("Enabling OUT EP%d for data reception \r\n", (int)endpNum);
                        mgrStat = Cy_HBDma_Channel_Enable(pAppCtxt->pOutEpDma[endpNum], 0);
                        if (mgrStat != CY_HBDMA_MGR_SUCCESS) {
                            DBG_APP_ERR("Bridge: OUT EP%d channel enable FAILED: %x\r\n", (int)endpNum, mgrStat);
                        } else {
                            DBG_APP_INFO("Bridge: OUT EP%d channel enable status: %x\r\n", (int)endpNum, mgrStat);
                        }
                        
                        Cy_Debug_AddToLog(1, "DBG: OUT EP%d handle=0x%x\r\n", (int)endpNum, (uint32_t)(uintptr_t)pAppCtxt->pOutEpDma[endpNum]);
                        Print_HbDma_Channel_State((const cy_stc_hbdma_channel_t *)pAppCtxt->pOutEpDma[endpNum]);
                    }
                }
                
                if (pAppCtxt && pAppCtxt->pHbDmaMgrCtxt && pAppCtxt->pHbDmaMgrCtxt->pBufMgr)
                {
                    Print_HbDma_BufMgr_Stats(pAppCtxt->pHbDmaMgrCtxt->pBufMgr);
                }
                else
                {
                    Print_HbDma_BufMgr_Stats(&HBW_BufMgr);
                }
                break;

            case CY_USB_BRIDGE_DEVICE_MSG_CTRL_XFER_SETUP:
                DBG_APP_TRACE("Bridge: Control request\r\n");
                Cy_USB_BridgeDeviceHandleCtrlSetup((void *)pAppCtxt, &queueMsg);
                break;

            case CY_USB_ENDP0_READ_TIMEOUT:
                DBG_APP_TRACE("Bridge: EP0 Read timeout\r\n");
                Cy_USB_USBD_RetireRecvEndp0Data(pAppCtxt->pUsbdCtxt);
                break;

            case CY_USB_BRIDGE_DEVICE_MSG_L1_SLEEP:
                DBG_APP_TRACE("Bridge:L1 Sleep \r\n");
                break;

            case CY_USB_BRIDGE_DEVICE_MSG_L1_RESUME:
                DBG_APP_TRACE("Bridge:L1 Resume \r\n");
                break;

            default:
                DBG_APP_ERR("BridgeMsgDefault %d\r\n", queueMsg.type);
                break;
        } 
    }


    /*
     * If the link has been in USB2-L1 for more than 0.5 seconds, initiate LPM exit so that
     * transfers do not get delayed significantly.
     */
        if ((MXS40USBHSDEV_USBHSDEV->DEV_PWR_CS & USBHSDEV_DEV_PWR_CS_L1_SLEEP) != 0)
        {
            if ((Cy_USBD_GetTimerTick() - lpEntryTime) >= 500UL) {
                lpEntryTime = Cy_USBD_GetTimerTick();
                Cy_USBD_GetUSBLinkActive(pAppCtxt->pUsbdCtxt);
            }
        } else {
            lpEntryTime = Cy_USBD_GetTimerTick();
        }
		
#if !READ_PRIME_ENABLE
        /* Drain any deferred read reservations: fill them and commit to start IN flow */
        while (read_defer_tail != read_defer_head) {
            cy_stc_hbdma_buff_status_t dbuf = read_defer_bufs[read_defer_tail];
            /* advance tail */
            read_defer_tail = (uint8_t)((read_defer_tail + 1) & (READ_DEFER_RING_SIZE - 1));
            if (dbuf.pBuffer == NULL || dbuf.size == 0) {
                Cy_Debug_AddToLog(1, "ReadDeferred: invalid reserved buffer - discarding\r\n");
                continue;
            }
            /* Blocking SMIF read into reserved buffer, performed in main loop */
            uint32_t rt0 = Cy_USBD_GetTimerTick();

            bool status = QspiReceiveData(dbuf.pBuffer, (uint16_t)dbuf.size);
            if (!status) {
                Cy_Debug_AddToLog(1, "ReadDeferred: QSPI read failed while filling reserved buffer\r\n");
                /* discard the reserved buffer so DMA can reuse it */
                if (pAppCtxt && pAppCtxt->pInEpDma[3] != NULL) {
                    Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &dbuf);
                }
                continue;
            }
            dbuf.count = dbuf.size;
            /* Update millisecond timing counters for deferred reads */
            uint32_t rt1 = Cy_USBD_GetTimerTick();
            uint32_t rdt = (rt1 >= rt0) ? (rt1 - rt0) : (rt1 + (0xFFFFFFFFUL - rt0));
            qspi_read_calls++;
            qspi_read_ms_total += rdt;
            qspi_read_bytes_total += dbuf.size;
            if (rdt < qspi_read_ms_min) qspi_read_ms_min = rdt;
            if (rdt > qspi_read_ms_max) qspi_read_ms_max = rdt;
            if (pAppCtxt && pAppCtxt->pInEpDma[3] != NULL) {
                cy_en_hbdma_mgr_status_t cstat = Cy_HBDma_Channel_CommitBuffer(pAppCtxt->pInEpDma[3], &dbuf);
                if (cstat != CY_HBDMA_MGR_SUCCESS) {
                    Cy_Debug_AddToLog(1, "ReadDeferred: CommitBuffer failed: %d\r\n", (int)cstat);
                    /* if commit failed, discard */
                    read_defer_commit_fail++;
                    Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pInEpDma[3], &dbuf);
                } else {
                    read_defer_commits++;
                }
            }
        }
#endif

#if !WRITE_PRIME_ENABLE
        /* Drain deferred write ring: send buffers to QSPI and discard them so DMA can reuse. */
        {
            /* Measure drain loop entry time to capture per-iteration drain duration */
            uint32_t drain_t0 = Cy_USBD_GetTimerTick();
            while (write_defer_tail != write_defer_head) {
                cy_stc_hbdma_buff_status_t wbuf = write_defer_bufs[write_defer_tail];
                /* advance tail */
                write_defer_tail = (uint8_t)((write_defer_tail + 1) & (WRITE_DEFER_RING_SIZE - 1));
                if (wbuf.pBuffer == NULL || wbuf.size == 0) {
                    Cy_Debug_AddToLog(1, "WriteDeferred: invalid reserved buffer - discarding\r\n");
                    /* discard to return buffer to DMA */
                    if (pAppCtxt && pAppCtxt->pOutEpDma[2] != NULL) {
                        Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pOutEpDma[2], &wbuf);
                    }
                    continue;
                }
                /* Blocking QSPI write into reserved buffer, performed in main loop. Measure time. */
                uint32_t t0 = Cy_USBD_GetTimerTick();
                
                bool status = QspiTransmitData(wbuf.pBuffer, (uint16_t)wbuf.count);

                uint32_t t1 = Cy_USBD_GetTimerTick();
                uint32_t dt = (t1 >= t0) ? (t1 - t0) : (t1 + (0xFFFFFFFFUL - t0));
                

                /* update per-call instrumentation */
                qspi_write_calls++;
                qspi_write_ms_total += dt;
                qspi_write_bytes_total += wbuf.count;
                
                /* min/max */
                if (dt < qspi_write_ms_min) qspi_write_ms_min = dt;
                if (dt > qspi_write_ms_max) qspi_write_ms_max = dt;


                if (!status) {
                    Cy_Debug_AddToLog(1, "WriteDeferred: QSPI transmit failed for reserved buffer\r\n");
                    write_defer_transmit_fail++;
                    if (pAppCtxt && pAppCtxt->pOutEpDma[2] != NULL) {
                        Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pOutEpDma[2], &wbuf);
                    }
                    continue;
                }
                /* After transmitting, discard the buffer so DMA can reuse it */
                if (pAppCtxt && pAppCtxt->pOutEpDma[2] != NULL) {
                    cy_en_hbdma_mgr_status_t cstat = Cy_HBDma_Channel_DiscardBuffer(pAppCtxt->pOutEpDma[2], &wbuf);
                    if (cstat != CY_HBDMA_MGR_SUCCESS) {
                        Cy_Debug_AddToLog(1, "WriteDeferred: DiscardBuffer failed: %d\r\n", (int)cstat);
                    }
                }
                write_defer_transmits++;
            }
            uint32_t drain_t1 = Cy_USBD_GetTimerTick();
            uint32_t drain_dt = (drain_t1 >= drain_t0) ? (drain_t1 - drain_t0) : (drain_t1 + (0xFFFFFFFFUL - drain_t0));
            write_drain_calls++;
            write_drain_ms_total += drain_dt;
            write_drain_last_ms = drain_dt;

            if (write_reset_pending && (write_defer_head == write_defer_tail)) {
                if (pAppCtxt && pAppCtxt->pOutEpDma[2] != NULL) {
                    cy_en_hbdma_mgr_status_t rstat = Cy_HBDma_Channel_Reset(pAppCtxt->pOutEpDma[2]);
                    if (rstat != CY_HBDMA_MGR_SUCCESS) {
                        Cy_Debug_AddToLog(1, "WriteDeferred: Channel reset failed: %d\r\n", (int)rstat);
                    }
                }
                write_reset_pending = false;
            }
        }
#endif

        /* If the host requested a SMIF throughput test, run it here (main loop) */
        if (run_smif_throughput_request) {
            /* Clear request before running to avoid reentrancy */
            run_smif_throughput_request = 0;
            /* Run a 50 MB throughput test with 16 KiB chunks as requested by the host */
            Run_Smif_Throughput_Measure(50U * 1024U * 1024U, 16U * 1024U);
        }
    } while (1);
}   

/**
 * \name Cy_USB_BridgeDeviceHandleCtrlSetup
 * \brief This function handles control command given to application.
 * \param pApp application layer context pointer
 * \param pMsg app message queue pointer
 * \retval None
 */
void
Cy_USB_BridgeDeviceHandleCtrlSetup (void *pApp, cy_stc_usbd_app_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    cy_en_usb_endp_dir_t endpDir = CY_USB_ENDP_DIR_INVALID;
    cy_en_usbd_ret_code_t retStatus = CY_USBD_STATUS_SUCCESS;
    uint32_t  setupData0;
    uint32_t  setupData1;
    uint8_t bmRequest, bRequest, bTarget;
    uint16_t wValue, wIndex, wLength;
    uint8_t   reqType;
    bool isReqHandled = false;
	uint8_t loopCount = 250u;

    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pApp;

    setupData0 = pMsg->data[0];
    setupData1 = pMsg->data[1];

    /* Decode the fields from the setup request. */
    bmRequest = (uint8_t)((setupData0 & CY_USB_BMREQUEST_SETUP0_MASK) >>
                           CY_USB_BMREQUEST_SETUP0_POS);
    bRequest =  (uint8_t)((setupData0 & CY_USB_BREQUEST_SETUP0_MASK) >>
                           CY_USB_BREQUEST_SETUP0_POS);
    wValue = (uint16_t)((setupData0 & CY_USB_WVALUE_SETUP0_MASK) >>
                         CY_USB_WVALUE_SETUP0_POS);
    wIndex = (uint16_t)((setupData1 & CY_USB_WINDEX_SETUP1_MASK) >>
                         CY_USB_WINDEX_SETUP1_POS);
    wLength = (uint16_t)((setupData1 & CY_USB_WLENGTH_SETUP1_MASK) >>
                          CY_USB_WLENGTH_SETUP1_POS);

    reqType = ((bmRequest & CY_USB_CTRL_REQ_TYPE_MASK) >>
                                                CY_USB_CTRL_REQ_TYPE_POS);
    bTarget = (bmRequest & CY_USB_CTRL_REQ_RECIPENT_MASK);

    DBG_APP_TRACE("RQ: reqType:%x bRequest %x bTarget %x wValue %x wLength %x \r\n",reqType,bRequest,bTarget,wValue,wLength);
        DBG_APP_TRACE("Bridge: Control Setup Handler\r\n");
        DBG_APP_INFO("CtrlSetup: bmRequest=0x%02X bRequest=0x%02X wValue=0x%04X wIndex=0x%04X wLength=%u\r\n",
                     bmRequest, bRequest, wValue, wIndex, wLength);

    switch (reqType) {

        case CY_USB_CTRL_REQ_STD:
            DBG_APP_TRACE("USB: Stand request\r\n");
            if ((bRequest == CY_USB_SC_SET_FEATURE) &&
                (bTarget == CY_USB_CTRL_REQ_RECIPENT_ENDP) &&
                (wValue == CY_USB_FEATURE_ENDP_HALT)) {
                DBG_APP_INFO("USB: Set Feature - EndpHalt\r\n");
                endpDir = ((wIndex & 0x80UL) ? (CY_USB_ENDP_DIR_IN) :
                         (CY_USB_ENDP_DIR_OUT));
                Cy_USB_USBD_EndpSetClearStall(pAppCtxt->pUsbdCtxt,
                                              ((uint32_t)wIndex & 0x7FUL),
                                               endpDir, true);
                Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                isReqHandled = true;
            }
            
            if ((bRequest == CY_USB_SC_SET_FEATURE) &&
                (bTarget == CY_USB_CTRL_REQ_RECIPENT_DEVICE)) {
                switch (wValue) {
                    case CY_USB_FEATURE_DEVICE_REMOTE_WAKE:
                        DBG_APP_INFO("USB: Set Feature - Remote Wakeup\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;
                    case CY_USB_FEATURE_U1_ENABLE:
                        DBG_APP_INFO("USB: Set Feature - U1 Enable\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;

                    case CY_USB_FEATURE_U2_ENABLE:
                        DBG_APP_INFO("USB: Set Feature - U2 Enable\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;
                    
                    default:
                    /* Unknown feature selector: Request will be stalled below. */
                    break;
                }
            }

            /* Handle FUNCTION_SUSPEND here */
            if ((bRequest == CY_USB_SC_SET_FEATURE) &&
                (bTarget == CY_USB_CTRL_REQ_RECIPENT_INTF) &&
                (wValue == 0x00)) {
                Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                isReqHandled = true;
            }

            if ((bRequest == CY_USB_SC_CLEAR_FEATURE) &&
                (bTarget == CY_USB_CTRL_REQ_RECIPENT_ENDP) &&
                (wValue == CY_USB_FEATURE_ENDP_HALT)) {
                DBG_APP_INFO("USB: Clear Feature\r\n");

                endpDir = ((wIndex & 0x80UL) ? (CY_USB_ENDP_DIR_IN) :
                         (CY_USB_ENDP_DIR_OUT));
                Cy_USBD_FlushEndp(pAppCtxt->pUsbdCtxt,
                                  ((uint32_t)wIndex & 0x7FUL), endpDir);
                Cy_USBD_ResetEndp(pAppCtxt->pUsbdCtxt,
                                  ((uint32_t)wIndex & 0x7FUL), endpDir, false);
                Cy_USB_USBD_EndpSetClearStall(pAppCtxt->pUsbdCtxt,
                                              ((uint32_t)wIndex & 0x7FUL),
                                              endpDir, false);
                Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                isReqHandled = true;
            }
            
            if ((bRequest == CY_USB_SC_CLEAR_FEATURE) &&
                (bTarget == CY_USB_CTRL_REQ_RECIPENT_DEVICE)) {
                switch (wValue) {
                    case CY_USB_FEATURE_DEVICE_REMOTE_WAKE:
                        DBG_APP_INFO("USB: Clear Feature - Remote wakeup\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        Cy_USBD_LpmEnable(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;

                    case CY_USB_FEATURE_U1_ENABLE:
                        DBG_APP_INFO("USB: Clear Feature - U1 Enable\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;

                    case CY_USB_FEATURE_U2_ENABLE:
                         DBG_APP_INFO("USB: Clear Feature - U2 Enable\r\n");
                        Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                        isReqHandled = true;
                        break;
                    
                    default:
                        /*
                         * Unknown feature selector so dont handle here.
                         * just send stall.
                         */
                        isReqHandled = false;
                    break;
                }
            }

            /* Handle Microsoft OS String Descriptor request. */
            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_DEVICE) &&
                (bRequest == CY_USB_SC_GET_DESCRIPTOR) &&
                (wValue == ((CY_USB_STRING_DSCR << 8) | 0xEE))) {

                /* Make sure we do not send more data than requested. */
                if (wLength > glOsString[0]) {
                    wLength = glOsString[0];
                }

                DBG_APP_INFO("USB: OS String\r\n");
                retStatus = Cy_USB_USBD_SendEndp0Data(pAppCtxt->pUsbdCtxt,
                                                      (uint8_t *)glOsString, wLength);
                if (retStatus != CY_USBD_STATUS_SUCCESS) {
                    DBG_APP_ERR("USB: Send Ep0 Failed %x\r\n",retStatus);
                }
                isReqHandled = true;
            }

            break;

        case CY_USB_CTRL_REQ_CLASS:
        case CY_USB_CTRL_REQ_VENDOR:
            DBG_APP_INFO("USB: Class requests - bRequest: %x\r\n",bRequest);

            if ((bRequest == 0xB8) && (wLength != 0) &&
                ((wValue & 0x3) == 0) && ((wValue + wLength) <= 4096U)) {
                if ((bmRequest & 0x80) != 0) {
                    retStatus =
                    Cy_USB_USBD_SendEndp0Data(pAppCtxt->pUsbdCtxt,
                                              ((uint8_t *)Ep0TestBuffer) + wValue,
                                              wLength);
                } else {
                    retStatus =
                    Cy_USB_USBD_RecvEndp0Data(pAppCtxt->pUsbdCtxt,
                                              ((uint8_t *)Ep0TestBuffer) + wValue,
                                              wLength);

                    while (!Cy_USBD_IsEp0ReceiveDone(pAppCtxt->pUsbdCtxt) && loopCount--) {
                        Cy_SysLib_DelayUs(10);
                    }
                }

                if (retStatus == CY_USBD_STATUS_SUCCESS) {
                    isReqHandled = true;
                }
            }

            if ((bRequest == 0xC8) && (wIndex != 0)) {
                pktType = wValue;
                if (pktLength > 1024) {
                    pktLength = 1024;
                }
                pktLength = wIndex;
                Cy_USBD_SendAckSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
                isReqHandled = true;
            }

            /* Handle OS Compatibility and OS Feature requests */
            if (bRequest == MS_VENDOR_CODE) {
                /*
                 * this one is VENDOR request. As off now class and vendor
                 * request under fallback case statement.
                 */
                if (wIndex == 0x04) {
                    if (wLength > *((uint16_t *)glOsCompatibilityId)) {
                        wLength = *((uint16_t *)glOsCompatibilityId);
                    }
                    DBG_APP_INFO("USB: OS Compat\r\n");
                    retStatus = Cy_USB_USBD_SendEndp0Data(pAppCtxt->pUsbdCtxt,
                                                 (uint8_t*)glOsCompatibilityId, wLength);
                    if (retStatus != CY_USBD_STATUS_SUCCESS) {
                        DBG_APP_ERR("USB: Send Ep0 Failed %x\r\n",retStatus);
                    }
                    isReqHandled = true;

                } else if (wIndex == 0x05) {

                    if (wLength > *((uint16_t *)glOsFeature)) {
                        wLength = *((uint16_t *)glOsFeature);
                    }
                    DBG_APP_INFO("USB: OS Feature\r\n");
                    retStatus = Cy_USB_USBD_SendEndp0Data(pAppCtxt->pUsbdCtxt,
                                                          (uint8_t *)glOsFeature, wLength);
                    if (retStatus != CY_USBD_STATUS_SUCCESS) {
                        DBG_APP_ERR("USB: Send Ep0 Failed %x\r\n",retStatus);
                    }
                    isReqHandled = true;
                }
            }

            break;

        default:
            DBG_APP_INFO("Bridge: Control setup :Default\r\n");
            break;
    }

    if(!isReqHandled) {
        Cy_USB_USBD_EndpSetClearStall(pAppCtxt->pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);
    }
    return;
}
