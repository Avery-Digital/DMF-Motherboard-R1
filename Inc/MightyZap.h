/*******************************************************************************
 * @file    Inc/MightyZap.h
 * @author  Cam
 * @brief   mightyZAP 12Lf Linear Servo Driver — API and Types
 *
 *          Binary packet protocol over RS-485 (UART8) half-duplex.
 *          Packet: [0xFF][0xFF][0xFF][ID][SIZE][CMD][FACTORS...][CHECKSUM]
 *
 *          Hardware:
 *            PE1 (UART8_TX, AF8) → DI on RS-485 transceiver
 *            PE0 (UART8_RX, AF8) → RO on RS-485 transceiver
 *            PD15 (GPIO)         → NOT gate → DE+RE (tied together)
 *            Inverted DE: PD15 LOW → NOT → HIGH → transmit
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef MIGHTYZAP_H
#define MIGHTYZAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Bsp.h"

/* ======================== Status ========================================== */

typedef enum {
    MZAP_OK             = 0x00U,
    MZAP_ERR_INIT       = 0x01U,
    MZAP_ERR_TIMEOUT    = 0x02U,
    MZAP_ERR_CHECKSUM   = 0x03U,
    MZAP_ERR_COMM       = 0x04U,
    MZAP_ERR_SERVO      = 0x05U,   /**< Servo returned error in feedback */
} MightyZap_Status;

/* ======================== Instruction Codes ================================ */

#define MZAP_CMD_ECHO           0xF1U   /**< Ping / verify connection        */
#define MZAP_CMD_LOAD_DATA      0xF2U   /**< Read register                   */
#define MZAP_CMD_STORE_DATA     0xF3U   /**< Write register (permanent)      */
#define MZAP_CMD_SEND_DATA      0xF4U   /**< Write register (temporary)      */
#define MZAP_CMD_EXECUTION      0xF5U   /**< Execute temp data               */
#define MZAP_CMD_FACTORY_RESET  0xF6U   /**< Reset to factory defaults       */
#define MZAP_CMD_RESTART        0xF8U   /**< Reboot servo                    */

/* ======================== Register Addresses =============================== */

/* Non-volatile (saved to flash) */
#define MZAP_REG_MODEL_NUM_L    0x00U
#define MZAP_REG_MODEL_NUM_H    0x01U
#define MZAP_REG_FW_VERSION     0x02U
#define MZAP_REG_ID             0x03U
#define MZAP_REG_BAUD_RATE      0x04U
#define MZAP_REG_RETURN_DELAY   0x05U
#define MZAP_REG_SHORT_LIMIT_L  0x06U
#define MZAP_REG_SHORT_LIMIT_H  0x07U
#define MZAP_REG_LONG_LIMIT_L   0x08U
#define MZAP_REG_LONG_LIMIT_H   0x09U
#define MZAP_REG_VOLT_LIMIT     0x0DU
#define MZAP_REG_FEEDBACK_MODE  0x10U
#define MZAP_REG_ALARM_LED      0x11U
#define MZAP_REG_ALARM_SHUTDOWN 0x12U
#define MZAP_REG_START_MARGIN   0x13U
#define MZAP_REG_END_MARGIN     0x14U
#define MZAP_REG_GOAL_SPEED_L   0x15U
#define MZAP_REG_GOAL_SPEED_H   0x16U
#define MZAP_REG_ACCEL_RATE     0x21U
#define MZAP_REG_DECEL_RATE     0x22U

/* Volatile (RAM only) */
#define MZAP_REG_GOAL_CURRENT_L 0x34U
#define MZAP_REG_GOAL_CURRENT_H 0x35U
#define MZAP_REG_FORCE_ONOFF    0x80U
#define MZAP_REG_GOAL_POS_L     0x86U
#define MZAP_REG_GOAL_POS_H     0x87U
#define MZAP_REG_PRESENT_POS_L  0x8CU
#define MZAP_REG_PRESENT_POS_H  0x8DU
#define MZAP_REG_MOTOR_RATE_L   0x90U
#define MZAP_REG_MOTOR_RATE_H   0x91U
#define MZAP_REG_PRESENT_VOLT   0x92U

