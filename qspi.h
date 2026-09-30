#ifndef _SPI_H_
#define _SPI_H_

#include "cy_pdl.h"
#include "cy_debug.h"
#include "usb_app.h"

/* Macros */
#define SMIF_HW                           (SMIF0)
#define CY_SYSCLK_SPI_CLK_HF1             (1)

#define SMIF_CLK_HSIOM                    (P6_0_SMIF_SPI_CLK)
#define SMIF_CLK_PORT                     (P6_0_PORT)
#define SMIF_CLK_PIN                      (P6_0_PIN)

#define SMIF_SELECT0_HSIOM                (P6_1_SMIF_SPI_SELECT0)
#define SMIF_SELECT0_PORT                 (P6_1_PORT)
#define SMIF_SELECT0_PIN                  (P6_1_PIN)

#define SMIF_SELECT1_HSIOM                (P6_2_SMIF_SPI_SELECT1)
#define SMIF_SELECT1_PORT                 (P6_2_PORT)
#define SMIF_SELECT1_PIN                  (P6_2_PIN)

#define SMIF_DATA0_HSIOM                  (P7_0_SMIF_SPI_DATA0)
#define SMIF_DATA0_PORT                   (P7_0_PORT)
#define SMIF_DATA0_PIN                    (P7_0_PIN)

#define SMIF_DATA1_HSIOM                  (P7_1_SMIF_SPI_DATA1)
#define SMIF_DATA1_PORT                   (P7_1_PORT)
#define SMIF_DATA1_PIN                    (P7_1_PIN)

#define SMIF_DATA2_HSIOM                  (P7_2_SMIF_SPI_DATA2)
#define SMIF_DATA2_PORT                   (P7_2_PORT)
#define SMIF_DATA2_PIN                    (P7_2_PIN)

#define SMIF_DATA3_HSIOM                  (P7_3_SMIF_SPI_DATA3)
#define SMIF_DATA3_PORT                   (P7_3_PORT)
#define SMIF_DATA3_PIN                    (P7_3_PIN)

#define SMIF_DATA4_HSIOM                  (P7_4_SMIF_SPI_DATA4)
#define SMIF_DATA4_PORT                   (P7_4_PORT)
#define SMIF_DATA4_PIN                    (P7_4_PIN)

#define SMIF_DATA5_HSIOM                  (P7_5_SMIF_SPI_DATA5)
#define SMIF_DATA5_PORT                   (P7_5_PORT)
#define SMIF_DATA5_PIN                    (P7_5_PIN)

#define SMIF_DATA6_HSIOM                  (P7_6_SMIF_SPI_DATA6)
#define SMIF_DATA6_PORT                   (P7_6_PORT)
#define SMIF_DATA6_PIN                    (P7_6_PIN)

#define SMIF_DATA7_HSIOM                  (P7_7_SMIF_SPI_DATA7)
#define SMIF_DATA7_PORT                   (P7_7_PORT)
#define SMIF_DATA7_PIN                    (P7_7_PIN)

#if FLASH_AT45D
#define CY_SPI_STATUS_READ_CMD            (0xD7)
#define CY_SPI_SECTOR_ERASE_CMD           (0x7C)
#define CY_SPI_PROGRAM_CMD                (0x84)
#define CY_SPI_PROGRAM_CMD_1              (0x83)
#define CY_SPI_WRITE_ENABLE_CMD           (0x06)
#define CY_SPI_READ_CMD                   (0x0B)
#define SPI_ADDRESS_BYTE_COUNT            (4)
#define CY_SPI_WRITE_ENABLE_LATCH_MASK    (0x02)
#define CY_SPI_WIP_MASK                   (0x80)
#define CY_SPI_WIP_STATUS                 (0x00)
#else
#define CY_SPI_STATUS_READ_CMD            (0x05)
#define CY_SPI_SECTOR_ERASE_CMD           (0xD8)
#define CY_SPI_HYBRID_SECTOR_ERASE_CMD    (0x20)
#define CY_SPI_PROGRAM_CMD                (0x02)
#define CY_QSPI_PROGRAM_CMD               (0x32)
#define CY_SPI_WRITE_ENABLE_CMD           (0x06)
#define CY_SPI_READ_CMD                   (0x03)
#define CY_QSPI_READ_CMD                  (0x6B)
#define SPI_ADDRESS_BYTE_COUNT            (3)
#define CY_SPI_RESET_ENABLE_CMD           (0x66)
#define CY_SPI_SW_RESET_CMD               (0x99)
#define CY_SPI_WRITE_ENABLE_LATCH_MASK    (0x02)
#define CY_SPI_WIP_MASK                   (0x01)
#define CY_SPI_WIP_STATUS                 (0x01)
#endif

