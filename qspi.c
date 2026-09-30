#include "qspi.h"
#include "cy_debug.h"
#include "cy_smif.h"
#include "cycfg_pins.h"
#include "usb_app.h"
#include <string.h>
#include "usb_bridge_device.h"

/* Global variables */
// cy_stc_smif_context_t      spiContext;                          /* SMIF context structure */

HBDMA_BUF_ATTRIBUTES uint8_t readBuffer[MAX_BUFFER_SIZE];       /* Read buffer */
HBDMA_BUF_ATTRIBUTES uint8_t writeBuffer[MAX_BUFFER_SIZE];      /* Write buffer */

static cy_stc_smif_context_t glQspiContext;                     /* SMIF context structure */

static bool g_writeStreamActive = false;
static bool g_readStreamActive = false;

/* GPIO macros for chip select - direct register access */
#define CS_ASSERT()    (P6_2_PORT->OUT_CLR = (1UL << P6_2_PIN))
#define CS_DEASSERT()  (P6_2_PORT->OUT_SET = (1UL << P6_2_PIN))

/* SMIF block configuration */
static const cy_stc_smif_config_t glSmifConfig =
{
    .mode = (uint32_t)CY_SMIF_NORMAL,                       /* Normal SMIF operation */
    .deselectDelay = 0u,                                    /* CS deselect delay */
    .rxClockSel = (uint32_t)CY_SMIF_SEL_INV_INTERNAL_CLK,   /* Receiver clock selection */

    /* CY_SMIF_WAIT_STATES: Blocking wait when RX FIFO is empty and access is attempted
     * alternative: CY_SMIF_BUS_ERROR, throw error when empty */
    .blockEvent = (uint32_t)CY_SMIF_WAIT_STATES,
};

/**
 * \name Cy_QSPI_StartWriteStream
 * \brief Enable QSPI write streaming
 * \retval CY_SMIF_SUCCESS on exit
 */
cy_en_smif_status_t Cy_QSPI_StartWriteStream(void)
{
    g_writeStreamActive = true;
    g_readStreamActive = false;
    return CY_SMIF_SUCCESS;
}

/**
 * \name Cy_QSPI_StopWriteStream
 * \brief Disable QSPI write streaming
 * \retval CY_SMIF_SUCCESS on exit
 */
cy_en_smif_status_t Cy_QSPI_StopWriteStream(void)
{
    g_writeStreamActive = false;
    /* De-assert CS to terminate the transaction cleanly */
    Cy_QSPI_DeassertCS(SPI_FLASH_0);
    return CY_SMIF_SUCCESS;
}

/**
 * \name Cy_QSPI_StartReadStream
 * \brief Enable QSPI read streaming
 * \retval CY_SMIF_SUCCESS on exit
 */
cy_en_smif_status_t Cy_QSPI_StartReadStream(void)
{
    g_readStreamActive = true;
    g_writeStreamActive = false;
    return CY_SMIF_SUCCESS;
}

/**
 * \name Cy_QSPI_StopReadStream
 * \brief Disable QSPI read streaming
 * \retval CY_SMIF_SUCCESS on exit
 */
cy_en_smif_status_t Cy_QSPI_StopReadStream(void)
{
    g_readStreamActive = false;
    return CY_SMIF_SUCCESS;
}

/**
 * \name Cy_QSPI_DeassertCS
 * \brief De-asserts the slave select line to end the current transaction.
 * \param flashIndex Flash (or simply QSPI) slave select index.
 * \retval None
 */
void Cy_QSPI_DeassertCS(cy_en_flash_index_t flashIndex)
{
    cy_en_smif_slave_select_t slaveSelect = (flashIndex == SPI_FLASH_0) ?
        CY_SMIF_SLAVE_SELECT_0 : CY_SMIF_SLAVE_SELECT_1;

    /* A dummy command with the CY_SMIF_TX_LAST_BYTE flag is used to de-assert CS */
    Cy_SMIF_TransmitCommand(SMIF_HW, 0x00, CY_SMIF_WIDTH_SINGLE, NULL, 0,
                              CY_SMIF_WIDTH_SINGLE, slaveSelect, CY_SMIF_TX_LAST_BYTE,
                              &glQspiContext);
}

/**
 * \name Cy_QSPI_SafeSmifReset
 * \brief Reset the SMIF block to recover from a stuck command FIFO queue
 * \retval None
 */
void Cy_QSPI_SafeSmifReset(void)
{
    DBG_APP_WARN("SMIF: performing safe reset sequence\r\n");
    DBG_APP_WARN("SMIF: CmdFifoCount=%u\r\n", (uint32_t)Cy_SMIF_GetCmdFifoStatus(SMIF_HW));

    /* Disable, wait, then re-enable SMIF while preserving context. */
    Cy_SMIF_Disable(SMIF_HW);
    Cy_SysLib_DelayUs(2000u);
    Cy_SMIF_Enable(SMIF_HW, &glQspiContext);

    DBG_APP_WARN("SMIF: reset done CmdFifoCount=%u\r\n", (uint32_t)Cy_SMIF_GetCmdFifoStatus(SMIF_HW));
}

/**
 * \name QspiInterfaceInit
 * \brief Initialize the QSPI interface
 * \retval None
 */
void QspiInterfaceInit(void)
{
    /* Initialize SMIF with the configured parameters. */
    Cy_SMIF_Init(SMIF0, &glSmifConfig, 10000u, &glQspiContext);

    /* Configure default pin mapping for SMIF slaves. */
    Cy_SMIF_SetDataSelect(SMIF0, CY_SMIF_SLAVE_SELECT_0, CY_SMIF_DATA_SEL0);
    Cy_SMIF_SetDataSelect(SMIF0, CY_SMIF_SLAVE_SELECT_1, CY_SMIF_DATA_SEL0);
    Cy_SMIF_Enable(SMIF0, &glQspiContext);

}

