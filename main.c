/***************************************************************************//**
* \file main.c
* \version 1.0
*
* \details  This is the source code for the USB QSPI Bridge Application
*
*           See \ref ./README.md ["README.md"]
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
***************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include <string.h>
#include "cy_usb_common.h"
#include "usb_bridge_device.h"
#include "cy_usb_usbd.h"
#include "usb_app.h"
#include "cy_debug.h"
#include "cy_usbd_version.h"
#include "cy_hbdma_version.h"
#include "app_version.h"

/* Select SCB interface used for UART based logging. */
#define LOGGING_SCB             (SCB4)
#define LOGGING_SCB_IDX         (4)
#define DEBUG_LEVEL             (3u)

/* Debug log related initilization */
#if DEBUG_INFRA_EN

/* RAM buffer used to hold debug log data. */
#define LOGBUF_SIZE (1024u)
uint8_t logBuff[LOGBUF_SIZE];

cy_stc_debug_config_t dbgCfg = {
    .pBuffer         = logBuff,
    .traceLvl        = DEBUG_LEVEL,
    .bufSize         = LOGBUF_SIZE,
#if USBFS_LOGS_ENABLE
    .dbgIntfce       = CY_DEBUG_INTFCE_USBFS_CDC,
#else
    .dbgIntfce       = CY_DEBUG_INTFCE_UART_SCB4,
#endif
    .printNow        = true
};

#endif /* DEBUG_INFRA_EN */

/* Global variables associated with High BandWidth DMA setup. */
cy_stc_hbdma_context_t HBW_DrvCtxt;     /* High BandWidth DMA driver context. */
cy_stc_hbdma_dscr_list_t HBW_DscrList;  /* High BandWidth DMA descriptor free list. */
cy_stc_hbdma_buf_mgr_t HBW_BufMgr;      /* High BandWidth DMA buffer manager. */
cy_stc_hbdma_mgr_context_t HBW_MgrCtxt; /* High BandWidth DMA manager context. */

/* CPU DMA register pointers. */
DMAC_Type *pCpuDmacBase;
DW_Type   *pCpuDw0Base;
DW_Type   *pCpuDw1Base;

cy_stc_usb_usbd_ctxt_t usbdCtxt;
cy_stc_usb_app_ctxt_t appCtxt;
cy_stc_usb_cal_ctxt_t hsCalCtxt;

/* Common descriptors shared across speed. */
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];

/**
 * \name SysTickIntrWrapper
 * \brief Increment tick and pass control to sys tick handler
 * \retval None
 */
void SysTickIntrWrapper (void)
{
    Cy_USBD_TickIncrement(&usbdCtxt);
}

/**
 * \name Cy_USB_HS_ISR
 * \brief Handler for USB-HS Interrupts.
 * \retval None
 */
void Cy_USB_HS_ISR(void)
{
    if (Cy_USBHS_Cal_IntrHandler(&hsCalCtxt))
    {
        //portYIELD_FROM_ISR(true); TBD
    }
}

/**
 * \name Cy_Fx2g3_InitPeripheralClocks
 * \brief Function used to enable clocks to different peripherals on the FX2G3 device.
 * \param adcClkEnable Whether to enable clock to the ADC in the USBSS block.
 * \param usbfsClkEnable Whether to enable bus reset detect clock input to the USBFS block.
 * \retval None
 */
