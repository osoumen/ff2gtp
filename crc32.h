/**
 * @file crc32.h
 * @brief 
 * @author osoumen
 * @date 2019/01/03
 * @copyright 
 */

#ifndef crc32_h
#define crc32_h

#include <stdint.h>

uint32_t	CalcCrc32(const char *str);
uint32_t	CalcCrc32(const uint8_t *str, uint32_t buf_len);

#endif /* crc32_h */
