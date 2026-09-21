// license:GPL-2.0+
// copyright-holders:Daniel Coulom,Sandro Ronco
//
// Thomson TS9347 video controller, extracted from MAME 0.288
// (src/devices/video/ef9345.{h,cpp}), which emulates the EF9345 and its TS9347
// variant together. Only the TS9347 is kept here -- it is the part the Minitel
// 2 fits -- so the two virtual hooks the variants differed on (parse_video_mode
// and indexrow) are the TS9347 versions, called directly.
//
// The rendering code is used unchanged. What MAME's device framework provided
// around it is replaced here by plain members: the 16K video RAM address space
// becomes an array, the palette device becomes a pen table the machine fills
// in, the screen device becomes width/height/visible-area fields, and the two
// emu_timers become deadlines on a nanosecond clock the machine advances.

#ifndef MINITEL_EF9345_H
#define MINITEL_EF9345_H

#pragma once

#include "types.h"

#include <cstddef>
#include <tuple>

// A 32-bit RGB bitmap, standing in for MAME's bitmap_rgb32.
class bitmap_rgb32
{
public:
	bitmap_rgb32() = default;
	~bitmap_rgb32() { delete [] m_pixels; }

	bitmap_rgb32(const bitmap_rgb32 &) = delete;
	bitmap_rgb32 &operator=(const bitmap_rgb32 &) = delete;

	void allocate(int width, int height)
	{
		m_width = width;
		m_height = height;
		delete [] m_pixels;
		m_pixels = new u32[std::size_t(width) * height]();
	}

	void fill(u32 color)
	{
		for (std::size_t i = 0, n = std::size_t(m_width) * m_height; i < n; i++)
			m_pixels[i] = color;
	}

	u32 &pix(int y, int x) { return m_pixels[std::size_t(y) * m_width + x]; }

	const u32 *raw() const { return m_pixels; }
	int width() const { return m_width; }
	int height() const { return m_height; }

private:
	u32 *m_pixels = nullptr;
	int m_width = 0;
	int m_height = 0;
};


class ts9347_device
{
public:
	// The visible region of the frame buffer, as the screen device's visible
	// area would report it.
	struct rectangle { int min_x, max_x, min_y, max_y; };

	// charset points at the 8K character generator ROM and must outlive the
	// device; the geometry is the driver's screen configuration.
	ts9347_device(const u8 *charset, int screen_width, int screen_height, rectangle visarea);

	void start();
	void reset();

	// device interface
	u8 data_r(offs_t offset);
	void data_w(offs_t offset, u8 data);
	void update_scanline(u16 scanline);

	// the machine advances this clock; the busy and blink deadlines hang off it
	void set_time(u64 time_ns) { m_time_ns = time_ns; }

	const bitmap_rgb32 &bitmap() const { return m_screen_out; }
	rectangle visible_area() const { return m_visarea; }

	// palette, filled in by the machine (8 grey levels + 1 spare)
	rgb_t m_pen[9] = { };

	// TEXT
	//
	// What each character cell showed when it was last drawn, for a host that
	// wants the screen as text rather than as pixels. It is recorded as the
	// rows are rendered, so it follows every mode, latch and zoom the picture
	// does with no second decoder to keep in step. Rows are TEXT_STRIDE cells
	// apart whatever the mode; text_columns() says how many of them are in use.
	// Four bytes a cell:
	//
	//   [0] character code, 7 bits
	//   [1] bits 0-3 the character set as read_char() indexes it: 0 and 1 the
	//       alphanumerics, 2 the mosaics, 3 the 80-column extras, 8 and up
	//       characters redefined in video RAM. Bit 4 double width, bit 5
	//       double height, bit 6 the right half of a double-width character,
	//       bit 7 the lower half of a double-height one.
	//   [2] foreground colour in bits 0-2, background in bits 4-6
	//   [3] bit 0 flash, 1 conceal, 2 negative, 3 underline, 4 the cursor is
	//       on this cell and showing, 5 insert
	//
	// A row the chip shows as border reads as spaces.
	static constexpr int TEXT_ROWS = 25;
	static constexpr int TEXT_STRIDE = 80;

	const u8 *text_cells() const { return &m_text[0][0][0]; }
	int text_columns() const { return (m_char_mode == char_mode_t::MODE12x80 || m_char_mode == char_mode_t::MODE8x80) ? 80 : 40; }

	// The video RAM, which a program is free to use as working memory as well
	// as for the page it shows.
	u8 *vram() { return m_vram; }
	static constexpr std::size_t vram_size() { return VRAM_SIZE; }

private:
	enum class char_mode_t : u8 {
		// 40 column modes:
		MODE24x40, // long codes
		MODEVAR40, // variable codes
		MODE16x40, // short codes

		// 80 column modes:
		MODE8x80,  // long codes
		MODE12x80, // variable codes
	};

	// inline helpers
	u16 indexram(u8 r);
	void inc_x(u8 r);
	void inc_y(u8 r);