void Cy_Fx2g3_InitPeripheralClocks (
        bool adcClkEnable,
        bool usbfsClkEnable)
{
    if (adcClkEnable) {
        /* Divide PERI clock at 75 MHz by 75 to get 1 MHz clock using 16-bit divider #1. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 1, 74);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 1);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_LVDS2USB32SS_CLOCK_SAR, CY_SYSCLK_DIV_16_BIT, 1);
    }

    if (usbfsClkEnable) {
        /* Divide PERI clock at 75 MHz by 750 to get 100 KHz clock using 16-bit divider #2. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 2, 749);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 2);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_USB_CLOCK_DEV_BRS, CY_SYSCLK_DIV_16_BIT, 2);
    }
}

/**
 * \name Cy_Fx2g3_OnResetInit
 * \details This function performs initialization that is required to enable scatter
 *          loading of data into the High BandWidth RAM during device boot-up. The FX2G3
 *          device comes up with the High BandWidth RAM disabled and hence any attempt
 *          to read/write the RAM will cause the processor to hang. The RAM needs to
 *          be enabled with default clock settings to allow scatter loading to work.
 *          This function needs to be called from Cy_OnResetUser.
 * \retval None
 */
void
Cy_Fx2g3_OnResetInit (
        void)
{
    /* Enable clk_hf4 with IMO as input. */
    SRSS->CLK_ROOT_SELECT[4] = SRSS_CLK_ROOT_SELECT_ENABLE_Msk;

    /* Enable LVDS2USB32SS IP and select clk_hf[4] as clock input. */
    MAIN_REG->CTRL = (
            MAIN_REG_CTRL_IP_ENABLED_Msk |
            (1UL << MAIN_REG_CTRL_NUM_FAST_AHB_STALL_CYCLES_Pos) |
            (1UL << MAIN_REG_CTRL_NUM_SLOW_AHB_STALL_CYCLES_Pos) |
            (3UL << MAIN_REG_CTRL_DMA_SRC_SEL_Pos));
}

/**
 * \name Cy_PrintVersionInfo
 * \brief Function to print version information to UART console.
 * \param type Type of version string.
 * \param version Version number including major, minor, patch and build number.
 * \retval None
 */
void Cy_PrintVersionInfo(const char *type, uint32_t version)
{
    char tString[32];
    uint16_t vBuild;
    uint8_t vMajor, vMinor, vPatch;
    uint8_t typeLen = strlen(type);

    vMajor = (version >> 28U);
    vMinor = ((version >> 24U) & 0x0FU);
    vPatch = ((version >> 16U) & 0xFFU);
    vBuild = (uint16_t)(version & 0xFFFFUL);

    memcpy(tString, type, typeLen);
    tString[typeLen++] = '0' + (vMajor / 10);
    tString[typeLen++] = '0' + (vMajor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vMinor / 10);
    tString[typeLen++] = '0' + (vMinor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vPatch / 10);
    tString[typeLen++] = '0' + (vPatch % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vBuild / 1000);
    tString[typeLen++] = '0' + ((vBuild % 1000) / 100);
    tString[typeLen++] = '0' + ((vBuild % 100) / 10);
    tString[typeLen++] = '0' + (vBuild % 10);
    tString[typeLen++] = '\r';
    tString[typeLen++] = '\n';
    tString[typeLen] = 0;

    Cy_Debug_AddToLog(1,"%s", tString);
}

/**
 * \name Cy_USB_VbusDetGpio_ISR
 * \brief Interrupt handler for the Vbus detect GPIO transition detection.
 * \retval None
 */
static void Cy_USB_VbusDetGpio_ISR(void)
{
    cy_stc_usbd_app_msg_t xMsg;
    /* Send VBus changed message to the main loop via the simple queue */
    xMsg.type = CY_USB_VBUS_CHANGE_INTR;

    /* Check that the message was enqueued; if it fails the app hasn't
     * finished init or the queue is full. */
    if (!sq_send_from_isr(appCtxt.xQueue, &xMsg)) {
        DBG_APP_ERR("MAIN VBUS_ISR: failed to enqueue VBUS_CHANGE (queue not ready/full)\r\n");
    }

    /* Remember that VBus change has happened and disable the interrupt. */
    appCtxt.vbusChangeIntr = true;
    Cy_GPIO_SetInterruptMask(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, 0);
}

/**
 * \name Cy_USB_RemoteWakeGpio_ISR
 * \brief Interrupt handler for the remote wake-up trigger GPIO.
 * \retval None
 */
static void Cy_USB_RemoteWakeGpio_ISR(void)
{
    uint32_t gpio_state = Cy_GPIO_Read(P13_0_PORT, P13_0_PIN);

    /* Clear the GPIO interrupt. */
    Cy_GPIO_ClearInterrupt(P13_0_PORT, P13_0_PIN);

    if (gpio_state != 0) {
    DBG_APP_INFO("USB: Remote wake request detected\r\n");
    /* Duration of signal will be taken care by HS_CAL */
    Cy_USBD_SignalRemoteWakeup(&usbdCtxt, true);
    }
}

/**
* \name Cy_Set_BasePri
* \brief This function sets the BASEPRI value for the active Cortex-M CPU core.
* \param val BASEPRI value to be set.
* \retval None
*/
void Cy_Set_BasePri(uint32_t val)
{
#if (CY_CPU_CORTEX_M4)
__asm__ __volatile__(
"MSR BASEPRI, r0\n\t"
);
#endif /* (CY_CPU_CORTEX_M4) */
}

/**
 * \name Cy_USB_USBHSInit
 * \brief Initialize USBHS block and attempt device enumeration.
 * \retval None
 */
void Cy_USB_USBHSInit(void)
{
    cy_stc_gpio_pin_config_t pinCfg;
    cy_stc_sysint_t intrCfg;
   
    /* Do all the relevant clock configuration */
    Cy_Fx2g3_InitPeripheralClocks(false, true);

    /* Unlock and then disable the watchdog. */
    Cy_WDT_Unlock();
    Cy_WDT_Disable();
    /* Enable interrupts. */
#if (CY_CPU_CORTEX_M4)
    Cy_Set_BasePri(0);
#endif /* (CY_CPU_CORTEX_M4) */
    /* Enable interrupts. */
    __enable_irq();

    memset((void *)&pinCfg, 0, sizeof(pinCfg));

    /* Configure VBus detect GPIO. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom     = HSIOM_SEL_GPIO;
    pinCfg.intEdge   = CY_GPIO_INTR_BOTH;
    /* Keep GPIO interrupt masked; app will enable it after initialization to avoid ISR races. */
    pinCfg.intMask   = 0x00UL;
    Cy_GPIO_Pin_Init(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, &pinCfg);

     /* Configure and register VBUS and GPIO interrupts. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc = VBUS_DETECT_GPIO_INTR;
    intrCfg.intrPriority = 7;
#else
    intrCfg.cm0pSrc = VBUS_DETECT_GPIO_INTR;
    intrCfg.intrSrc = NvicMux2_IRQn;
    intrCfg.intrPriority = 3;
#endif /* CY_CPU_CORTEX_M4 */
    /* Register the VBUS GPIO ISR but do not enable NVIC here. The application
     * will enable the IRQ after it has initialized queues/timers to avoid a
     * race where the ISR runs before the app is ready (no-RTOS startup). */
    Cy_SysInt_Init(&intrCfg, Cy_USB_VbusDetGpio_ISR);
    //NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Configure P13.0 pin as interrupt source to trigger remote wakeup. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom     = HSIOM_SEL_GPIO;
    pinCfg.intEdge   = CY_GPIO_INTR_RISING;
    pinCfg.intMask   = 0x01UL;
    Cy_GPIO_Pin_Init(P13_0_PORT, P13_0_PIN, &pinCfg);

    /* Register edge detect interrupt for remote wake up GPIO. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc = ioss_interrupts_gpio_dpslp_13_IRQn;
    intrCfg.intrPriority = 7;
#else
    intrCfg.cm0pSrc = ioss_interrupts_gpio_dpslp_13_IRQn;
    intrCfg.intrSrc = NvicMux3_IRQn;
    intrCfg.intrPriority = 3;
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysInt_Init(&intrCfg, Cy_USB_RemoteWakeGpio_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* For Bringup  this is required */
    MAIN_REG->CTRL = 0x81100003;

    /* Register the ISR for USBHS and enable the interrupt. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc      = usbhsdev_interrupt_u2d_active_o_IRQn;
    intrCfg.intrPriority = 4;
#else
    intrCfg.cm0pSrc      = usbhsdev_interrupt_u2d_active_o_IRQn;
    intrCfg.intrSrc      = NvicMux5_IRQn;
    intrCfg.intrPriority = 2;
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysInt_Init(&intrCfg, Cy_USB_HS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Point the SysTick vector to our wrapper which increments the USB tick. */
    Cy_SysInt_SetVector(SysTick_IRQn, SysTickIntrWrapper);
    /* Configure SysTick for 1ms ticks so Cy_USBD_GetTimerTick() advances */
    Cy_SysTick_SetClockSource(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU);
#if CY_CPU_CORTEX_M4
    Cy_SysTick_SetReload(Cy_SysClk_ClkFastGetFrequency() / 1000U);
#else
    Cy_SysTick_SetReload(Cy_SysClk_ClkSlowGetFrequency() / 1000U);
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysTick_Clear();
    Cy_SysTick_Enable();
}

/**
 * \name Cy_Bridge_HbDmaInit
 * \brief Initialize HBDMA block.
 * \retval None
 */
bool Cy_Bridge_HbDmaInit(void)
{
    cy_en_hbdma_status_t drvstat;
    cy_en_hbdma_mgr_status_t mgrstat;

    /* Initialize the HBW DMA driver layer. */
    drvstat = Cy_HBDma_Init(LVDSSS_LVDS, USB32DEV, &HBW_DrvCtxt, 0, 0);
    if (drvstat != CY_HBDMA_SUCCESS)
    {
        return false;
    }

    /* Setup a HBW DMA descriptor list. */
    mgrstat = Cy_HBDma_DscrList_Create(&HBW_DscrList, 256U);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }

    /* Initialize the DMA buffer manager. We will use 512 KB of space from 0x1C030000 onwards. */
    mgrstat = Cy_HBDma_BufMgr_Create(&HBW_BufMgr, (uint32_t *)0x1C030000UL, 0x80000UL);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }

    /* Initialize the HBW DMA channel manager. */
    mgrstat = Cy_HBDma_Mgr_Init(&HBW_MgrCtxt, &HBW_DrvCtxt, &HBW_DscrList, &HBW_BufMgr);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }
    
    /* Register the USB stack context with the HBW DMA manager. */
    Cy_HBDma_Mgr_RegisterUsbContext(&HBW_MgrCtxt, &usbdCtxt);

    /* Print HBDMA buffer-manager details if available. */
    if (HBW_MgrCtxt.pBufMgr != NULL)
    {
        Cy_Debug_AddToLog(3, "Init HBDMA: BufMgr start=0x%x regionSize=%u statusSize=%u searchPos=%u\r\n",
                          (uint32_t)HBW_MgrCtxt.pBufMgr->startAddr,
                          (uint32_t)HBW_MgrCtxt.pBufMgr->regionSize,
                          (uint32_t)HBW_MgrCtxt.pBufMgr->statusSize,
                          (uint32_t)HBW_MgrCtxt.pBufMgr->searchPos);
    }
    else
    {
        Cy_Debug_AddToLog(3, "Init HBDMA: pBufMgr=NULL\r\n");
    }
 
	{
        Cy_Debug_AddToLog(3, "Init HBDMA: pOutEpDma[1]=0x%x pOutEpDma[2]=0x%x pInEpDma[3]=0x%x\r\n",
                          (uint32_t)(uintptr_t)appCtxt.pOutEpDma[1], (uint32_t)(uintptr_t)appCtxt.pOutEpDma[2], (uint32_t)(uintptr_t)appCtxt.pInEpDma[3]);
    }

    return true;
}

