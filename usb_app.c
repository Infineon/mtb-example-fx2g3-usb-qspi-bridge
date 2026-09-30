/***************************************************************************//**
* \file usb_app.c
* \version 1.0
*
* \details Implements the USB data handling part of the FX2G3 USB QSPI Bridge application.
*
*********************************************************************************
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

#include "cy_pdl.h"
#include "cy_device.h"
#include "simple_queue.h"
#include "cy_pdl.h"
#include "cy_device.h"
#include "cy_usb_common.h"
#include "cy_usbhs_cal_drv.h"
#include "cy_usb_usbd.h"
#include "usb_bridge_device.h"
#include "usb_app.h"
#include "cy_debug.h"
#include "cy_usbhs_dw_wrapper.h"

/* Extern functions */
extern void OutEpDma_ISR(uint8_t endpNumber);
extern void InEpDma_ISR(uint8_t endpNumber);

/* Global functions */
cy_israddress GetEPInDmaIsr(uint8_t epNum);
cy_israddress GetEPOutDmaIsr(uint8_t epNum);

/* Global variables */
uint32_t Ep0TestBuffer[1024U] __attribute__ ((aligned (32)));
volatile uint16_t pktType = 0;
volatile uint16_t pktLength = 1024;

/**
 * \name Cy_USB_AppInitCpuDmaIntr
 * \brief   Function to register an ISR for a USB endpoint DMA channel and enable
 *          the interrupt.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \param userIsr user provided ISR function pointer.
 * \retval None
 */
void 
Cy_USB_AppInitCpuDmaIntr (uint32_t endpNum, cy_en_usb_endp_dir_t endpDir,
                          cy_israddress userIsr)
{
    cy_stc_sysint_t intrCfg;
    
    DBG_APP_TRACE("USB: Init DMA Interrupt endp:%d dir:%d core:%s\r\n", 
                  endpNum, endpDir, 
#if CY_CPU_CORTEX_M4
                  "CM4"
#else
                  "CM0P"
#endif
                  );

    if ((endpNum == 0x00) && (endpDir == CY_USB_ENDP_DIR_OUT)) {
        /* To make "RcevEndp0Data" non blocking, register ISR */
#if (!CY_CPU_CORTEX_M4)
        intrCfg.intrPriority = 3;
        intrCfg.intrSrc = NvicMux6_IRQn;
        /* DW0 channels 0 onwards are used for OUT endpoints. */
        intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dmac_1_IRQn);

#else
        /* Only for OUT transfer interrupt registration required. */
        intrCfg.intrPriority = 5;
        intrCfg.intrSrc = (IRQn_Type)(cpuss_interrupts_dmac_1_IRQn);
#endif

       if (userIsr != NULL)  {
            /* If an ISR is provided, register it and enable the interrupt. */
            Cy_SysInt_Init(&intrCfg, userIsr);
            NVIC_EnableIRQ(intrCfg.intrSrc);
        } else {
            /* ISR is NULL. Disable the interrupt. */
            NVIC_DisableIRQ(intrCfg.intrSrc);
        }

    }

    if ((endpNum > 0) && (endpNum < CY_USB_NUM_ENDP_CONFIGURED)) {
        DBG_APP_TRACE("USB: Registering ISR for endp:%d endpDir %d \r\n",endpNum,endpDir);
#if (!CY_CPU_CORTEX_M4)
        if (endpDir == CY_USB_ENDP_DIR_IN) {
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux4_IRQn;
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw1_0_IRQn + endpNum);
        } else {
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux1_IRQn;
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw0_0_IRQn + endpNum);
        }
#else
        intrCfg.intrPriority = 5;
        if (endpDir == CY_USB_ENDP_DIR_IN) {
            DBG_APP_TRACE("USB: DIR-IN \r\n");
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.intrSrc =
                          (IRQn_Type)(cpuss_interrupts_dw1_0_IRQn + endpNum);
        } else {
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            DBG_APP_TRACE("USB: DIR-OUT \r\n");
            intrCfg.intrSrc =
                          (IRQn_Type)(cpuss_interrupts_dw0_0_IRQn + endpNum);
        }
#endif /* (CY_CPU_CORTEX_M4) */

        if (userIsr != NULL)  {
            /* If an ISR is provided, register it and enable the interrupt. */
            DBG_APP_TRACE("USB: Registering ISR \r\n");
            Cy_SysInt_Init(&intrCfg, userIsr);
            NVIC_EnableIRQ(intrCfg.intrSrc);
        } else {
            /* ISR is NULL. Disable the interrupt. */
            DBG_APP_TRACE("USB: Disabling ISR\r\n");
            NVIC_DisableIRQ(intrCfg.intrSrc);
        }
    }

}   /* end of function. */

