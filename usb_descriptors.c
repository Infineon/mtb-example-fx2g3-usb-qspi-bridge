/***************************************************************************//**
* \file usb_descriptors.c
* \version 1.0
*
* \brief Defines the USB descriptors used in the QSPI Bridge Device Application.
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

#include "cy_pdl.h"
#include "cy_usb_common.h"
#include "cy_usb_usbd.h"
#include "usb_bridge_device.h"

#define USB_DESC_ATTRIBUTES __attribute__ ((section(".descSection"), used))

/* USB 2.0 descriptors */
/* Standard device descriptor for USB 2.0 */
const uint8_t CyFxUSB20DeviceDscr[] __attribute__ ((aligned (4))) =
{
    0x12,                           /* Descriptor size */
    0x01,                           /* Device descriptor type */
    0x10,0x02,                      /* USB 2.10  */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    0xB4,0x04,                      /* Vendor ID */
    0x0C,0x49,                      /* Product ID */
    0x00,0x00,                      /* Device release number */
    0x01,                           /* Manufacture string index */
    0x02,                           /* Product string index */
    0x00,                           /* Serial number string index */
    0x01                            /* Number of configurations */
};

/* Device qualifier descriptor. */
const uint8_t CyFxUSBDeviceQualDscr[] __attribute__ ((aligned (4))) =
{
    0x0A,                           /* Descriptor size */
    0x06,                           /* Device qualifier descriptor type */
    0x00,0x02,                      /* USB 2.0 */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    0x01,                           /* Number of configurations */
    0x00                            /* Reserved */
};

/* Binary Object Store (BOS) Descriptor. */
const uint8_t CyFxUSBBOSDscr[] __attribute__ ((aligned (4))) =
{
    0x05,                           /* Descriptor size */
    CY_USB_BOS_DSCR,                /* BOS descriptor type */
    0x0C,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x01,                           /* Number of device capability descriptors */

    /* USB 2.0 extension */
    0x07,                           /* Descriptor size */
    CY_DEVICE_CAPB_DSCR,            /* Device capability type descriptor */
    0x02,                           /* USB 2.0 extension capability type */
    0x1E,0x64,0x00,0x00,            /* Supported device level features: LPM support, BESL supported,
                                       Baseline BESL=400 us, Deep BESL=1000 us. */
};

/* Standard high speed configuration descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBHSConfigDscr[1024] __attribute__ ((aligned (32))) =
{
    /* Configuration descriptor */
    0x09,                           /* Descriptor size */
    0x02,                           /* Configuration descriptor type */
    0x12,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x01,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - bus powered  and Remote wakeup disable 0x80 */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA - 0x32 */

    /* Interface descriptor, alt setting 0, bulk tranfer */
    0x09,                           /* Descriptor size */
    0x04,                           /* Interface descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x00,                           /* Number of endpoints */
    0xFF,                           /* Interface class */
    0x00,                           /* Interface sub class */
    0x00,                           /* Interface protocol code */
    0x00,                           /* Interface descriptor string index */
};

/* Standard full speed configuration descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBFSConfigDscr[1024] __attribute__ ((aligned (4))) =
{
    /* Configuration descriptor */
    0x09,                           /* Descriptor size */
    0x02,                           /* Configuration descriptor type */
    0x12,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x01,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - bus powered  and Remote wakeup disable 0x80 */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA - 0x32 */

    /* Interface descriptor, alt setting 0, bulk transfer */
    0x09,                           /* Descriptor size */
    0x04,                           /* Interface descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x00,                           /* Number of endpoints */
    0xFF,                           /* Interface class */
    0x00,                           /* Interface sub class */
    0x00,                           /* Interface protocol code */
    0x00,                           /* Interface descriptor string index */

};

USB_DESC_ATTRIBUTES uint8_t CyFxUSBStringLangIDDscr[32] __attribute__ ((aligned (32))) =
{
    0x04,
    0x03,
    0x09,
    0x04
};

/* Standard Manufacturer String descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBManufactureDscr[32] __attribute__ ((aligned (32))) =
{
    0x12,        /* Descriptor size */
    0x03,        /* Device descriptor type */
    'I',0x00,
    'N',0x00,
    'F',0x00,
    'I',0x00,
    'N',0x00,
    'E',0x00,
    'O',0x00,
    'N',0x00
};

