// license:BSD-3-Clause
// copyright-holders: Jean-Francois DEL NERO
//
// Minitel 2 (NFZ 400) by Philips, extracted from MAME 0.288
// (src/mame/philips/minitel_2_rpic.cpp).
//
// The Minitel is a small, on-line computer/Videotex terminal with
// multi-services that can be connected to any French telephone line. This
// terminal was widely used in France during the 80's and 90's.
//
// More Minitel hardware related information is available on this page:
// http://hxc2001.free.fr/minitel
//
// What is implemented and working:
//
//  - Main MCU (Intel 80C32)
//  - Video output (TS9347)
//  - Keyboard
//  - 24C02 EEPROM
//
// What MAME's driver has and this build drops:
//
//  - The modem and the rear serial port (prise peri-informatique). MAME
//    configures both RS232 ports with no device plugged in, which leaves RXD
//    idle high on each; that is reproduced here as a constant, so what the
//    firmware reads on P1/P3 and on INT1 is unchanged.
//  - Sound output, which MAME does not emulate either (MACHINE_NO_SOUND).

#ifndef MINITEL_MINITEL_H
#define MINITEL_MINITEL_H

#pragma once

#include "ef9345.h"
#include "i2cmem.h"
#include "mcs51.h"
#include "types.h"

#include <cstddef>

class minitel_machine
{
public:
	// Screen configuration, from the driver's machine_config
	static constexpr int SCREEN_WIDTH  = 512;
	static constexpr int SCREEN_HEIGHT = 312;

	// MAME's driver says set_refresh_hz(60), but 312 lines is the European
	// 625/50 standard -- a 60 Hz field is 262 lines. The Minitel is a French
	// 50 Hz machine, and the difference is not academic: the dino game spins
	// on the TS9347's VSYNC bit, so its frame takes a whole number of video
	// frames. Its ~35 ms of work fits in two 20 ms frames but needs three at
	// 16.7 ms, which drops it from 25 fps to 20. 50 is therefore the default;
	// set_refresh_hz() exists so MAME's value can be reproduced on demand.
	static constexpr int DEFAULT_REFRESH_HZ = 50;
	static constexpr u32 CPU_CLOCK     = 14318181; // XTAL, verified on pcb

	// charset must point at the 8K TS9347 character generator ROM and outlive
	// the machine.
	explicit minitel_machine(const u8 *charset);

	// A cartridge replaces the whole program space -- this is the ROM
	// emulation board sitting in the Minitel's ROM socket, and it is the only
	// way a program gets in here. The France Telecom BIOS is not emulated.
	void load_cart(const u8 *data, std::size_t size);

	void reset();

	// The video rate. This rescales the timebase rather than the clock: the
	// cycle budget per scanline scales with it, so the CPU still runs at
	// CPU_CLOCK either way. What changes is how much work fits between two
	// VSYNCs, which is what a program spinning on the VSYNC flag feels.
	// Out-of-range values are ignored, so refresh_hz() is the rate in effect.
	void set_refresh_hz(int hz);
	int refresh_hz() const { return m_refresh_hz; }

	// Run one 60 Hz video frame.
	void run_frame();

	// Framebuffer: BGRA/ARGB32 pixels, row stride in pixels, cropped to the
	// visible area the video chip currently reports.
	const u32 *framebuffer() const;
	int fb_width() const;
	int fb_height() const;
	int fb_stride() const;

	// The Minitel 2's tube is monochrome, and its eight pens are the grey levels
	// it shows the eight Videotex colors as. Color mode substitutes the
	// colors themselves -- what a Minitel 1 Couleur would have made of the
	// same data.
	void set_color(bool color);
	bool color() const { return m_color; }

	// Keyboard matrix: 16 rows of 8 active-low bits, of which nine are wired up.
	// Rows the Minitel leaves empty read back as 0, as they do in MAME.
	void set_key(int row, int bit, bool pressed);
	void release_all_keys();
	static bool row_populated(int row);