#define CY_SPI_READ_ID_CMD                (0x9F)
#define CY_FLASH_ID_LENGTH                (0x04)
#define CY_APP_SPI_FLASH_ERASE_SIZE       (0x10000)
#define CY_SPI_FLASH_PAGE_SIZE            (0x100)
#define CY_SPI_PROGRAM_TIMEOUT_US         (650000)
#define MAX_BUFFER_SIZE                   (2048u)

#define CY_CFI_DEVICE_SIZE_OFFSET           (0x27)
#define CY_CFI_ERASE_NUM_SECTORS_OFFSET     (0x2D)
#define CY_CFI_ERASE_REGION_SIZE_INFO_SIZE  (0x04)
#define CY_CFI_ERASE_SECTOR_SIZE_OFFSET     (0x2F)
#define CY_CFI_MAX_SIZE_NUM_ERASE_SECTORS   (0xFF)
#define CY_CFI_NUM_ERASE_REGION_OFFSET      (0x2C)
#define CY_CFI_TABLE_LENGTH                 (0x56)

#define CY_APP_QSPI_CONFIG_REG_READ_CMD   (0x35)
#define CY_APP_QSPI_WRITE_REGISTER_CMD    (0x01)
#define CY_APP_QSPI_STATUS_1_READ_CMD     (0x05)

/* Commands to configure the SMIF peripheral for a bus transaction */
#define CY_QSPI_PROGRAM_CMD               (0x32)  /* For Write */
#define CY_QSPI_READ_CMD                  (0x6B)  /* For Read */
#define QSPI_READ_DUMMY_CYCLES            (8)

#define HBDMA_BUF_ATTRIBUTES __attribute__ ((section(".hbBufSection"), used)) __attribute__ ((aligned (32)))

typedef enum cy_en_flash_index_t
{
  SPI_FLASH_0    = 0,
  SPI_FLASH_1    = 1,
  DUAL_SPI_FLASH = 2,
  NUM_SPI_FLASH,
}cy_en_flash_index_t;

/**
 * \name Cy_QSPI_StartWriteStream
 * \brief Starts high-speed write streaming mode for USB-QSPI bridge.
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_StartWriteStream(void);

/**
 * \name Cy_QSPI_StopWriteStream
 * \brief Stops write streaming mode.
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_StopWriteStream(void);

/**
 * \name Cy_QSPI_StartReadStream
 * \brief Starts high-speed read streaming mode for USB-QSPI bridge.
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_StartReadStream(void);

/**
 * \name Cy_QSPI_StopReadStream
 * \brief Stops read streaming mode.
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_StopReadStream(void);

/**
 * \name Cy_QSPI_SafeSmifReset
 * \brief Reset the SMIF block to recover from a stuck command FIFO queue
 * \retval None
 */
void Cy_QSPI_SafeSmifReset(void);

/**
 * \name Cy_QSPI_DeassertCS
 * \brief De-asserts the slave select line to end the current transaction.
 * \param flashIndex Flash slave select index.
 * \retval None
 */
void Cy_QSPI_DeassertCS(cy_en_flash_index_t flashIndex);

/**
 * \name QspiInterfaceInit
 * \brief Initialize the QSPI interface
 * \retval None
 */
void QspiInterfaceInit(void);

/**
 * \name QspiTransmitData
 * \brief Transmit data over QSPI
 * \param pTxData Pointer to data to be transmitted
 * \param txByteCnt Number of bytes to be transmitted
 * \retval status
 */
bool QspiTransmitData(uint8_t *pTxData, uint16_t txByteCnt);

/**
 * \name QspiReceiveData
 * \brief Receive data over QSPI
 * \param pRxData pointer to the buffer to recieve data into
 * \param rxByteCnt number of bytes to recieve
 * \retval true if successful, false if not
 */
bool QspiReceiveData(uint8_t *pRxData, uint32_t rxByteCnt);

#endif /* _SPI_H_ */