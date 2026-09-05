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
//  - Sound output: the TS7514 modem's monitor path, which is the only thing
//    on this machine wired to a speaker.
//
// What MAME's driver has and this build drops:
//
//  - The modem and the rear serial port (prise peri-informatique) as serial
//    links. MAME configures both RS232 ports with no device plugged in, which
//    leaves RXD idle high on each; that is reproduced here as a constant, so
//    what the firmware reads on P1/P3 and on INT1 is unchanged. The modem's
//    audio side is emulated -- see the sound section below.

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

	// SOUND
	//
	// The Minitel's speaker is the modem's monitor output: the TS7514 line
	// interface can route what it is transmitting or receiving to it, and the
	// firmware uses that to make the machine audible -- dialling tones, the
	// call progress it plays back while connecting, and, for a program that
	// drives the chip itself, DTMF tones as a crude four-by-four tone
	// generator. MAME models the two sources the monitor can carry: the DTMF
	// pair selected by RDTMF, and the fixed "signalling frequency" beep.
	//
	// Samples accumulate as the machine runs, at whatever rate was set, and
	// the host drains them after each frame. They accumulate continuously,
	// silence included, so a quiet machine yields a frame's worth of zeroes
	// rather than nothing -- which is what lets a host keep one unbroken
	// output timeline instead of splicing tones into it.

	static constexpr int DEFAULT_AUDIO_RATE = 48000;

	// MAME fixes the modem stream at 48 kHz. Making it settable lets a host
	// generate directly at its output rate and resample nothing; both tone
	// generators derive their phase step from it, so any rate works.
	// Out-of-range values are ignored, so audio_rate() is the rate in effect.
	void set_audio_rate(int hz);
	int audio_rate() const { return m_audio_rate; }

	// Move up to max samples out of the buffer, oldest first, and return how
	// many were moved. Samples are mono and nominally in [-1, +1], the range
	// MAME's stream works in.
	std::size_t audio_read(float *dst, std::size_t max);
	std::size_t audio_available() const { return std::size_t(m_audio_written - m_audio_taken); }

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
	void modem_exec_command();

	// Bring the audio buffer up to m_time_ns using the state in effect over
	// the span, then let the caller change that state -- the same discipline
	// as MAME's stream->update() calls, and for the same reason.
	void sound_update();
	void sound_generate(std::size_t samples);
	void sound_push(float sample);

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

	// TS7514 registers, as far as the monitor output needs them. The chip
	// takes commands over a shift register: with DTMF and MCBC both low it is
	// in control mode, PRD carries data and each falling edge of RTS clocks
	// one bit in; leaving control mode executes what was shifted in. RWLO
	// selects what the monitor listens to, RDTMF which of the sixteen DTMF
	// pairs to send, RPTF which halves of the pair the output filter passes.
	// RWLO powers up at 0xF, which is "monitor nothing".
	u8 modem_input_reg = 0;
	u8 modem_rdtmf_reg = 0;
	u8 modem_rwlo_reg = 0xF;
	u8 modem_rptf_reg = 0;

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

	// AUDIO
	//
	// Generation is driven off m_time_ns, which advances a scanline at a time,
	// so a change to the modem state takes effect at the scanline boundary it
	// falls in rather than at the exact sample. That is 64 us at 50 Hz against
	// tones that last tens of milliseconds, and it is why sound_update() can
	// be a cheap call at every state change.
	//
	// The buffer holds a third of a second, which no host should ever need:
	// it is drained once per emulated frame, and overrunning it means the
	// machine ran without anything listening. In that case the oldest samples
	// go, so what the host finally reads is the most recent audio rather than
	// a stale third of a second.
	static constexpr std::size_t AUDIO_BUFFER_SIZE = 16384;

	int m_audio_rate = DEFAULT_AUDIO_RATE;
	u64 m_audio_time_ns = 0;   // emulated time the buffer has been filled to
	u64 m_audio_frac = 0;      // and the sub-sample remainder of that

	float m_audio_buffer[AUDIO_BUFFER_SIZE];
	u64 m_audio_written = 0;   // monotonic; the buffer index is these mod SIZE
	u64 m_audio_taken = 0;

	double modem_dtmf_phase1 = 0, modem_dtmf_phase2 = 0, modem_beep_phase = 0;

	void run_cpu(int cycles);
};

#endif // MINITEL_MINITEL_H
