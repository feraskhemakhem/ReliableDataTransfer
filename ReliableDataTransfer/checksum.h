/*
 * checsum.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */
#pragma once

#include "stdafx.h"

class Checksum {
	DWORD crc_table[CHKSM_TBLE_SIZE];
public:
	Checksum();
	DWORD CRC32(unsigned char *buf, size_t len);
};