/**
 * \name Cy_USB_AppConfigureEndp
 * \brief   This Function is used by application to configure endpoints after set
 *          configuration. This function should be used for all endpoints except endp0.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
static void
Cy_USB_AppConfigureEndp (cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, uint8_t *pEndpDscr)
{
    cy_stc_usb_endp_config_t endpConfig;
    cy_en_usb_endp_dir_t endpDirection;
    bool valid;
    uint32_t endpType;
    uint32_t endpNum, dir;
    uint16_t maxPktSize;
    uint32_t isoPkts = 0x00;
    uint8_t burstSize = 0x00;
    uint8_t maxStream = 0x00;
    uint8_t interval = 0x00;

    /* If it is not endpoint descriptor then return */
    if (!Cy_USBD_EndpDscrValid(pEndpDscr)) {
        DBG_APP_ERR("USB: EndpDscrNotValid \r\n");
        return;
    }
    
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNum, &maxPktSize, &dir);
    
    if (dir) {
        DBG_APP_TRACE("USB: DIR-IN endpNum:%d \n", endpNum);
        endpDirection = CY_USB_ENDP_DIR_IN;
    } else {
        DBG_APP_TRACE("USB: DIR-OUT endpNum:%d \n", endpNum);
        endpDirection = CY_USB_ENDP_DIR_OUT;
    }
    
    Cy_USBD_GetEndpType(pEndpDscr, &endpType);

    if ((CY_USB_ENDP_TYPE_ISO == endpType) ||
        (CY_USB_ENDP_TYPE_INTR == endpType)) {
        /*
         * The ISOINPKS setting in the USBHS register is the actual
         * packets per microframe value.
         */
        isoPkts = 
        ((*((uint8_t *)(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_MAX_PKT + 1)) & CY_USB_ENDP_ADDL_XN_MASK)
        >> CY_USB_ENDP_ADDL_XN_POS) + 1;

    }

    valid = 0x01;
    Cy_USBD_GetEndpInterval(pEndpDscr, &interval);

    /* Prepare endpointConfig parameter. */
    endpConfig.endpType = (cy_en_usb_endp_type_t)endpType;
    endpConfig.endpDirection = endpDirection;
    endpConfig.valid = valid;
    endpConfig.endpNumber = endpNum;
    endpConfig.maxPktSize = (uint32_t)maxPktSize;
    endpConfig.isoPkts = isoPkts;
    endpConfig.burstSize = burstSize;
    endpConfig.streamID = (maxStream & 0x1F);
    endpConfig.interval = interval;
    
     if (CY_USB_ENDP_TYPE_ISO == endpType)
     {
		 endpConfig.allowNakTillDmaRdy = false;
	 }else {
		/*
	     * allowNakTillDmaRdy = true means device will send NAK
	     * till DMA setup is ready. This field is applicable to only
	     * ingress direction ie OUT transfer/OUT endpoint.
	     * For Egress ie IN transfer, this field is ignored.
	     */
	    endpConfig.allowNakTillDmaRdy = TRUE;

	}

    Cy_USB_USBD_EndpConfig(pUsbdCtxt, endpConfig);
    Cy_USBD_ResetEndp(pUsbdCtxt, endpNum, endpDirection, false);
    Cy_SysLib_Delay(1);

    return;
}  /* end of function */

/**
 * \name Cy_USB_AppDestroyEndpDmaParamsHs
 * \details This Function de-couple endpoint and DMA channel for HS controller. It also
 *          destroys DMA channel.
 * \param pAppCtxt application layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
static void
Cy_USB_AppDestroyEndpDmaParamsHs (cy_stc_usb_app_ctxt_t *pUsbApp,
                                  uint8_t *pEndpDscr)
{
    uint32_t endpNum, endpDir;
    uint16_t maxPktSize;

    DBG_APP_TRACE("USB: Destroy DMA Params \r\n");
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNum, &maxPktSize, &endpDir);
 

	if ((endpNum > 0 &&  endpNum < CY_USB_NUM_ENDP_CONFIGURED) && (endpDir != 0))
	{
	    /* Stop and destroy the high bandwidth DMA channel if present. To be done before AppInit is called. */
	    if (pUsbApp->pInEpDma[endpNum] != NULL)
	    {
	        DBG_APP_TRACE("Destroying EP IN HBDMA channel \r\n");
	        Cy_HBDma_Channel_Reset(pUsbApp->pInEpDma[endpNum]);
	        Cy_HBDma_Channel_Disable(pUsbApp->pInEpDma[endpNum]);
	        Cy_HBDma_Channel_Destroy(pUsbApp->pInEpDma[endpNum]);
	        pUsbApp->pInEpDma[endpNum] = NULL;
	    }
    }
    
    if ((endpNum > 0 &&  endpNum < CY_USB_NUM_ENDP_CONFIGURED) && (endpDir == 0))
	{
	    /* Stop and destroy the high bandwidth DMA channel if present. To be done before AppInit is called. */
	    if (pUsbApp->pOutEpDma[endpNum] != NULL)
	    {
	        DBG_APP_TRACE("Destroying EP OUT HBDMA channel \r\n");
	        Cy_HBDma_Channel_Reset(pUsbApp->pOutEpDma[endpNum]);
	        Cy_HBDma_Channel_Disable(pUsbApp->pOutEpDma[endpNum]);
	        Cy_HBDma_Channel_Destroy(pUsbApp->pOutEpDma[endpNum]);
	        pUsbApp->pOutEpDma[endpNum] = NULL;
	    }
    }

    Cy_USBD_FlushEndp(pUsbApp->pUsbdCtxt, endpNum,
                      endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT));
    Cy_USBD_ResetEndp(pUsbApp->pUsbdCtxt, endpNum,
                      endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT), false);
    Cy_USB_USBD_EndpSetClearStall(pUsbApp->pUsbdCtxt, (cy_en_usb_endp_dir_t)endpNum, 
    				  endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT), false);
    Cy_USBD_EnableEndp(pUsbApp->pUsbdCtxt, endpNum,
                       endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT),
                       false); 
                                    
   Cy_USB_AppInitCpuDmaIntr(endpNum, endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT),NULL);
    return;
}   /* end of function() */