/* Standard Product String desciptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBProductDscr[] __attribute__ ((aligned (32))) =
{
    0x32, 0x03,    
    'E',  0x00,
    'Z',  0x00,
    '-',  0x00,
    'U',  0x00,
    'S',  0x00,
    'B',  0x00,
    ' ',  0x00,
    'F',  0x00,
    'X',  0x00,
    '2',  0x00,
    'G',  0x00,
    '3',  0x00,
    ' ',  0x00,
    'Q',  0x00,
    'S',  0x00,
    'P',  0x00,
    'I',  0x00,
    ' ',  0x00,
    'B',  0x00,
    'R',  0x00,
    'I',  0x00,
    'D',  0x00,
    'G',  0x00,
    'E',  0x00
};

/* MS OS String Descriptor */
USB_DESC_ATTRIBUTES uint8_t glOsString[] __attribute__ ((aligned (32))) =
{
    0x12, /* Length. */
    0x03, /* Type - string. */
    'M', 0x00, 'S', 0x00, 'F', 0x00, 'T', 0x00, '1', 0x00, '0', 0x00, '0', 0x00, /* Signature. */
    MS_VENDOR_CODE, /* MS vendor code. */
    0x00 /* Padding. */
};

USB_DESC_ATTRIBUTES uint8_t glOsCompatibilityId[] __attribute__ ((aligned (32))) =
{
    /* Header */
    0x28, 0x00, 0x00, 0x00, /* length Need to be updated based on number of interfaces. */
    0x00, 0x01, /* BCD version */
    0x04, 0x00, /* Index: 4 - compatibility ID */
    0x01, /* count. Need to be updated based on number of interfaces. */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* reserved. */
    /* First Interface */
    0x00, /* Interface number */
    0x01, /* reserved: Need to be 1. */
    0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00, /* comp ID –ID to bind the device with
                                                       WinUSB.*/
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* sub-compatibility ID - NONE. */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* reserved - needs to be zero. */
};

USB_DESC_ATTRIBUTES uint8_t glOsFeature[] __attribute__ ((aligned (32))) =
{
    /* Header */
    0x8E, 0x00, 0x00, 0x00, /* Length. */
    0x00, 0x01, /* BCD version. 1.0 as per MS */
    0x05, 0x00, /* Index */
    0x01, 0x00, /* count. */
    /* Property section. */
    0x84, 0x00, 0x00, 0x00, /* length */
    0x01, 0x00, 0x00, 0x00, /* dwPropertyDataType: REG_DWORD_LITTLE_ENDIAN */
    0x28, 0x00, /* wPropertyNameLength: 0x30 */

    0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00, 0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6E, 0x00,
    0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00, 0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00,
    0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x00, 0x00, /* bPropertyName: DeviceInterfaceGUID */
    0x4E, 0x00, 0x00, 0x00, /* dwPropertyDataLength: 4E */

    '{', 0x00, '0', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00,
    '7', 0x00, '-', 0x00, '2', 0x00, 'A', 0x00, '4', 0x00, 'F', 0x00, '-', 0x00, '4', 0x00,
    '9', 0x00, 'E', 0x00, 'E', 0x00, '-', 0x00, '8', 0x00, 'D', 0x00, 'D', 0x00, '3', 0x00,
    '-', 0x00, 'F', 0x00, 'A', 0x00, 'D', 0x00, 'E', 0x00, 'A', 0x00, '3', 0x00, '7', 0x00,
    '7', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, 'A', 0x00, '}', 0x00, 0x00, 0x00
        /* bPropertyData: {01234567-2A4F-49EE-8DD3-FADEA377234A} */
};