	// 24C02 contents, for the host to persist between sessions
	u8 *nvram() { return m_i2cmem.data(); }
	static constexpr std::size_t NVRAM_SIZE = i2c_24c02_device::DATA_SIZE;

private:
	// 14174 Control register bits definition
	enum
	{
		CTRL_REG_MCBC      = 1 << 0,
		CTRL_REG_DTMF      = 1 << 1,
		CTRL_REG_CRTON     = 1 << 3,
		CTRL_REG_OPTO      = 1 << 4,
		CTRL_REG_LINERELAY = 1 << 5,
	};

	// 80C32 Port IO usage definitions
	enum
	{
		PORT_1_KBSERIN  = 1 << 0,
		PORT_1_MDM_DCDn = 1 << 1,
		PORT_1_MDM_PRD  = 1 << 2,
		PORT_1_MDM_TXD  = 1 << 3,
		PORT_1_MDM_RTS  = 1 << 4,
		PORT_1_KBLOAD   = 1 << 5,
		PORT_1_SCL      = 1 << 6,
		PORT_1_SDA      = 1 << 7,
	};

	enum
	{
		PORT_3_SER_RXD = 1 << 0,
		PORT_3_SER_TXD = 1 << 1,
		PORT_3_SER_ZCO = 1 << 2,
		PORT_3_MDM_RXD = 1 << 3,
		PORT_3_SER_RDY = 1 << 5,
	};

	// address decoding, from the driver's mem_prg / mem_data
	u8 mem_prg_r(offs_t offset);
	u8 mem_data_r(offs_t offset);
	void mem_data_w(offs_t offset, u8 data);

	void port1_w(u8 data);
	void port3_w(u8 data);
	u8 port1_r();
	u8 port3_r();

	void update_modem_state();

	void dev_ctrl_reg_w(offs_t offset, u8 data);
	u8 dev_keyb_ser_r(offs_t offset);

	void run_scanline(int scanline);

	mcs51_cpu_device m_maincpu;
	ts9347_device m_ts9347;
	i2c_24c02_device m_i2cmem;

	// the ROM socket, which shadows the whole program space when populated
	u8 m_cart[0x10000];
	std::size_t m_cart_size = 0;

	u8 m_io_kbd[16];

	u8 port1 = 0, port3 = 0;

	int keyboard_para_ser = 0;
	u8 keyboard_x_row_reg = 0;

	u8 last_ctrl_reg = 0;

	int lineconnected = 0;

	bool m_color = false;

	// Fake 1300 Hz carrier on INT0, emulating the modem ZCO output. MAME uses
	// a CLOCK device with a 384 us pulse width; the signal starts low, so the
	// first rising edge is one low period in.
	static constexpr u64 CARRIER_HIGH_NS = 384000;
	static constexpr u64 CARRIER_LOW_NS  = 1000000000ull / 1300 - CARRIER_HIGH_NS;

	int m_carrier_signal = 0;
	u64 m_carrier_edge_ns = CARRIER_LOW_NS;

	// Machine clock. Both counters carry their remainder so neither the
	// scanline period nor the CPU cycle count drifts.
	// The video rate and the two divisors derived from it. Held rather than
	// recomputed because run_scanline() divides by them twice per line.
	int m_refresh_hz = DEFAULT_REFRESH_HZ;
	u32 m_scanlines_per_second = u32(DEFAULT_REFRESH_HZ) * SCREEN_HEIGHT;
	u32 m_cycle_divisor = 12u * u32(DEFAULT_REFRESH_HZ) * SCREEN_HEIGHT;

	u64 m_time_ns = 0;
	u32 m_ns_frac = 0;
	u32 m_cycle_frac = 0;

	// execute_run may overshoot the cycles it was asked for; carry the excess.
	int m_cycle_debt = 0;

	void run_cpu(int cycles);
};

#endif // MINITEL_MINITEL_H
