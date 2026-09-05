// license:BSD-3-Clause
// copyright-holders: Jean-Francois DEL NERO
//
// Minitel 2 (NFZ 400) by Philips, extracted from MAME 0.288
// (src/mame/philips/minitel_2_rpic.cpp). See minitel.h.

#include "minitel.h"

#include <algorithm>
#include <cstring>

namespace {

// The driver's screen.set_visarea(2, 512-10, 0, 278-1). The video chip narrows
// max_x to the width of the mode it is in.
constexpr ts9347_device::rectangle SCREEN_VISAREA = { 2, 512 - 10, 0, 278 - 1 };

// The Videotex color attributes, ESC 0x40-0x47, select these eight in this
// order. MAME's driver sets the palette to the grey levels a monochrome tube
// shows them as, which are their Rec. 601 luminances rounded up:
//
//   black 0   red 76->80    green 150->160   yellow 226->230
//   blue 29->40   magenta 105->120   cyan 179->200   white 255
constexpr u8 TELETEL_RGB[8][3] = {
	{   0,   0,   0 },   // 0x40 noir
	{ 255,   0,   0 },   // 0x41 rouge
	{   0, 255,   0 },   // 0x42 vert
	{ 255, 255,   0 },   // 0x43 jaune
	{   0,   0, 255 },   // 0x44 bleu
	{ 255,   0, 255 },   // 0x45 magenta
	{   0, 255, 255 },   // 0x46 cyan
	{ 255, 255, 255 }    // 0x47 blanc
};

constexpr u8 TELETEL_GREY[8] = { 0, 80, 160, 230, 40, 120, 200, 255 };

// Scanline timer: TIMER(...).configure_scanline(..., "screen", 0, 10)
constexpr int SCANLINE_TIMER_PERIOD = 10;

} // anonymous namespace


minitel_machine::minitel_machine(const u8 *charset)
	: m_ts9347(charset, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_VISAREA)
{
	std::memset(m_cart, 0xff, sizeof(m_cart));

	// machine_start: the eight grey levels the Minitel displays
	set_color(false);

	m_maincpu.program_r = [this](offs_t a) { return mem_prg_r(a); };
	m_maincpu.xdata_r   = [this](offs_t a) { return mem_data_r(a); };
	m_maincpu.xdata_w   = [this](offs_t a, u8 d) { mem_data_w(a, d); };

	m_maincpu.port_in_cb[1]  = [this]() -> u8 { return port1_r(); };
	m_maincpu.port_out_cb[1] = [this](u8 d) { port1_w(d); };
	m_maincpu.port_in_cb[3]  = [this]() -> u8 { return port3_r(); };
	m_maincpu.port_out_cb[3] = [this](u8 d) { port3_w(d); };

	m_ts9347.start();

	release_all_keys();
	reset();
}


void minitel_machine::set_color(bool color)
{
	m_color = color;

	for (int i = 0; i < 8; i++)
	{
		if (color)
			m_ts9347.m_pen[i] = rgb_t(TELETEL_RGB[i][0], TELETEL_RGB[i][1], TELETEL_RGB[i][2]);
		else
			m_ts9347.m_pen[i] = rgb_t(TELETEL_GREY[i], TELETEL_GREY[i], TELETEL_GREY[i]);
	}

	// PALETTE(...).set_entries(8 + 1); the spare entry is never drawn
	m_ts9347.m_pen[8] = rgb_t(0, 0, 0);
}


void minitel_machine::load_cart(const u8 *data, std::size_t size)
{
	m_cart_size = std::min(size, sizeof(m_cart));
	std::memset(m_cart, 0xff, sizeof(m_cart));
	std::memcpy(m_cart, data, m_cart_size);
}


// Both divisors follow from the rate, so they are recomputed together here
// rather than at each use. An MCS-51 machine cycle is 12 oscillator periods.
void minitel_machine::set_refresh_hz(int hz)
{
	// Zero would divide by zero in run_scanline(); the upper bound only exists
	// to keep m_cycle_divisor (12 * hz * 312) inside 32 bits.
	if (hz < 1 || hz > 1000)
		return;

	m_refresh_hz = hz;
	m_scanlines_per_second = u32(hz) * SCREEN_HEIGHT;
	m_cycle_divisor = 12u * m_scanlines_per_second;

	// The accumulators hold remainders scaled to the old divisors, so they
	// mean nothing under the new ones.
	m_ns_frac = 0;
	m_cycle_frac = 0;
}