void Cy_USB_GenerateConfigDescriptor (void)
{

    uint16_t hsDscLen = 0;
    uint16_t fsDscLen = 0;
    uint16_t idx = 0;

    /* Code to configure the USBHS Configuration descriptor. */
    idx      = 0;
    hsDscLen = 9 + 9 + (7 * 3); /* Config descriptor + Interface descriptor + 3 endpoint descriptors */

    /* Configure the 9 byte config descriptor. */
    CyFxUSBHSConfigDscr[idx++] = 0x09;
    CyFxUSBHSConfigDscr[idx++] = 0x02;
    CyFxUSBHSConfigDscr[idx++] = (hsDscLen & 0xFF);
    CyFxUSBHSConfigDscr[idx++] = (hsDscLen >> 8);
    CyFxUSBHSConfigDscr[idx++] = 0x01;
    CyFxUSBHSConfigDscr[idx++] = 0x01;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x80;
    CyFxUSBHSConfigDscr[idx++] = 0x32;

    /* Interface descriptor with 2 Bulk OUT and 1 Bulk IN endpoints. */
    CyFxUSBHSConfigDscr[idx++] = 0x09;
    CyFxUSBHSConfigDscr[idx++] = 0x04;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x03;  /* Number of endpoints: 3 total */
    CyFxUSBHSConfigDscr[idx++] = 0xFF;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x00;

    /* Bulk OUT endpoint 1 descriptor. */
    CyFxUSBHSConfigDscr[idx++] = 0x07;
    CyFxUSBHSConfigDscr[idx++] = 0x05;
    CyFxUSBHSConfigDscr[idx++] = 0x01;  /* EP1 OUT */
    CyFxUSBHSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x02;
    CyFxUSBHSConfigDscr[idx++] = 0x00;

    /* Bulk OUT endpoint 2 descriptor. */
    CyFxUSBHSConfigDscr[idx++] = 0x07;
    CyFxUSBHSConfigDscr[idx++] = 0x05;
    CyFxUSBHSConfigDscr[idx++] = 0x02;  /* EP2 OUT */
    CyFxUSBHSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x02;
    CyFxUSBHSConfigDscr[idx++] = 0x00;

    /* Bulk IN endpoint 1 descriptor. */
    CyFxUSBHSConfigDscr[idx++] = 0x07;
    CyFxUSBHSConfigDscr[idx++] = 0x05;
    CyFxUSBHSConfigDscr[idx++] = 0x83;  /* EP3 IN */
    CyFxUSBHSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBHSConfigDscr[idx++] = 0x00;
    CyFxUSBHSConfigDscr[idx++] = 0x02;
    CyFxUSBHSConfigDscr[idx++] = 0x00;

     /* Code to configure the USBFS Configuration descriptor. */
    idx      = 0;
    fsDscLen = 9 + 9 + (7 * 3); /* Config descriptor + Interface descriptor + 3 endpoint descriptors */

    /* Configure the 9 byte config descriptor. */
    CyFxUSBFSConfigDscr[idx++] = 0x09;
    CyFxUSBFSConfigDscr[idx++] = 0x02;
    CyFxUSBFSConfigDscr[idx++] = (fsDscLen & 0xFF);
    CyFxUSBFSConfigDscr[idx++] = (fsDscLen >> 8);
    CyFxUSBFSConfigDscr[idx++] = 0x01;
    CyFxUSBFSConfigDscr[idx++] = 0x01;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x80;
    CyFxUSBFSConfigDscr[idx++] = 0x32;
    
    /* Interface descriptor with 2 Bulk OUT and 1 Bulk IN endpoints. */
    CyFxUSBFSConfigDscr[idx++] = 0x09;
    CyFxUSBFSConfigDscr[idx++] = 0x04;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x03;  /* Number of endpoints: 3 total */
    CyFxUSBFSConfigDscr[idx++] = 0xFF;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;

    /* Bulk OUT endpoint 1 descriptor. */
    CyFxUSBFSConfigDscr[idx++] = 0x07;
    CyFxUSBFSConfigDscr[idx++] = 0x05;
    CyFxUSBFSConfigDscr[idx++] = 0x01;  /* EP1 OUT */
    CyFxUSBFSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBFSConfigDscr[idx++] = 0x40;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;

    /* Bulk OUT endpoint 2 descriptor. */
    CyFxUSBFSConfigDscr[idx++] = 0x07;
    CyFxUSBFSConfigDscr[idx++] = 0x05;
    CyFxUSBFSConfigDscr[idx++] = 0x02;  /* EP2 OUT */
    CyFxUSBFSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBFSConfigDscr[idx++] = 0x40;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;

    /* Bulk IN endpoint 3 descriptor. */
    CyFxUSBFSConfigDscr[idx++] = 0x07;
    CyFxUSBFSConfigDscr[idx++] = 0x05;
    CyFxUSBFSConfigDscr[idx++] = 0x83;  /* EP3 IN */
    CyFxUSBFSConfigDscr[idx++] = CY_USB_ENDP_TYPE_BULK;
    CyFxUSBFSConfigDscr[idx++] = 0x40;
    CyFxUSBFSConfigDscr[idx++] = 0x00;
    CyFxUSBFSConfigDscr[idx++] = 0x00;

}

/*[]*/

