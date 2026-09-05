// license:BSD-3-Clause
// copyright-holders:smf
//
// 24C02 I2C memory, extracted from MAME 0.288
// (src/devices/machine/i2cmem.{h,cpp}).
//
// MAME's i2cmem_device covers seventeen parts through a shared bit-banged state
// machine parameterised at construction. Only the 24C02 fitted to the Minitel 2
// is kept here, so its parameters -- 256 bytes, an 8-byte write page, no read
// page wrap, slave address 0xa0, chip-select pins tied low and write control
// permanently enabled -- are constants and the branches for the other parts are
// gone. The protocol handling itself is unchanged.

#ifndef MINITEL_I2CMEM_H
#define MINITEL_I2CMEM_H

#pragma once

#include "types.h"

class i2c_24c02_device
{
public:
	static constexpr int DATA_SIZE = 0x100;
	static constexpr int WRITE_PAGE_SIZE = 8;
	static constexpr int SLAVE_ADDRESS = 0xa0;

	i2c_24c02_device() { reset(); }

	// I/O operations
	void write_sda(int state);
	void write_scl(int state);
	int read_sda() const;

	void reset();

	// NVRAM contents, so the host can persist them
	u8 *data() { return m_data; }
	const u8 *data() const { return m_data; }

private:
	enum {
		STATE_IDLE = 0,
		STATE_DEVSEL,
		STATE_ADDRESSLOW,
		STATE_DATAIN,
		STATE_READSELACK,
		STATE_DATAOUT,
		STATE_RESET
	};

	enum { DEVSEL_RW = 1, DEVSEL_ADDRESS = 0xfe };

	static constexpr int address_mask() { return DATA_SIZE - 1; }

	// With a 256-byte part the byte address fits in one packet, so the device
	// is selected on the slave address alone (E0-E2 are tied low here).
	bool select_device() const
	{
		constexpr int mask = DEVSEL_ADDRESS & ~(address_mask() >> 7);
		return (m_devsel & mask) == (SLAVE_ADDRESS & mask);
	}

	int data_offset() const { return m_byteaddr & address_mask(); }

	u8 m_data[DATA_SIZE];
	int m_scl;
	int m_sdaw;
	int m_sdar;
	int m_state;
	int m_bits;
	int m_shift;
	int m_devsel;
	int m_byteaddr;
	u8 m_page[WRITE_PAGE_SIZE];
	int m_page_offset;
	int m_page_written_size;
};

#endif // MINITEL_I2CMEM_H