void minitel_machine::reset()
{
	m_i2cmem.reset();
	m_ts9347.reset();

	port1 = 0;
	// The rear serial port has nothing plugged into it, so its RXD sits at the
	// idle mark; MAME's rs232_port_device reports that at reset.
	port3 = PORT_3_SER_RXD;

	keyboard_para_ser = 0;
	keyboard_x_row_reg = 0;
	last_ctrl_reg = 0;
	lineconnected = 0;

	m_carrier_signal = 0;
	m_carrier_edge_ns = CARRIER_LOW_NS;

	m_time_ns = 0;
	m_ns_frac = 0;
	m_cycle_frac = 0;
	m_cycle_debt = 0;

	m_ts9347.set_time(0);
	m_maincpu.reset();
}


/***************************************************************************
    ADDRESS DECODING
***************************************************************************/

u8 minitel_machine::mem_prg_r(offs_t offset)
{
	// machine_start: if populated, this ROM slot replaces the main ROM over
	// the whole program space. A linear slot mirrors a short ROM.
	if (m_cart_size)
		return m_cart[offset % m_cart_size];

	// mem_prg: map(0x0000, 0x7fff).rom(). With no cartridge there is no ROM
	// region to read, so the whole window reads back as ROMREGION_ERASEFF did.
	return (offset < 0x8000) ? 0xff : 0x00;
}

u8 minitel_machine::mem_data_r(offs_t offset)
{
	// mem_data: two windows, everything else reads back as an unmapped 0
	if (offset >= 0x2000 && offset < 0x4000)
		return dev_keyb_ser_r(offset - 0x2000);
	if (offset >= 0x4000 && offset < 0x6000)
		return m_ts9347.data_r(offset - 0x4000);
	return 0x00;
}

void minitel_machine::mem_data_w(offs_t offset, u8 data)
{
	if (offset >= 0x2000 && offset < 0x4000)
		dev_ctrl_reg_w(offset - 0x2000, data);
	else if (offset >= 0x4000 && offset < 0x6000)
		m_ts9347.data_w(offset - 0x4000, data);
}


/***************************************************************************
    PORT I/O
***************************************************************************/

void minitel_machine::port1_w(u8 data)
{
	// PORT_1_MDM_TXD would drive the modem here, but no device is plugged into
	// the modem port.

	if ((port1 ^ data) & PORT_1_KBLOAD)
	{
		if (data & PORT_1_KBLOAD)
			keyboard_para_ser = 1;
		else
			keyboard_para_ser = 0;
	}

	if ((port1 ^ data) & PORT_1_SCL)
		m_i2cmem.write_scl((data & PORT_1_SCL) ? 1 : 0);

	if ((port1 ^ data) & PORT_1_SDA)
		m_i2cmem.write_sda((data & PORT_1_SDA) ? 1 : 0);

	port1 = data;
}

void minitel_machine::port3_w(u8 data)
{
	// PORT_3_SER_TXD would drive the rear serial port, which is unconnected.

	port3 = (port3 & PORT_3_SER_RXD) | (data & ~PORT_3_SER_RXD);
}

void minitel_machine::update_modem_state()
{
	// Main transmission mode: PORT_1_MDM_RTS = 0, CTRL_REG_DTMF = 1, CTRL_REG_MCBC = 0
	if (last_ctrl_reg & CTRL_REG_LINERELAY)
		lineconnected = 1;
	else
		lineconnected = 0;
}

u8 minitel_machine::port1_r()
{
	u8 data;

	data = (((port1 & (0xFE & ~PORT_1_SDA)) | ((keyboard_x_row_reg >> 7) & 1)));
	data |= (m_i2cmem.read_sda() ? PORT_1_SDA : 0);

	update_modem_state();

	if (lineconnected)
		data &= ~PORT_1_MDM_DCDn;
	else
		data |= PORT_1_MDM_DCDn;

	return data;
}

u8 minitel_machine::port3_r()
{
	update_modem_state();

	return (port3 & ~(PORT_3_SER_RDY)); // External port ready state
}

void minitel_machine::dev_ctrl_reg_w(offs_t offset, u8 data)
{
	last_ctrl_reg = data;
}

u8 minitel_machine::dev_keyb_ser_r(offs_t offset)
{
	if (keyboard_para_ser)
	{
		// load the 4014 with the keyboard row state
		keyboard_x_row_reg = m_io_kbd[(offset >> 8) & 0xF];
	}
	else
	{
		// shift the keyboard register...
		keyboard_x_row_reg = keyboard_x_row_reg << 1;
	}

	return 0xFF;
}