/**
 * \name OutEpDma_ISR
 * \brief Handler for DMA transfer completion on OUT endpoint.
 * \retval None
 */
void
OutEpDma_ISR (uint8_t endpNum)
{
    DBG_APP_TRACE("USB: OUT Ep 0x%x DMA ISR\r\n",endpNum);
	Cy_HBDma_Mgr_HandleDW0Interrupt(appCtxt.pHbDmaMgrCtxt);
}

/**
 * \name InEpDma_ISR
 * \brief Handler for DMA transfer completion on IN endpoint.
 * \retval None
 */
void InEpDma_ISR (uint8_t endpNum)
{
    DBG_APP_TRACE("USB: IN Ep 0x%x DMA ISR\r\n",endpNum);
	Cy_HBDma_Mgr_HandleDW1Interrupt(appCtxt.pHbDmaMgrCtxt);
}

/**
 * \name Cy_USB_ConnectionEnable
 * \brief Function used to enable USB 2.x connection.
 * \param pAppCtxt Application context structure.
 * \retval None
 */
bool Cy_USB_ConnectionEnable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    DBG_APP_INFO("USB: Connection Enable\r\n");
    
    /* Try to connect device with retries */
    Cy_USBD_ConnectDevice(appCtxt.pUsbdCtxt, CY_USBD_USB_DEV_HS);
    Cy_SysLib_DelayUs(1000);
    pAppCtxt->usbConnected = true;
    appCtxt.usbConnectDone = true;

    return pAppCtxt->usbConnected;
}