/* ======================== Constants ======================================== */

#define MZAP_HEADER_BYTE        0xFFU
#define MZAP_BROADCAST_ID       0xFEU
#define MZAP_MAX_PACKET_SIZE    32U
#define MZAP_TIMEOUT_MS         50U
#define MZAP_WRITE_DELAY_MS     5U
#define MZAP_READ_DELAY_MS      10U

/* Position range */
#define MZAP_POS_MIN            0U
#define MZAP_POS_CENTER         2047U
#define MZAP_POS_MAX            4095U
#define MZAP_POS_27MM           3686U   /**< 27mm stroke limit (default)     */

/* Speed range */
#define MZAP_SPEED_MIN          0U
#define MZAP_SPEED_MAX          1023U

/* ======================== Configuration ==================================== */

typedef struct {
    PinConfig       tx_pin;         /**< PE1 — UART8_TX (AF8)              */
    PinConfig       rx_pin;         /**< PE0 — UART8_RX (AF8)              */
    PinConfig       de_re_pin;      /**< PD15 — direction (inverted)       */
    USART_TypeDef  *usart;          /**< UART8                             */
    uint32_t        bus_clk_enable; /**< LL_APB1_GRP1_PERIPH_UART8        */
    uint32_t        kernel_clk_src; /**< LL_RCC_USART234578_CLKSOURCE_x   */
    uint32_t        kernel_clk_hz;  /**< Kernel clock frequency            */
    uint32_t        baudrate;       /**< 57600 default                     */
} MightyZap_Config;

/* ======================== Runtime Handle ==================================== */

typedef struct {
    const MightyZap_Config *cfg;
    bool                    initialised;
    uint8_t                 servo_id;       /**< Target servo ID (default 0) */
    uint8_t                 last_error;     /**< Last feedback error byte    */
} MightyZap_Handle;

/* ======================== Extern Instances ================================== */

extern const MightyZap_Config   mzap_cfg;
extern MightyZap_Handle         mzap_handle;

/* ======================== Public API ======================================== */

/**
 * @brief  Initialise UART8 and GPIO pins for mightyZAP communication.
 */
MightyZap_Status MightyZap_Init(MightyZap_Handle *h);

/**
 * @brief  Ping the servo. Returns MZAP_OK if servo responds.
 */
MightyZap_Status MightyZap_Ping(MightyZap_Handle *h);

/**
 * @brief  Read 1 byte from a register.
 */
MightyZap_Status MightyZap_ReadByte(MightyZap_Handle *h, uint8_t addr, uint8_t *value);

/**
 * @brief  Read 2 bytes (16-bit little-endian) from a register.
 */
MightyZap_Status MightyZap_ReadWord(MightyZap_Handle *h, uint8_t addr, uint16_t *value);

/**
 * @brief  Write 1 byte to a register (Store Data — permanent).
 */
MightyZap_Status MightyZap_WriteByte(MightyZap_Handle *h, uint8_t addr, uint8_t value);

/**
 * @brief  Write 2 bytes (16-bit little-endian) to a register (Store Data — permanent).
 */
MightyZap_Status MightyZap_WriteWord(MightyZap_Handle *h, uint8_t addr, uint16_t value);

/* ---- Convenience functions ---- */

/**
 * @brief  Set goal position (0–4095).
 */
MightyZap_Status MightyZap_SetPosition(MightyZap_Handle *h, uint16_t position);

/**
 * @brief  Read current position.
 */
MightyZap_Status MightyZap_GetPosition(MightyZap_Handle *h, uint16_t *position);

/**
 * @brief  Set goal speed (0–1023).
 */
MightyZap_Status MightyZap_SetSpeed(MightyZap_Handle *h, uint16_t speed);

/**
 * @brief  Set force on (1) or off (0).
 */
MightyZap_Status MightyZap_SetForce(MightyZap_Handle *h, bool on);

/**
 * @brief  Read present voltage.
 */
MightyZap_Status MightyZap_GetVoltage(MightyZap_Handle *h, uint8_t *voltage);

#ifdef __cplusplus
}
#endif

#endif /* MIGHTYZAP_H */