/***************************************************************************
    KEYBOARD

    Sixteen ioports, of which nine carry keys: Y0, Y1, Y2 and then every second
    one up to Y14. MAME reads an ioport as (pressed bits) ^ (sum of the fields'
    default values), so a row of eight active-low fields idles at 0xff, while
    the seven rows the driver declares empty read back as 0. The firmware only
    ever scans the populated nine.
***************************************************************************/

bool minitel_machine::row_populated(int row)
{
	return row == 0 || row == 1 || (row >= 2 && row <= 14 && (row & 1) == 0);
}

void minitel_machine::release_all_keys()
{
	for (int row = 0; row < 16; row++)
		m_io_kbd[row] = row_populated(row) ? 0xff : 0x00;
}

void minitel_machine::set_key(int row, int bit, bool pressed)
{
	if (bit < 0 || bit > 7 || row < 0 || row > 15 || !row_populated(row))
		return;

	if (pressed)
		m_io_kbd[row] &= ~(1 << bit);
	else
		m_io_kbd[row] |= (1 << bit);
}


/***************************************************************************
    SCHEDULING
***************************************************************************/

void minitel_machine::run_cpu(int cycles)
{
	cycles -= m_cycle_debt;
	if (cycles <= 0)
	{
		m_cycle_debt = -cycles;
		return;
	}

	// the core runs whole instructions, so it can overshoot the budget
	m_cycle_debt = m_maincpu.execute_run(cycles) - cycles;
}

void minitel_machine::run_scanline(int scanline)
{
	if ((scanline % SCANLINE_TIMER_PERIOD) == 0)
		m_ts9347.update_scanline(u16(scanline));

	// How long this scanline lasts, and how many CPU cycles fit in it. Both
	// carry a remainder, so one second's worth of scanlines adds up to exactly
	// one second and to exactly CPU_CLOCK / 12 machine cycles, whatever the
	// video rate is.
	u64 line_ns = 1000000000u / m_scanlines_per_second;
	m_ns_frac += 1000000000u % m_scanlines_per_second;
	if (m_ns_frac >= m_scanlines_per_second)
	{
		m_ns_frac -= m_scanlines_per_second;
		line_ns++;
	}

	int line_cycles = int(CPU_CLOCK / m_cycle_divisor);
	m_cycle_frac += CPU_CLOCK % m_cycle_divisor;
	if (m_cycle_frac >= m_cycle_divisor)
	{
		m_cycle_frac -= m_cycle_divisor;
		line_cycles++;
	}

	// Run the scanline, stopping at each edge of the fake 1300 Hz carrier so
	// INT0 is asserted and cleared at the right times.
	u64 const line_start = m_time_ns;
	u64 const line_end = line_start + line_ns;
	int issued = 0;

	while (m_carrier_edge_ns < line_end)
	{
		int const want = int((u64(line_cycles) * (m_carrier_edge_ns - line_start)) / line_ns) - issued;
		if (want > 0)
		{
			run_cpu(want);
			issued += want;
		}

		m_carrier_signal ^= 1;
		m_maincpu.set_input_line(MCS51_INT0_LINE, m_carrier_signal ? ASSERT_LINE : CLEAR_LINE);
		m_carrier_edge_ns += m_carrier_signal ? CARRIER_HIGH_NS : CARRIER_LOW_NS;
	}

	run_cpu(line_cycles - issued);

	m_time_ns = line_end;
	m_ts9347.set_time(m_time_ns);
}

void minitel_machine::run_frame()
{
	for (int scanline = 0; scanline < SCREEN_HEIGHT; scanline++)
		run_scanline(scanline);
}


/***************************************************************************
    VIDEO OUTPUT
***************************************************************************/

const u32 *minitel_machine::framebuffer() const
{
	auto const area = m_ts9347.visible_area();
	auto const &bitmap = m_ts9347.bitmap();
	return bitmap.raw() + std::size_t(area.min_y) * bitmap.width() + area.min_x;
}

int minitel_machine::fb_width() const
{
	auto const area = m_ts9347.visible_area();
	return std::min(area.max_x, m_ts9347.bitmap().width() - 1) - area.min_x + 1;
}

int minitel_machine::fb_height() const
{
	auto const area = m_ts9347.visible_area();
	return std::min(area.max_y, m_ts9347.bitmap().height() - 1) - area.min_y + 1;
}

int minitel_machine::fb_stride() const
{
	return m_ts9347.bitmap().width();
}
