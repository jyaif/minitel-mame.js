// license:BSD-3-Clause
// copyright-holders: Jean-Francois DEL NERO
//
// Minitel 2 (NFZ 400) by Philips, extracted from MAME 0.288
// (src/mame/philips/minitel_2_rpic.cpp). See minitel.h.

#include "minitel.h"

#include <algorithm>
#include <cstring>

namespace {

// std::numbers::pi is C++20 and this builds as C++17.
constexpr double PI = 3.14159265358979323846;

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

// sin(x) for x in [0, 2*PI), without libm.
//
// std::sin is the only thing in this build that would pull the C math library
// into the module, and it costs about 7 KB of wasm -- a sixth of the whole
// thing -- to generate two sine waves. This folds the quadrants onto
// [-PI/2, PI/2] and evaluates the Taylor series there, where eleventh order is
// enough for a worst-case error of 6e-8: 145 dB below the tone it is making,
// and some 120 dB below the 16-bit sample it ends up in.
double sine(double x)
{
	if (x > PI)
		x -= 2.0 * PI;               // [-PI, PI]
	if (x > PI / 2)
		x = PI - x;                  // and the two outer quadrants reflect
	else if (x < -PI / 2)
		x = -PI - x;                 // onto the two inner ones

	double const x2 = x * x;
	return x * (1.0 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040
	         + x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800))))));
}

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

	// The TS7514 has no reset line on this board, so MAME's driver carries its
	// registers across a soft reset. Here reset() is also how a freshly loaded
	// ROM starts, and a machine that came up mid-tone would be a surprise, so
	// they go back to their power-up values: monitor nothing, no tone.
	modem_input_reg = 0;
	modem_rdtmf_reg = 0;
	modem_rwlo_reg = 0xF;
	modem_rptf_reg = 0;

	modem_dtmf_phase1 = 0;
	modem_dtmf_phase2 = 0;
	modem_beep_phase = 0;

	m_time_ns = 0;
	m_ns_frac = 0;
	m_cycle_frac = 0;
	m_cycle_debt = 0;

	// Whatever was buffered belongs to the machine that just went away.
	m_audio_time_ns = 0;
	m_audio_frac = 0;
	m_audio_written = 0;
	m_audio_taken = 0;

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

	if ((port1 ^ data) & PORT_1_MDM_RTS)
	{
		// Generate audio until now using the old RTS value.
		sound_update();

		// If the modem is in control mode (DTMF=MCBC=0), PRD feeds the input
		// shift register (clocked by RTS) that receives the next command to
		// execute.
		bool in_control_mode = (last_ctrl_reg & (CTRL_REG_DTMF | CTRL_REG_MCBC)) == 0;
		bool rts_falling_edge = (data & PORT_1_MDM_RTS) == 0;
		if (in_control_mode && rts_falling_edge)
			modem_input_reg = ((data & PORT_1_MDM_PRD) ? 0x80 : 0) | (modem_input_reg >> 1);
	}

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
	if (last_ctrl_reg != data)
	{
		if ((last_ctrl_reg ^ data) & (CTRL_REG_DTMF | CTRL_REG_MCBC))
		{
			// Generate audio until now using the old DTMF and MCBC values.
			sound_update();

			// If leaving control mode (no longer DTMF=MCBC=0), execute the
			// command that was shifted in.
			bool was_control_mode = (last_ctrl_reg & (CTRL_REG_DTMF | CTRL_REG_MCBC)) == 0;
			if (was_control_mode)
				modem_exec_command();
		}

		last_ctrl_reg = data;
	}
}