/**
 * \name Cy_USB_ConnectionDisable
 * \brief Function which disables the USB 2.x connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_ConnectionDisable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    Cy_USBD_DisconnectDevice(pAppCtxt->pUsbdCtxt);
    pAppCtxt->usbConnected = false;
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
}


/**
 * \name Cy_USB_AppLightDisable
 * \brief Function which disables the USB connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_AppLightDisable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    Cy_USBD_DisconnectDevice(pAppCtxt->pUsbdCtxt);
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
}

extern void Cy_USB_GenerateConfigDescriptor(void);

/**
 * \name main
 * \brief Entry to the application.
 * \retval None
 */
int main(void)
{
    pCpuDmacBase = ((DMAC_Type *)DMAC_BASE);
    pCpuDw0Base = ((DW_Type *)DW0_BASE);
    pCpuDw1Base = ((DW_Type *)DW1_BASE);

    /* Initialize the PDL driver library and set the clock variables. */
    /* Note: All FX devices, share a common configuration structure. */
    Cy_PDL_Init(&cy_deviceIpBlockCfgFX3G2);

    /* Initialize the device and board peripherals */
    cybsp_init();

    /* Disable ECO to avoid overwriting crystal settings */
    Cy_SysClk_EcoDisable();

    /* Initialize the PDL and register ISR for USB block. */
    Cy_USB_USBHSInit();

#if DEBUG_INFRA_EN
#if !USBFS_LOGS_ENABLE
    /* Initialize the UART for logging. */
    InitUart(LOGGING_SCB_IDX);
#endif /* USBFS_LOGS_ENABLE */

    /*
     * Initialize the logger module. We are using a blocking print option which will
     * output the messages immediately without buffering.
     */
    Cy_Debug_LogInit(&dbgCfg);

    /* Create task for printing logs in no-RTOS mode, flush once. */
    Cy_SysLib_Delay(500);
    Cy_Debug_AddToLog(1, "********** FX2G3: USB-QSPI bridge application ********** \r\n");
    /* One-time flush */
    Cy_Debug_PrintLog();

    /* Print application, USBD stack and HBDMA version information. */
    Cy_PrintVersionInfo("APP_VERSION: ", APP_VERSION_NUM);
    Cy_PrintVersionInfo("USBD_VERSION: ", USBD_VERSION_NUM);
    Cy_PrintVersionInfo("HBDMA_VERSION: ", HBDMA_VERSION_NUM);

#endif /* DEBUG_INFRA_EN */

    memset((void *)&usbdCtxt, 0, sizeof(cy_stc_usb_usbd_ctxt_t));
    memset((void *)&hsCalCtxt, 0, sizeof(cy_stc_usb_cal_ctxt_t));
    memset((void *)&appCtxt, 0, sizeof(cy_stc_usb_app_ctxt_t));

    /* Store IP base address in CAL context. */
    hsCalCtxt.pCalBase = MXS40USBHSDEV_USBHSDEV;
    hsCalCtxt.pPhyBase = MXS40USBHSDEV_USBHSPHY;

    /* Link CAL and USBD contexts so CAL driver can reference the USBD context
     */
    hsCalCtxt.pUsbdCtxt = &usbdCtxt;
    usbdCtxt.pCalCtxt = &hsCalCtxt;

    Cy_SysLib_Delay(500);

    /* Initialize the HbDma IP and DMA Manager */
    Cy_Bridge_HbDmaInit();
    DBG_APP_INFO("HbDmaInit done\r\n");

    /* Initialize the USBD layer */
    Cy_USB_USBD_Init(&appCtxt, &usbdCtxt, pCpuDmacBase, &hsCalCtxt,NULL, &HBW_MgrCtxt);
    DBG_APP_INFO("USBD_Init done\r\n");

    /* Enable stall cycles between back-to-back AHB accesses to high bandwidth RAM. */
    MAIN_REG->CTRL = (MAIN_REG->CTRL & 0xF00FFFFFUL) | 0x09900000UL;

    /* Generate USB descriptors */
    Cy_USB_GenerateConfigDescriptor();

    /* Register USB descriptors with the stack. */
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_DEVICE_DSCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_BOS_DSCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_DEVICE_QUAL_DSCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_CONFIG_DSCR, 0, (uint8_t *)CyFxUSBHSConfigDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_FS_CONFIG_DSCR, 0, (uint8_t *)CyFxUSBFSConfigDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 2, (uint8_t *)CyFxUSBProductDscr);

    /* Clear VBus present status. */
    appCtxt.vbusPresent    = false;
    appCtxt.usbConnectDone = false;

    /* Initialize the application and create bridge device thread. */
    Cy_USB_AppInit(&appCtxt, &usbdCtxt, pCpuDmacBase, pCpuDw0Base, pCpuDw1Base, &HBW_MgrCtxt);
    /* If VBUS is present at boot, enable USB connection after a short delay. */
    if (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE) {
        appCtxt.vbusPresent = true;
        DBG_APP_INFO("CM4: VBUS seen at boot; delaying 100 ms before connect (no-RTOS immediate path)\r\n");
        /* Allow CAL/PHY to stabilize before attempting connect in no-RTOS. */
        Cy_SysLib_Delay(100);
        (void)Cy_USB_ConnectionEnable(&appCtxt);
    }

    DBG_APP_INFO("Scheduler start done\r\n");

    Cy_USB_BridgeDeviceTaskHandler(&appCtxt);

    /* Program not expected to reach here */
    for(;;);

    return 0;
}

/**
 * \name Cy_OnResetUser
 * \details Init function which is executed before the load regions in RAM are updated.
 *          The High BandWidth subsystem needs to be enable here to allow variables
 *          placed in the High BandWidth SRAM to be updated.
 * \retval None
 */
void Cy_OnResetUser(void)
{
    Cy_Fx2g3_OnResetInit();
}

/* [] END OF FILE */