/**
 * \name Cy_USB_AppHandleSetCfgCommon
 * \brief Function handles common portion of set configuration call back to application.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer  context pointer.
 * \param pMsg pointer to message coming from lower layer.
 * \retval  CY_USB_APP_STATUS_SUCCESS in case of set Config handle without error.
 *          CY_USB_APP_STATUS_FAILURE in all other case.
 */
static cy_en_usb_app_ret_code_t
Cy_USB_AppHandleSetCfgCommon (cy_stc_usb_app_ctxt_t *pAppCtxt,
                              cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                              cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_setup_req_t *pSetupReq;
    cy_en_usb_speed_t devSpeed;
    uint8_t *pActiveCfg, *pIntfDscr, *pEndpDscr;
    uint8_t index, numOfIntf, numOfEndp;

    DBG_APP_TRACE("USB: Set config common \r\n");

    devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);

    pAppCtxt->devSpeed = devSpeed;

    /* Get setup packet from message. */
    pSetupReq = (cy_stc_usb_setup_req_t *)(&(pMsg->data[0]));
    /* Disable DMA if Set Config request on Config index 0 is received */
    if(pSetupReq->wValue == 0) {
        DBG_APP_ERR("USB: Set CFG 0\r\n");
        return CY_USB_APP_STATUS_FAILURE;
    }

    /* Enable Datawire. This application uses data wire for data transfer. */
    Cy_DMA_Enable(pAppCtxt->pCpuDw0Base);
    Cy_DMA_Enable(pAppCtxt->pCpuDw1Base);

    pActiveCfg = Cy_USB_USBD_GetActiveCfgDscr(pUsbdCtxt);
    if (!pActiveCfg) {
        /* Set config should be called when active config value > 0x00. */
        DBG_APP_ERR("USB: Active config not available\r\n");
        return CY_USB_APP_STATUS_FAILURE;
    }

    numOfIntf = Cy_USBD_FindNumOfIntf(pActiveCfg);
    if (numOfIntf == 0x00) {
        DBG_APP_ERR("USB: No. of intf-0x00\r\n");
        return CY_USB_APP_STATUS_FAILURE;
    }

    for (index = 0x00; index < numOfIntf; index++) {
        /* During Set Config command always altSetting 0 will be active. */
        pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, index, 0x00);
        if (pIntfDscr == NULL) {
            DBG_APP_ERR("USB: NULL intf dscr\r\n");
            return CY_USB_APP_STATUS_FAILURE;
        }

        numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
        if (numOfEndp == 0x00) {
            /* If current interface has 0 endpoint then move to next intf */
            DBG_APP_ERR("USB: No. of endpoint-0x00\r\n");
            continue;
        }

        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00) {
            /* first cleanup all channels related info then configure new. */
            Cy_USB_AppDestroyEndpDmaParamsHs(pAppCtxt, pEndpDscr);
            Cy_USB_AppSetupEndpDmaParamsHs(pAppCtxt, pEndpDscr);
            numOfEndp--;

            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
            
        }
    }
    
    DBG_APP_TRACE("USB: All endpoints configured\r\n");

    pAppCtxt->prevDevState = CY_USB_DEVICE_STATE_CONFIGURED;
    pAppCtxt->devState = CY_USB_DEVICE_STATE_CONFIGURED;

    return CY_USB_APP_STATUS_SUCCESS;
}   /* end of function. */

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
void
Cy_USB_AppInit (cy_stc_usb_app_ctxt_t *pAppCtxt,
                cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, DMAC_Type *pCpuDmacBase,
                DW_Type *pCpuDw0Base, DW_Type *pCpuDw1Base, cy_stc_hbdma_mgr_context_t *pHbDmaMgrCtxt)
{
    uint32_t index;
    cy_stc_app_endp_dma_set_t *pEndpInDma;
    cy_stc_app_endp_dma_set_t *pEndpOutDma;

#if CY_CPU_CORTEX_M4
    DBG_APP_INFO("USB: AppInit - Running on CM4 core\r\n");
#else
    DBG_APP_INFO("USB: AppInit - Running on CM0P core\r\n");
#endif
	

    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
    pAppCtxt->prevDevState = CY_USB_DEVICE_STATE_DISABLE;

    /*
     * Initially application sees device speed as USBFS and during set
     * configuration application will update actual device speed.
     */
    pAppCtxt->devSpeed = CY_USBD_USB_DEV_FS;
    pAppCtxt->devAddr = 0x00;
    pAppCtxt->activeCfgNum = 0x00;
    pAppCtxt->currentAltSetting = 0x00;
    pAppCtxt->enumMethod = CY_USB_ENUM_METHOD_FAST;

    for (index = 0x01; index < CY_USB_NUM_ENDP_CONFIGURED; index++) {
        pEndpInDma = &(pAppCtxt->endpInDma[index]);

        memset((void *)pEndpInDma, 0, sizeof(cy_stc_app_endp_dma_set_t));
        pEndpInDma->channel = index;
        pEndpInDma->firstRqtDone = false;

        /* Time to handle OUT endpoints. */
        pEndpOutDma = &(pAppCtxt->endpOutDma[index]);

        memset((void *)pEndpOutDma, 0, sizeof(cy_stc_app_endp_dma_set_t));
        pEndpOutDma->channel = index;
        pEndpOutDma->firstRqtDone = false;
    }

    pAppCtxt->pCpuDmacBase = pCpuDmacBase;
    pAppCtxt->pCpuDw0Base = pCpuDw0Base;
    pAppCtxt->pCpuDw1Base = pCpuDw1Base;
    pAppCtxt->pUsbdCtxt = pUsbdCtxt;
    pAppCtxt->pHbDmaMgrCtxt = pHbDmaMgrCtxt;
    pAppCtxt->dataXferIntrEnabled = false;

    /*
     * Callbacks registered with USBD layer. These callbacks will be called
     * based on appropriate event.
     */
    Cy_USB_AppRegisterCallback(pAppCtxt);

    if (!(pAppCtxt->firstInitDone)) {

    /* No FreeRTOS - using polling mode only */
    pAppCtxt->xQueue = sq_create(CY_USB_BRIDGE_DEVICE_MSG_SIZE, CY_USB_BRIDGE_DEVICE_MSG_QUEUE_SIZE);
    if (pAppCtxt->xQueue == 0) {
        DBG_APP_ERR("Bridge: simple queue create failed\r\n");
        return;
    }
    DBG_APP_TRACE("Bridge: simple queue Created \r\n");

    pAppCtxt->firstInitDone = 0x01;
    }

    /* Zero out the EP0 test buffer. */
    memset((uint8_t *)Ep0TestBuffer, 0, sizeof(Ep0TestBuffer));

    DBG_APP_INFO("AppInit: Enabling VBUS GPIO mask\r\n");
    Cy_GPIO_SetInterruptMask(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, 1);

    /* Enable the appropriate IRQ for CM4 and the NVIC Mux for CM0+. */
#if CY_CPU_CORTEX_M4
    NVIC_EnableIRQ(VBUS_DETECT_GPIO_INTR);
#else
    NVIC_EnableIRQ(NvicMux2_IRQn);
#endif

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppRegisterCallback
 * \brief This function will register all calback with USBD layer.
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
void
Cy_USB_AppRegisterCallback (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt = pAppCtxt->pUsbdCtxt;

    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESET, 
                                               Cy_USB_AppBusResetCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESET_DONE, 
                                           Cy_USB_AppBusResetDoneCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_BUS_SPEED, 
                                               Cy_USB_AppBusSpeedCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SETUP, 
                                                  Cy_USB_AppSetupCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SUSPEND, 
                                                Cy_USB_AppSuspendCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESUME, 
                                                 Cy_USB_AppResumeCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SET_CONFIG, 
                                                 Cy_USB_AppSetCfgCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SET_INTF, 
                                                Cy_USB_AppSetIntfCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_L1_SLEEP,
                                                Cy_USB_AppL1SleepCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_L1_RESUME,
                                                Cy_USB_AppL1ResumeCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_ZLP,
                                                Cy_USB_AppZlpCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SLP,
                                                Cy_USB_AppSlpCallback);
    return;
}   /* end of function. */