/**
 * \name QspiTransmitData
 * \brief Transmit data over QSPI.
 * \param pTxData Pointer to data to transmit
 * \param txByteCnt Number of bytes to transmit
 * \retval true on success, false on error
 */
bool QspiTransmitData(uint8_t *pTxData, uint16_t txByteCnt)
{
    uint32_t idx = 0;

    if ((txByteCnt == 0) || (pTxData == NULL)) {
        /* Invalid arguments. */
        return false;
    }

    if (txByteCnt == 1) {
        CS_ASSERT();
        SMIF0->TX_CMD_FIFO_WR = 0x00028100UL | pTxData[idx++];  /* SINGLE mode, last byte */
        while ((SMIF0->STATUS & 0x80000000UL) != 0); /* Wait for SMIF to be not busy */
        CS_DEASSERT();
        return true;
    }

    /* Assert CS and start SINGLE mode transmit (1-bit) */
    CS_ASSERT();
    SMIF0->TX_CMD_FIFO_WR = 0x00020100UL | pTxData[idx++];  /* SINGLE command */
    SMIF0->TX_CMD_FIFO_WR = 0x00060000UL | (txByteCnt - 2); /* SINGLE data count */

    /* Write the first few bytes to align to a 4-byte boundary.
     * The first byte is already sent, so we check alignment of (pTxData + 1). */
    while ((((uint32_t)(pTxData + idx)) & 0x03U) != 0 && idx < txByteCnt)
    {
        while (SMIF0->TX_DATA_FIFO_STATUS >= 8);
        SMIF0->TX_DATA_FIFO_WR1 = pTxData[idx++];
    }

    /* Now, we are 4-byte aligned. Write words. */
    uint32_t *pData32 = (uint32_t *)(pTxData + idx);
    uint32_t words = (txByteCnt - idx) >> 2;

    /* Unrolled loop - write 4 words (16 bytes) at a time */
    while (words >= 4)
    {
        /* Wait until FIFO has room for 4 words */
        while (SMIF0->TX_DATA_FIFO_STATUS > 4);
        SMIF0->TX_DATA_FIFO_WR4 = *pData32++;
        SMIF0->TX_DATA_FIFO_WR4 = *pData32++;
        SMIF0->TX_DATA_FIFO_WR4 = *pData32++;
        SMIF0->TX_DATA_FIFO_WR4 = *pData32++;
        words -= 4;
        idx += 16;
    }

    /* Write any remaining words */
    while (words > 0)
    {
        while (SMIF0->TX_DATA_FIFO_STATUS >= 8);
        SMIF0->TX_DATA_FIFO_WR4 = *pData32++;
        words--;
        idx += 4;
    }

    /* Write any remaining bytes */
    while (idx < txByteCnt)
    {
        while (SMIF0->TX_DATA_FIFO_STATUS >= 8);
        SMIF0->TX_DATA_FIFO_WR1 = pTxData[idx++];
    }

    /* Wait for TX FIFO to drain, then de-assert CS. */
    while (SMIF0->TX_DATA_FIFO_STATUS > 0);
    CS_DEASSERT();

    return true;
}

/**
 * \name QspiReceiveData
 * \brief Receive data over QSPI.
 * \param pRxData Pointer to destination buffer
 * \param rxByteCnt Number of bytes to receive
 * \retval true on success, false on error
 */
bool QspiReceiveData(uint8_t *pRxData, uint32_t rxByteCnt)
{
    uint32_t idx = 0;
    volatile uint32_t dummy;

    if ((rxByteCnt == 0) || (pRxData == NULL)) {
        /* Invalid arguments. */
        return false;
    }

    /* Issue SINGLE mode RX command (1-bit) */
    SMIF0->TX_CMD_FIFO_WR = 0x00020100UL;                   /* SINGLE command */
    SMIF0->TX_CMD_FIFO_WR = 0x000A0000UL | (rxByteCnt + 7); /* SINGLE receive */
    
    /* Wait for initial prefetch in RX FIFO. */
    while (SMIF0->RX_DATA_FIFO_STATUS < 8) {
        __NOP();
    }

    /* Assert SPI-CS now. */
    CS_ASSERT();

    /* Discard prefetched words from RX FIFO. */
    dummy = SMIF0->RX_DATA_FIFO_RD4;
    dummy = SMIF0->RX_DATA_FIFO_RD4;
    (void)dummy; /* suppress unused warning */

    /* Optimized 4-byte word reads with burst */
    uint32_t *pData32 = (uint32_t *)pRxData;
    uint32_t words = rxByteCnt >> 2;
    
    /* Unrolled read loop for better performance */
    while (words >= 8) {
        pData32[0] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[1] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[2] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[3] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[4] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[5] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[6] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[7] = SMIF0->RX_DATA_FIFO_RD4;
        pData32 += 8;
        words -= 8;
        idx += 32;
    }
    
    while (words >= 4) {
        pData32[0] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[1] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[2] = SMIF0->RX_DATA_FIFO_RD4;
        pData32[3] = SMIF0->RX_DATA_FIFO_RD4;
        pData32 += 4;
        words -= 4;
        idx += 16;
    }
    
    while (words > 0) {
        *pData32++ = SMIF0->RX_DATA_FIFO_RD4;
        words--;
        idx += 4;
    }

    /* Handle remaining bytes */
    rxByteCnt -= idx;
    while (rxByteCnt != 0) {
        pRxData[idx++] = SMIF0->RX_DATA_FIFO_RD1;
        rxByteCnt--;
    }

    CS_DEASSERT();
    return true;
}
