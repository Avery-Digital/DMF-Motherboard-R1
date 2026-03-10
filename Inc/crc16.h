/*******************************************************************************
 * @file    Inc/crc16.h
 * @author  Cam
 * @brief   CRC-16 CCITT (0x1021) — Lookup Table Implementation
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef CRC16_H
#define CRC16_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  CRC-16 CCITT initial value.
 */
#define CRC16_INIT  0xFFFFU

/**
 * @brief  Compute CRC-16 CCITT over a buffer.
 * @param  buf   Pointer to data
 * @param  len   Number of bytes
 * @return 16-bit CRC value
 *
 * Polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
 * Initial value: 0xFFFF
 * No final XOR.
 */
uint16_t CRC16_Calc(const uint8_t *buf, uint16_t len);

/**
 * @brief  Feed a single byte into a running CRC.
 * @param  crc   Current CRC accumulator value
 * @param  byte  Next data byte
 * @return Updated CRC value
 *
 * Use this for incremental CRC calculation (byte-at-a-time as data
 * arrives) rather than buffering the entire packet first.
 */
uint16_t CRC16_Update(uint16_t crc, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