/**
 * \name Cy_USB_AppSetupEndpDmaParamsHs
 * \details This Function will setup Endpoint and DMA related parameters for high speed
 *          device before transfer initiated.
 * \param pAppCtxt application layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
void
Cy_USB_AppSetupEndpDmaParamsHs (cy_stc_usb_app_ctxt_t *pUsbApp,
                                uint8_t *pEndpDscr)
{
    uint32_t endpNum;
    uint16_t maxPktSize = 0x00;
    uint32_t  endpDir;
    cy_stc_hbdma_chn_config_t dmaConfig;
    cy_en_hbdma_mgr_status_t mgrStat;

    DBG_APP_TRACE("USB: Setup Dma params \r\n");

    /* Configure endpoint in USB-IP */
    Cy_USB_AppConfigureEndp(pUsbApp->pUsbdCtxt, pEndpDscr);
    
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNum, &maxPktSize, &endpDir);

    if ((endpNum > 0 &&  endpNum < CY_USB_NUM_ENDP_CONFIGURED) && (endpDir != 0)) {
		
		 if (pUsbApp->pInEpDma[endpNum] != NULL) {
            DBG_APP_ERR("USB: IN channel already created\r\n");
            return;
        }
        
#if APP_SRC_SNK_EN
        dmaConfig.size         = CY_USB_MAX_DATA_BUFFER_SIZE;                       /* DMA Buffer size in bytes */
        dmaConfig.prodBufSize  = CY_USB_MAX_DATA_BUFFER_SIZE;                       /* DMA Buffer Count */
 #else
        dmaConfig.size         = maxPktSize;                       					/* DMA Buffer size in bytes */
        dmaConfig.prodBufSize  = maxPktSize;                                        /* DMA Buffer Count */
 #endif /* APP_SRC_SNK_EN */
 
        dmaConfig.count        = CY_IFX_BRIDGE_LOPBACK_MAX_QUEUE_SIZE;		
        dmaConfig.prodHdrSize  = 0;                         						/* No header being added. */
        dmaConfig.eventEnable  = 0;
        dmaConfig.intrEnable   = LVDSSS_LVDS_ADAPTER_DMA_SCK_INTR_CONSUME_EVENT_Msk;
        dmaConfig.bufferMode   = false;                     						/* DMA buffer mode disabled */
        dmaConfig.chType       = CY_HBDMA_TYPE_MEM_TO_IP;   						/* DMA Channel type: from MEM to USB EG IP. */
        dmaConfig.prodSckCount = 1;                         						/* No. of producer sockets */
        dmaConfig.prodSck[0]   = CY_HBDMA_VIRT_SOCKET_WR;
        dmaConfig.prodSck[1]   = (cy_hbdma_socket_id_t)0;   						/* Producer Socket ID: None */
        dmaConfig.consSckCount = 1;                         						/* No. of consumer Sockets */
        dmaConfig.consSck[1]   = (cy_hbdma_socket_id_t)0;   						/* Consumer Socket ID: None */
        dmaConfig.cb           = HbDma_Cb;             	 							/* HB-DMA callback */
        dmaConfig.userCtx      = (void *)(pUsbApp);         						/* Pass the application context as user context. */
 		dmaConfig.consSck[0]    = (cy_hbdma_socket_id_t)(CY_HBDMA_USBHS_IN_EP_00 + endpNum);
        dmaConfig.usbMaxPktSize = maxPktSize;


 		mgrStat = Cy_HBDma_Channel_Create(pUsbApp->pUsbdCtxt->pHBDmaMgr,
                &(pUsbApp->endpInDma[endpNum].hbDmaChannel),
                &dmaConfig);
        if (mgrStat != CY_HBDMA_MGR_SUCCESS) {
            DBG_APP_ERR("USB: IN channel create failed for Ep 0x%x status 0x%x\r\n",endpNum, mgrStat);
            return;
        }
        else {
            DBG_APP_TRACE("USB: IN channel create SUCCESS for Ep 0x%x \r\n",endpNum);
        }

        /* Store the pointer to the DMA channel */
        pUsbApp->pInEpDma[endpNum] = &(pUsbApp->endpInDma[endpNum].hbDmaChannel);
        DBG_APP_TRACE("USB: pInEpDma[%d]=%p (endpInObj=%p) \r\n", (int)endpNum, (void*)pUsbApp->pInEpDma[endpNum], (void*)&(pUsbApp->endpInDma[endpNum].hbDmaChannel));
        
        /* Register the interrupt handler for this endpoint's DMA channel */
        Cy_USB_AppInitCpuDmaIntr(endpNum, CY_USB_ENDP_DIR_IN, GetEPInDmaIsr(endpNum));
        
    }
    
	if ((endpNum > 0 &&  endpNum < CY_USB_NUM_ENDP_CONFIGURED) && (endpDir == 0)) 
	{
		
		 if (pUsbApp->pOutEpDma[endpNum] != NULL) {
            DBG_APP_ERR("USB: OUT channel already created\r\n");
            return;
        }

#if APP_SRC_SNK_EN
        dmaConfig.size         = CY_USB_MAX_DATA_BUFFER_SIZE;                       /* DMA Buffer size in bytes */
        dmaConfig.prodBufSize  = CY_USB_MAX_DATA_BUFFER_SIZE;                       /* DMA Buffer Count */
 #else
        dmaConfig.size         = maxPktSize;                       					/* DMA Buffer size in bytes */
        dmaConfig.prodBufSize  = maxPktSize;                                        /* DMA Buffer Count */
 #endif /* APP_SRC_SNK_EN */
 
        dmaConfig.count        = CY_IFX_BRIDGE_LOPBACK_MAX_QUEUE_SIZE;
        dmaConfig.prodHdrSize  = 0;                         						/* No header being added. */
        dmaConfig.eventEnable  = 0;
        dmaConfig.intrEnable   = LVDSSS_LVDS_ADAPTER_DMA_SCK_INTR_PRODUCE_EVENT_Msk;
        dmaConfig.bufferMode   = false;                     						/* DMA buffer mode disabled */
        dmaConfig.chType       = CY_HBDMA_TYPE_IP_TO_MEM;   						/* DMA Channel type: from MEM to USB EG IP. */
        dmaConfig.prodSckCount = 1;                         						/* No. of producer sockets */
        dmaConfig.prodSck[0]   = (cy_hbdma_socket_id_t)(CY_HBDMA_USBHS_OUT_EP_00 + endpNum);;
        dmaConfig.prodSck[1]   = (cy_hbdma_socket_id_t)0;   						/* Producer Socket ID: None */
        dmaConfig.consSckCount = 1;                         						/* No. of consumer Sockets */
        dmaConfig.consSck[1]   = (cy_hbdma_socket_id_t)0;   						/* Consumer Socket ID: None */
        dmaConfig.cb           = HbDma_Cb;              							/* HB-DMA callback */
        dmaConfig.userCtx      = (void *)(pUsbApp);         						/* Pass the application context as user context. */
 		dmaConfig.consSck[0]    = CY_HBDMA_VIRT_SOCKET_WR;
        dmaConfig.usbMaxPktSize = maxPktSize;


 		mgrStat = Cy_HBDma_Channel_Create(pUsbApp->pUsbdCtxt->pHBDmaMgr,
                &(pUsbApp->endpOutDma[endpNum].hbDmaChannel),
                &dmaConfig);
        if (mgrStat != CY_HBDMA_MGR_SUCCESS) {
            DBG_APP_ERR("USB: OUT channel create failed for Ep 0x%x status 0x%x\r\n",endpNum, mgrStat);
            return;
        }
        else {
            DBG_APP_TRACE("USB: OUT channel create SUCCESS for Ep 0x%x\r\n",endpNum);
        }

        /* Store the pointer to the DMA channel */
        pUsbApp->pOutEpDma[endpNum] = &(pUsbApp->endpOutDma[endpNum].hbDmaChannel);
        DBG_APP_TRACE("USB: pOutEpDma[%d]=%p (endpOutObj=%p)\r\n", (int)endpNum, (void*)pUsbApp->pOutEpDma[endpNum], (void*)&(pUsbApp->endpOutDma[endpNum].hbDmaChannel));
        
        /* Register the interrupt handler for this endpoint's DMA channel */
        Cy_USB_AppInitCpuDmaIntr(endpNum, CY_USB_ENDP_DIR_OUT, GetEPOutDmaIsr(endpNum));
        
    }

    return;
}   /* end of function  */

