/***************************************************************************//**
* \file usb_app.h
* \version 1.0
*
* \brief Provides definition used by the FX2G3 USB Bridge Device application.
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

#ifndef _CY_USB_APP_H_
#define _CY_USB_APP_H_

#include "simple_queue.h"
#include <stdint.h>

#include <stdbool.h>
#include "cy_debug.h"
#include "cy_usbhs_dw_wrapper.h"
#include "usb_bridge_device.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* P4.0 is used for VBus detect functionality. */
#define VBUS_DETECT_GPIO_PORT                   (P4_0_PORT)
#define VBUS_DETECT_GPIO_PIN                    (P4_0_PIN)
#define VBUS_DETECT_GPIO_INTR                   (ioss_interrupts_gpio_dpslp_4_IRQn)
#define VBUS_DETECT_STATE                       (0u)


/*Related to WINUSB*/
extern uint8_t glOsString[];
extern uint8_t glOsCompatibilityId[];
extern uint8_t glOsFeature[];

/* Forward declaration */
typedef struct cy_stc_usb_app_ctxt_ cy_stc_usb_app_ctxt_t;

/* QSPI Bridge Operation States */
typedef enum
{
    STATE_IDLE,           /* No active operation */
    STATE_CONT_WRITE,     /* Continuous write mode - USB OUT -> QSPI */
    STATE_CONT_READ,      /* Continuous read mode - QSPI -> USB IN */
    STATE_SET_FREQ        /* Set frequency mode - actual value comes via OUT EP2 */
} data_stream_state_t;

/* Message structure for QSPI Writer task queue */
typedef struct
{
    cy_stc_hbdma_channel_t      *handle;           /* DMA channel handle */
    cy_stc_hbdma_buff_status_t  buffStat;         /* Buffer status structure */
} qspi_write_msg_t;

/* USBD layer return code shared between USBD layer and Application layer. */
typedef enum cy_en_usb_app_ret_code_ {
    CY_USB_APP_STATUS_SUCCESS=0,
    CY_USB_APP_STATUS_FAILURE,
}cy_en_usb_app_ret_code_t;

/* 
 * USB applicatio data structure which is bridge between USB system and device
 * functionality.
 * It maintains some usb system information which comes from USBD and it also
 * maintains information about device functionality.
 */
struct cy_stc_usb_app_ctxt_
{
    uint8_t firstInitDone;
    uint8_t devAddr;
    uint8_t activeCfgNum;
    uint8_t currentAltSetting;
    cy_en_usb_device_state_t devState;
    cy_en_usb_device_state_t prevDevState;
    cy_en_usb_speed_t devSpeed;
    cy_en_usb_enum_method_t enumMethod;

    cy_stc_app_endp_dma_set_t endpInDma[CY_USB_NUM_ENDP_CONFIGURED];
    cy_stc_app_endp_dma_set_t endpOutDma[CY_USB_NUM_ENDP_CONFIGURED];
    
    cy_stc_hbdma_channel_t *pInEpDma[CY_USB_NUM_ENDP_CONFIGURED];       /** PDM to USB DMA channel handle. */
    cy_stc_hbdma_channel_t *pOutEpDma[CY_USB_NUM_ENDP_CONFIGURED];       /** PDM to USB DMA channel handle. */
    
    uint8_t intfAltSetEndp[4][4];    /* Max 4INTF AND EACH INTERFACE 4 alt setting */ 
    /* Next three are related to central DMA */
    DMAC_Type *pCpuDmacBase;
    DW_Type *pCpuDw0Base;
    DW_Type *pCpuDw1Base;
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt;
    cy_stc_hbdma_mgr_context_t *pHbDmaMgrCtxt;

    /* Application queues */
    sq_handle_t xQueue;
    sq_handle_t qspiWriteQueue;

    /* Data flow state */
    volatile data_stream_state_t streamState;
    void *pDevFuncCtxt;
    /* Timer functionality for endp0Rcvd */
    void *endp0OutTimerHandle;
    uint32_t endp0OuttimerExpiry;
    bool dataXferIntrEnabled;
    /* VBus detect status */
    bool vbusPresent;
    bool usbConnected;                   /** Whether USB connection is enabled. */
    /* USB connection status */
    bool usbConnectDone;

    bool vbusChangeIntr;                 /** VBus change interrupt received flag. */

    bool          isLpmEnabled;                         /* Whether LPM transitions are enabled. */
    uint32_t      lpmEnableTime;                        /* Timestamp at which LPM should be re-enabled. */

    uint8_t *qspiWriteBuffer;
    uint8_t *qspiReadBuffer;
};