	void set_busy_flag(int period);
	void update_busy_flag();
	void set_video_mode();
	void init_accented_chars();
	u8 read_char(u8 index, u16 addr);
	u8 get_dial(u8 x, u8 attrib);
	void zoom(u8 *pix, u16 n);
	u16 indexblock(u16 x, u16 y);
	std::tuple<u8, u8, bool> makecolors(u8 c0, u8 c1, bool insert, bool flash, bool conceal, bool negative, bool cursor);

	char_mode_t parse_video_mode() const;

	// Computes the index of the memory row containing data for the y-th
	// screen row.
	u16 indexrow(u16 y);

	// Dispatch rendering of character (x, y) to one of the specialized
	// drawing functions (bichrome40/bichrome80).
	void makechar(u16 x, u16 y);
	void makechar_16x40(u16 x, u16 y);
	void makechar_24x40(u16 x, u16 y);
	void makechar_12x80(u16 x, u16 y);

	// Call draw_char_40/80 to draw the given character ** at (x + 1, y + 1) **.
	// Why at (x + 1, y + 1) and not just at (x, y)? Because we need to leave
	// some blank space at the top and at the left of the text area for the
	// margin.
	void bichrome40(u8 type, u16 address, u8 dial, u16 iblock, u16 x, u16 y, u8 c0, u8 c1, bool insert, bool flash, bool conceal, bool negative, bool underline);
	void bichrome80(u8 c, u8 a, u16 x, u16 y, bool cursor);

	void draw_char_40(u8 *c, u16 x, u16 y);
	void draw_char_80(u8 *c, u16 x, u16 y);
	void draw_border(u16 line);

	// Keep m_text up to date: one cell as it is drawn, or a cell or a whole row
	// that is showing nothing. zoom is byte [1]'s top four bits, which
	// dial_zoom() works out from get_dial()'s answer.
	void text_record(u16 x, u16 y, u8 code, u8 set, u8 zoom, u8 c0, u8 c1, u8 flags);
	void text_clear(u16 x, u16 y);
	void text_clear_row(u16 y);
	static u8 dial_zoom(u8 dial);

	void ef9345_exec(u8 cmd);

	// ---------------------------------------------------------------------
	// Video RAM. MAME maps 0x0000-0x3fff as RAM inside a 16-bit address space,
	// so anything above that reads back as 0 and swallows writes.
	// ---------------------------------------------------------------------
	static constexpr offs_t VRAM_SIZE = 0x4000;
	u8 m_vram[VRAM_SIZE] = { };

	u8 vram_r(offs_t addr) const { return (addr < VRAM_SIZE) ? m_vram[addr] : 0; }
	void vram_w(offs_t addr, u8 data) { if (addr < VRAM_SIZE) m_vram[addr] = data; }

	// ---------------------------------------------------------------------
	// Lazily evaluated stand-ins for the two emu_timers. A deadline of NEVER
	// means "scheduled but never firing", which is how MAME's timer reset()
	// leaves the busy flag latched until the next command.
	// ---------------------------------------------------------------------
	static constexpr u64 NEVER = ~u64(0);
	static constexpr u64 BLINK_PERIOD_NS = 500000000ull;

	u64 m_time_ns = 0;
	u64 m_busy_deadline_ns = 0;   // 0 means "already fired"

	bool busy_timer_enabled() const { return m_busy_deadline_ns == NEVER || m_busy_deadline_ns > m_time_ns; }
	u64 busy_timer_remaining() const { return (m_busy_deadline_ns == NEVER) ? NEVER : (m_busy_deadline_ns - m_time_ns); }
	void update_blink_phase();

	const u8 *m_charset;                  // 8K character generator ROM

	// screen geometry, standing in for the screen device
	int m_screen_width;
	int m_screen_height;
	rectangle m_visarea;

	// internal state
	u8 m_bf = 0;                          // busy flag
	char_mode_t m_char_mode = char_mode_t::MODE24x40; // 40 or 80 chars for line
	u8 m_acc_char[0x2000] = { };          // accented chars
	u8 m_registers[8] = { };              // registers R0-R7
	u8 m_state = 0;                       // status register
	u8 m_tgs = 0, m_mat = 0, m_pat = 0, m_dor = 0, m_ror = 0; // indirect registers
	u8 m_border[80] = { };                // border color
	u16 m_block = 0;                      // current memory block
	u16 m_ram_base[4] = { };              // index of ram charset
	u8 m_blink_phase = 0;                 // flash and cursor blink phase
	u8 m_last_dial[40] = { };             // last chars dial (to determine the zoom position)
	u8 m_latchc0 = 0;                     // background color latch
	u8 m_latchm = 0;                      // hidden attribute latch
	u8 m_latchi = 0;                      // insert attribute latch
	u8 m_latchu = 0;                      // underline attribute latch

	bitmap_rgb32 m_screen_out;

	u8 m_text[TEXT_ROWS][TEXT_STRIDE][4] = { };  // see text_cells()
};

#endif // MINITEL_EF9345_H