/**
 * \name Cy_USB_RecvEndp0TimerCallback
 * \brief   This Function will be called when timer expires.
 *          This function posts TIMER Expiry message to bridge device.
 * \param xTimer Timer handle
 * \retval None
 */
void
Cy_USB_RecvEndp0TimerCallback (void * xTimer)
{
    (void)xTimer; /* TBD: No-op in no-RTOS */
    return;
}

/**
 * \name Cy_USB_AppSetCfgCallback=
 * \brief Callback function will be invoked by USBD when set configuration is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppSetCfgCallback (void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                          cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;
    cy_stc_usbd_app_msg_t xMsg;
    cy_en_usb_speed_t devSpeed;
    cy_en_usb_app_ret_code_t retCode;

    DBG_APP_TRACE("USB: Set Configuration CB \r\n");

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);
    pUsbApp->devSpeed = devSpeed;
    pUsbApp->prevDevState = pUsbApp->devState;
    pUsbApp->devState = CY_USB_DEVICE_STATE_CONFIGURED;

    retCode = Cy_USB_AppHandleSetCfgCommon(pUsbApp, pUsbdCtxt, pMsg);
    if (retCode == CY_USB_APP_STATUS_FAILURE) {
        DBG_APP_TRACE("USB: Set Cfg Common Failed status %x\r\n",retCode);
        return;
    }


    /* Set timer expiry to 5 second (no-RTOS: No timers) */
    pUsbApp->endp0OuttimerExpiry = 5000;

    /* first send message to configure channel and then transfer data. */
    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_SETUP_DATA_XFER;
    (void)sq_send_from_isr(pUsbApp->xQueue, &(xMsg));

    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_START_DATA_XFER;
    (void)sq_send_from_isr(pUsbApp->xQueue, &(xMsg));