/**
 * \name Cy_USB_AppSetupEndpDmaParamsHs
 * \details This Function will setup Endpoint and DMA related parameters for high speed
 *          device before transfer initiated.
 * \param pAppCtxt application layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
void Cy_USB_AppSetupEndpDmaParamsHs(cy_stc_usb_app_ctxt_t *pUsbApp, uint8_t *pEndpDscr);

/**
 * \name Cy_USB_AppInit
 * \brief   This function Initializes application related data structures, register callback
 *          creates task for device function.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer Context pointer
 * \param pCpuDmacBase DMAC base address
 * \param pCpuDw0Base DataWire 0 base address
 * \param pCpuDw1Base DataWire 1 base address
 * \param pHbDmaMgrCtxt HBDMA Manager Context
 * \retval None
 */
void Cy_USB_AppInit (cy_stc_usb_app_ctxt_t *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, 
                    DMAC_Type *pCpuDmacBase, DW_Type *pCpuDw0Base, DW_Type *pCpuDw1Base,
                    cy_stc_hbdma_mgr_context_t *pHbDmaMgrCtxt);

/**
 * \name Cy_USB_AppRegisterCallback
 * \brief This function will register all calback with USBD layer.
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
void Cy_USB_AppRegisterCallback(cy_stc_usb_app_ctxt_t *pAppCtxt);


/**
 * \name Cy_USB_RecvEndp0TimerCallback
 * \brief   This Function will be called when timer expires.
 *          This function posts TIMER Expiry message to bridge device.
 * \param xTimer Timer handle
 * \retval None
 */
void Cy_USB_RecvEndp0TimerCallback(void * xTimer);


/**
 * \name Cy_USB_AppSetCfgCallback=
 * \brief Callback function will be invoked by USBD when set configuration is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetCfgCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppBusResetCallback
 * \brief Callback function will be invoked by USBD when bus detects RESET
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppBusResetDoneCallback
 * \brief Callback function will be invoked by USBD when RESET is completed
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetDoneCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppBusSpeedCallback
 * \brief   Callback function will be invoked by USBD when speed is identified or
 *          speed change is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusSpeedCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppSetupCallback
 * \brief Callback function will be invoked by USBD when SETUP packet is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetupCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppSuspendCallback
 * \brief Callback function will be invoked by USBD when Suspend signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSuspendCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppResumeCallback
 * \brief Callback function will be invoked by USBD when Resume signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppResumeCallback (void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppSetIntfCallback
 * \brief Callback function will be invoked by USBD when SET_INTERFACE is  received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetIntfCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppL1SleepCallback
 * \brief Callback function will be invoked by USBD when L1 Sleep message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppL1SleepCallback(void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppL1ResumeCallback
 * \brief Callback function will be invoked by USBD when L1 Resume message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppL1ResumeCallback(void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppZlpCallback
 * \brief Callback function will be invoked by USBD when bus ZLP is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppZlpCallback(void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppSlpCallback
 * \brief Callback function will be invoked by USBD when SLP message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSlpCallback(void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_ConnectionEnable
 * \brief Function used to enable USB 2.x connection.
 * \param pAppCtxt Application context structure.
 * \retval None
 */
bool Cy_USB_ConnectionEnable(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name Cy_USB_ConnectionDisable
 * \brief Function which disables the USB 2.x connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_ConnectionDisable(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name Cy_USB_AppLightDisable
 * \brief Function which disables the USB connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_AppLightDisable(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name HbDma_Cb
 * \brief   HBDMA callback function to manage data transfers on the USB Audio Class endpoint
 * \param handle HBDMA channel handle
 * \param cy_en_hbdma_cb_type_t HBDMA channel type
 * \param pbufStat fHBDMA buffer status
 * \param userCtx user context
 * \retval None
 */
void HbDma_Cb (cy_stc_hbdma_channel_t *handle,
                          cy_en_hbdma_cb_type_t type,
                          cy_stc_hbdma_buff_status_t *pbufStat,
                          void *userCtx);

#define ASSERT_NON_BLOCK(condition, value) Cy_CheckStatus(__func__, __LINE__, condition, value, false);
/**
 * \name Cy_CheckStatus
 * \brief Function that handles prints error log
 * \param function Pointer to function
 * \param line Line number where error is seen
 * \param condition condition of failure
 * \param value error code
 * \param isBlocking blocking function
 * \return None
 */
void Cy_CheckStatus(const char *function, uint32_t line, uint8_t condition, 
                    uint32_t value, uint8_t isBlocking);

#if defined(__cplusplus)
}
#endif

#endif /* _CY_USB_APP_H_ */

/* End of File */

