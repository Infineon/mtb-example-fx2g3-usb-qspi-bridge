/***************************************************************************//**
* \file usb_bridge_device.h
* \version 1.0
*
* Defines the messages and constants used in the USB QSPI Bridge implementation.
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

#ifndef _CY_USB_BRIDGE_DEVICE_H_
#define _CY_USB_BRIDGE_DEVICE_H_

#include "cy_hbdma_mgr.h"
#include "cy_usb_common.h"

/* Forward declaration */
typedef struct cy_stc_usb_app_ctxt_ cy_stc_usb_app_ctxt_t;

#if defined(__cplusplus)
extern "C" {
#endif

#define CY_USB_BRIDGE_DEVICE_MSG_SETUP_DATA_XFER      (0x01)
#define CY_USB_BRIDGE_DEVICE_MSG_START_DATA_XFER      (0x02)
#define CY_USB_BRIDGE_DEVICE_MSG_STOP_DATA_XFER       (0x03)
#define CY_USB_BRIDGE_DEVICE_MSG_L1_SLEEP             (0x04)
#define CY_USB_BRIDGE_DEVICE_MSG_L1_RESUME            (0x05)
#define CY_USB_ENDP0_READ_TIMEOUT                   (0x06)
#define CY_USB_BRIDGE_DEVICE_MSG_CTRL_XFER_SETUP      (0x07)
#define CY_USB_VBUS_CHANGE_INTR                     (0x1E)
#define CY_USB_VBUS_CHANGE_DEBOUNCED                (0x1F)
#define MS_VENDOR_CODE                              (0xF0)
#define CY_USB_BRIDGE_DEVICE_MSG_QUEUE_SIZE            (16)
#define CY_USB_BRIDGE_DEVICE_MSG_SIZE                  (sizeof (cy_stc_usbd_app_msg_t))
#define CY_USB_MAX_DATA_BUFFER_SIZE                 (16384)

#define CY_IFX_BRIDGE_LOPBACK_MAX_QUEUE_SIZE          (4)
#define CY_USB_NUM_ENDP_CONFIGURED                  (6)

/* Vendor commands to control the data flow for QSPI bridge */
#define VENDOR_CMD_START_WRITE              (0xB0)  /* Start continuous write to QSPI */
#define VENDOR_CMD_STOP_WRITE               (0xB1)  /* Stop continuous write */
#define VENDOR_CMD_START_READ               (0xB2)  /* Start continuous read from QSPI */
#define VENDOR_CMD_STOP_READ                (0xB3)  /* Stop continuous read */
#define VENDOR_CMD_SMIF_STATUS              (0xB4)  /* Get QSPI bridge status */
#define VENDOR_CMD_SMIF_THROUGHPUT          (0xB5)  /* Measure QSPI throughput */
#define VENDOR_CMD_SET_SMIF_CLK_FREQ        (0xC0)  /* Set QSPI clock frequency - frequency value comes via EP2 data */

/**
 * \name QSPIWriterTask
 * \brief RTOS task to handle writing data to the QSPI interface.
 * \details This task receives USB data buffers and transmits them over QSPI
 * \param pTaskParam Pointer to application context
 */
void QSPIWriterTask(void *pTaskParam);

/**
 * \name QSPIReaderTask  
 * \brief RTOS task to handle reading data from the QSPI interface.
 * \details This task generates/reads QSPI data and sends it to USB IN endpoints
 * \param pTaskParam Pointer to application context
 */
void QSPIReaderTask(void *pTaskParam);

/**
 * \name Cy_USB_BridgeDeviceTaskHandler
 * \brief Main application task to handle USB events and control commands.
 * \param pTaskParam Pointer to application context
 */
void Cy_USB_BridgeDeviceTaskHandler(void *pTaskParam);

/**
 * \name Cy_USB_BridgeDeviceHandleCtrlSetup
 * \brief Handler for setup packets on Endpoint 0 including vendor commands.
 * \param pApp Pointer to application context
 * \param pMsg Pointer to USB message structure
 */
void Cy_USB_BridgeDeviceHandleCtrlSetup(void *pApp, cy_stc_usbd_app_msg_t *pMsg);

/**
 * \name QspiBridgeSetClockFrequency
 * \brief Set QSPI clock frequency by configuring PLL1 and routing to SMIF.
 * \param freq_mhz Desired frequency in MHz (valid range: 11 to 39)
 * \retval None
 */
void QspiBridgeSetClockFrequency (uint8_t freq_mhz);

#if defined(__cplusplus)
}
#endif

#endif /* _CY_USB_BRIDGE_DEVICE_H_ */

/* End of File */