#if LPM_ENABLE
    /* Schedule LPM enable after 80 milliseconds. */
    pUsbApp->isLpmEnabled  = false;
    pUsbApp->lpmEnableTime = Cy_USBD_GetTimerTick() + 80;
    DBG_APP_INFO("Enabling LPM transitions\r\n");

#else
    DBG_APP_INFO("USB: Disabling LPM transitions\r\n");
    pUsbApp->lpmEnableTime = 0;
    Cy_USBD_LpmDisable(pUsbApp->pUsbdCtxt);
#endif /* LPM_ENABLE */

    return;
}   /* end of function */

/**
 * \name Cy_USB_AppBusResetCallback
 * \brief Callback function will be invoked by USBD when bus detects RESET
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    DBG_APP_TRACE("USB: Reset CB\r\n");
    
    /*
     * USBD layer takes care of reseting its own data structure as well as
     * takes care of calling CAL reset APIs. Application needs to take care
     * of reseting its own data structure as well as "device function".
     */
    Cy_USB_AppInit(pUsbApp, pUsbdCtxt, pUsbApp->pCpuDmacBase,
                   pUsbApp->pCpuDw0Base, pUsbApp->pCpuDw1Base, pUsbApp->pHbDmaMgrCtxt);
    pUsbApp->devState = CY_USB_DEVICE_STATE_RESET;
    pUsbApp->prevDevState = CY_USB_DEVICE_STATE_RESET;

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppBusResetDoneCallback
 * \brief Callback function will be invoked by USBD when RESET is completed
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetDoneCallback(void *pAppCtxt,
                                    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                    cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->devState = CY_USB_DEVICE_STATE_DEFAULT;
    pUsbApp->prevDevState = pUsbApp->devState;
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppBusSpeedCallback
 * \brief   Callback function will be invoked by USBD when speed is identified or
 *          speed change is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusSpeedCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->devState = CY_USB_DEVICE_STATE_DEFAULT;
    pUsbApp->devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppSetupCallback
 * \brief Callback function will be invoked by USBD when SETUP packet is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void 
Cy_USB_AppSetupCallback (void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                         cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_TRACE("USB: Setup CB\r\n");
    /*
     * cy_stc_usbd_app_msg_t xMsg;
     * Send message to setup and start data transfer.
     */
    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_CTRL_XFER_SETUP;
    xMsg.data[0] = pMsg->data[0];
    xMsg.data[1] = pMsg->data[1];

    (void)sq_send_from_isr(pUsbApp->xQueue, &(xMsg));
}   /* end of function. */

/**
 * \name Cy_USB_AppSuspendCallback
 * \brief Callback function will be invoked by USBD when Suspend signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSuspendCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                               cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->prevDevState = pUsbApp->devState;
    pUsbApp->devState = CY_USB_DEVICE_STATE_SUSPEND;
} /* end of function. */

/**
 * \name Cy_USB_AppResumeCallback
 * \brief Callback function will be invoked by USBD when Resume signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppResumeCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                              cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;
    cy_en_usb_device_state_t tempState;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    tempState = pUsbApp->devState;
    pUsbApp->devState = pUsbApp->prevDevState;
    pUsbApp->prevDevState = tempState;
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppSetIntfCallback
 * \brief Callback function will be invoked by USBD when SET_INTERFACE is  received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetIntfCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                               cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_setup_req_t *pSetupReq;
    cy_stc_usb_app_ctxt_t *pUsbApp;
    uint8_t intfNum, newAltSetting, numOfAltSetting;
    int8_t numOfEndp;
    uint8_t *pIntfDscr, *pEndpDscr;
    cy_stc_usbd_app_msg_t xMsg;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    DBG_APP_TRACE("USB: Set interface CB \r\n");

    pSetupReq = &(pUsbdCtxt->setupReq);
    /*
     * Get interface and alt setting info. If new setting same as previous
     * then take action as per spec.
     * If new alt setting came then first Unconfigure previous settings
     * and then configure new settings.
     */
    intfNum = pSetupReq->wIndex;
    newAltSetting = pSetupReq->wValue;

    DBG_APP_INFO("USB: Intf:%d currAltSet:%d NewAltSet:%d, \r\n", intfNum,
                 pUsbApp->currentAltSetting, newAltSetting);

    numOfAltSetting = Cy_USBD_GetNumOfAltSetting(pUsbdCtxt, intfNum);
    if (newAltSetting == pUsbApp->currentAltSetting) {
        if (numOfAltSetting == 0x01) {
            /*
             * Set interface request for same alt setting and only default is
             * available then send stall.
             */
            DBG_APP_INFO("USB:SameAltSetting and only default so send STALL\r\n");
            Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, 0x00,
                                          CY_USB_ENDP_DIR_IN, TRUE);
        }
        return;
    }

    /* New altSetting is different than previous one so unconfigure previous. */
    pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, intfNum, pUsbApp->currentAltSetting);
    if (pIntfDscr == NULL) {
        DBG_APP_ERR("USB: IntfDscr-Null\r\n");
        return;
    }

    /* first send stop data transfer message to stop data transfer. */
    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_STOP_DATA_XFER;
	
    (void)sq_send_from_isr(pUsbApp->xQueue,  &(xMsg));

    /* Now unconfigure things related to previous altSetting*/
    numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
    if (numOfEndp == 0x00) {
        /*
         * If previous interface does not have endpoint so no need to
         * unconfigure it. Just move to new setting.
         */
        DBG_APP_TRACE("USB:prevIntNumEp=0\r\n");
    } else {
        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);

        while (numOfEndp != 0x00) {
            Cy_USB_AppDestroyEndpDmaParamsHs(pUsbApp, pEndpDscr);
            numOfEndp--;

            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
            
        }   /* End of while */
    }

    DBG_APP_INFO("USB: Prev AltSet Unconfigured....\r\n");

    /* Now take care of different config with new alt setting. */
    DBG_APP_INFO("USB: New AltSet ConfigStarts...\r\n");
    pUsbApp->currentAltSetting = newAltSetting;
    pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, intfNum, newAltSetting);
    if (pIntfDscr == NULL) {
        DBG_APP_ERR("USB::pIntfDscrNull\r\n");
        return;
    }

    numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
    DBG_APP_TRACE("USB: New AltSet ConfigStarts for ep %d\r\n",numOfEndp);
    pUsbApp->intfAltSetEndp[intfNum][newAltSetting] = numOfEndp;
    if (numOfEndp == 0x00) {
        DBG_APP_TRACE("USB:numEp 0\r\n");
    } else {
        pUsbApp->currentAltSetting = newAltSetting;
        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00) {
            
            Cy_USB_AppSetupEndpDmaParamsHs(pAppCtxt, pEndpDscr);
            numOfEndp--;

            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
            
        }
    }

    /*
     * Send message to setup and start data transfer.
     */
	 
    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_SETUP_DATA_XFER;
    (void)sq_send_from_isr(pUsbApp->xQueue,  &(xMsg));

    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_START_DATA_XFER;
    (void)sq_send_from_isr(pUsbApp->xQueue,  &(xMsg));

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppZlpCallback
 * \brief Callback function will be invoked by USBD when bus ZLP is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppZlpCallback (void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    DBG_APP_TRACE("USB: ZLP CB\r\n");
    
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUsbApp;
    Cy_HBDma_Mgr_HandleUsbShortInterrupt(pAppCtxt->pHbDmaMgrCtxt, (pMsg->data[0] & 0x7FU), pMsg->data[1]);

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppL1SleepCallback
 * \brief Callback function will be invoked by USBD when L1 Sleep message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppL1SleepCallback (void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_TRACE("USB: L2 Sleep CB\r\n");
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUsbApp;

    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_L1_SLEEP;
    xMsg.data[0] = 0x00;
    xMsg.data[1] = 0x00;
    (void) sq_send_from_isr(pAppCtxt->xQueue, &(xMsg));

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppL1ResumeCallback
 * \brief Callback function will be invoked by USBD when L1 Resume message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppL1ResumeCallback (void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_TRACE("USB: L1 Resume CB\r\n");
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUsbApp;

    xMsg.type = CY_USB_BRIDGE_DEVICE_MSG_L1_RESUME;
    xMsg.data[0] = 0x00;
    xMsg.data[1] = 0x00;
    (void) sq_send_from_isr(pAppCtxt->xQueue, &(xMsg));

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppSlpCallback
 * \brief Callback function will be invoked by USBD when SLP message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppSlpCallback (void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    DBG_APP_TRACE("USB: SLP CB\r\n");
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUsbApp;
    Cy_HBDma_Mgr_HandleUsbShortInterrupt(pAppCtxt->pHbDmaMgrCtxt, (pMsg->data[0] & 0x7FU), pMsg->data[1]);
    return;
}   /* end of function. */

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
void Cy_CheckStatus(const char *function, uint32_t line, uint8_t condition, uint32_t value, uint8_t isBlocking)
{
    if (!condition)
    {
        /* Application failed with the error code status */
        Cy_Debug_AddToLog(1, "\r\nFunction %s failed at line %d with status = 0x%x\r\n", function, line, value);
        if (isBlocking)
        {
            /* Loop indefinitely */
            for (;;)
            {
            }
        }
    }
}