void minitel_machine::modem_exec_command()
{
	u8 addr = (modem_input_reg >> 4) & 0x7;
	u8 val = modem_input_reg & 0xf;

	switch (addr)
	{
		case 0x1:
			modem_rdtmf_reg = val;
			break;
		case 0x3:
			modem_rwlo_reg = val;
			break;
		case 0x4:
			modem_rptf_reg = val;
			break;
		default:
			logerror("Unimplemented TS7514 addr: %X val: %X\n", addr, val);
	}
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
    SOUND

    The TS7514's monitor output, which is what the Minitel's speaker is wired
    to. Two sources can reach it: the DTMF pair the chip is dialling with, and
    a fixed "signalling frequency" beep. RWLO says which -- and the ranges do
    not overlap, so at most one is audible at a time.
***************************************************************************/

void minitel_machine::set_audio_rate(int hz)
{
	// The lower bound keeps the tones below Nyquist; the upper one keeps
	// one scanline's worth of samples a sane number.
	if (hz < 8000 || hz > 96000)
		return;

	m_audio_rate = hz;
	m_audio_frac = 0;
}

void minitel_machine::sound_push(float sample)
{
	m_audio_buffer[m_audio_written % AUDIO_BUFFER_SIZE] = sample;
	m_audio_written++;

	// Nobody is draining. Drop the oldest sample rather than the newest, so
	// what is left is the audio nearest to now.
	if (m_audio_written - m_audio_taken > AUDIO_BUFFER_SIZE)
		m_audio_taken = m_audio_written - AUDIO_BUFFER_SIZE;
}

std::size_t minitel_machine::audio_read(float *dst, std::size_t max)
{
	std::size_t n = std::min(audio_available(), max);

	for (std::size_t i = 0; i < n; i++)
		dst[i] = m_audio_buffer[(m_audio_taken + i) % AUDIO_BUFFER_SIZE];

	m_audio_taken += n;
	return n;
}

// How many samples the elapsed time is worth, with the remainder carried so
// the sample clock neither drifts nor rounds a whole scanline away.
void minitel_machine::sound_update()
{
	if (m_time_ns <= m_audio_time_ns)
		return;

	u64 const ticks = (m_time_ns - m_audio_time_ns) * u64(m_audio_rate) + m_audio_frac;
	m_audio_time_ns = m_time_ns;
	m_audio_frac = ticks % 1000000000ull;

	sound_generate(std::size_t(ticks / 1000000000ull));
}

// MAME's sound_stream_update(), writing into the buffer instead of a stream.
void minitel_machine::sound_generate(std::size_t samples)
{
	// In real hardware, DTMF tones are emitted on the phone line if DTMF=0,
	// MCBC=1 and RTS=0. Given we only emulate the monitor output on the
	// speaker, we also require that the transmit signal is selected for
	// monitoring in RWLO.
	bool dtmf_active =
		(last_ctrl_reg & CTRL_REG_DTMF) == 0 &&
		(last_ctrl_reg & CTRL_REG_MCBC) != 0 &&
		(port1 & PORT_1_MDM_RTS) == 0 &&
		modem_rwlo_reg <= 3;

	// Tests if the "signalling-frequency" is selected for monitoring in RWLO.
	// Note that "beep_active" and "dtmf_active" are mutually exclusive due to
	// the different RWLO ranges.
	bool beep_active = 8 <= modem_rwlo_reg && modem_rwlo_reg <= 11;

	if (!dtmf_active)
	{
		modem_dtmf_phase1 = 0;
		modem_dtmf_phase2 = 0;
	}

	if (!beep_active)
		modem_beep_phase = 0;

	// The two frequencies selected by RDTMF.
	const double LOW_FREQS[4] = { 697, 770, 852, 941 };
	const double HIGH_FREQS[4] = { 1209, 1336, 1477, 1633 };
	double const rate1 = 2.0 * PI * LOW_FREQS[bitswap<2>(modem_rdtmf_reg, 1, 0)] / m_audio_rate;
	double const rate2 = 2.0 * PI * HIGH_FREQS[bitswap<2>(modem_rdtmf_reg, 3, 2)] / m_audio_rate;

	// The fixed frequency.
	const double BEEP_FREQ = 2982;
	double const beep_rate = 2.0 * PI * BEEP_FREQ / m_audio_rate;

	for (std::size_t i = 0; i < samples; i++)
	{
		// A stream MAME has not written to is silent, and so is this.
		float out = 0;

		if (dtmf_active)
		{
			double val = 0;
			if (modem_rptf_reg != 0x4) // unless high-only filtered
				val += sine(modem_dtmf_phase1);
			if (modem_rptf_reg != 0x8) // unless low-only filtered
				val += sine(modem_dtmf_phase2);
			out = float(0.5 * val); // mixed sine waves
			// MAME wraps these with fmod(); a step is always well under a
			// full turn, so one subtraction is the same thing.
			modem_dtmf_phase1 += rate1;
			if (modem_dtmf_phase1 >= 2.0 * PI) modem_dtmf_phase1 -= 2.0 * PI;
			modem_dtmf_phase2 += rate2;
			if (modem_dtmf_phase2 >= 2.0 * PI) modem_dtmf_phase2 -= 2.0 * PI;
		}

		if (beep_active)
		{
			out = modem_beep_phase >= PI ? +1.0f : -1.0f; // square wave
			modem_beep_phase += beep_rate;
			if (modem_beep_phase >= 2.0 * PI) modem_beep_phase -= 2.0 * PI;
		}

		sound_push(out);
	}
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
	sound_update();
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
