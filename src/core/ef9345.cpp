// license:GPL-2.0+
// copyright-holders:Daniel Coulom,Sandro Ronco
//
// Thomson TS9347 video controller emulator code, extracted from MAME 0.288
// (src/devices/video/ef9345.cpp).
//
// This code is based on Daniel Coulom's implementation in DCVG5k and DCAlice
// released by Daniel Coulom under GPL license.
// TS9347 variant support added by Jean-Francois DEL NERO.
//
// MAME emulates the EF9345 and the TS9347 in one device; only the TS9347 paths
// are kept here. The rendering code is otherwise unchanged -- the device
// plumbing around it (address spaces, palette and screen devices, emu_timers)
// is replaced by the plain members declared in ef9345.h.

#include "ef9345.h"

#include <algorithm>
#include <cstring>


ts9347_device::ts9347_device(const u8 *charset, int screen_width, int screen_height, rectangle visarea)
	: m_charset(charset)
	, m_screen_width(screen_width)
	, m_screen_height(screen_height)
	, m_visarea(visarea)
{
}

// calculate the internal RAM offset
inline u16 ts9347_device::indexram(u8 r)
{
	u8 x = m_registers[r];
	u8 y = m_registers[r - 1];
	if (y < 8)
		y &= 1;
	return ((x&0x3f) | ((x & 0x40) << 6) | ((x & 0x80) << 4) | ((y & 0x1f) << 6) | ((y & 0x20) << 8));
}

// increment x
inline void ts9347_device::inc_x(u8 r)
{
	u8 i = (m_registers[r] & 0x3f) + 1;
	if (i > 39)
	{
		i -= 40;
		m_state |= 0x40;
	}
	m_registers[r] = (m_registers[r] & 0xc0) | i;
}

// increment y
inline void ts9347_device::inc_y(u8 r)
{
	u8 i = (m_registers[r] & 0x1f) + 1;
	if (i > 31)
		i -= 24;
	m_registers[r] = (m_registers[r] & 0xe0) | i;
}


//-------------------------------------------------
//  start - one-time startup
//-------------------------------------------------

void ts9347_device::start()
{
	m_screen_out.allocate(496, m_screen_height);

	init_accented_chars();
}

//-------------------------------------------------
//  reset
//-------------------------------------------------
void ts9347_device::reset()
{
	m_tgs = m_mat = m_pat = m_dor = m_ror = 0;
	m_state = 0;
	m_bf = 0;
	m_block = 0;
	m_blink_phase = 0;
	m_latchc0 = 0;
	m_latchm = 0;
	m_latchi = 0;
	m_latchu = 0;
	m_char_mode = char_mode_t::MODE24x40;

	memset(m_last_dial, 0, sizeof(m_last_dial));
	memset(m_registers, 0, sizeof(m_registers));
	memset(m_border, 0, sizeof(m_border));
	memset(m_border, 0, sizeof(m_ram_base));

	m_screen_out.fill(0);

	set_video_mode();
}

//-------------------------------------------------
//  timer events
//-------------------------------------------------

// MAME clears the busy flag from a one-shot timer; deriving it from the
// deadline gives the same answer at every point a read can observe it.
void ts9347_device::update_busy_flag()
{
	if (!busy_timer_enabled())
		m_bf = 0;
}

// MAME re-arms this every 500 ms to step the phase 11 -> 10 -> 01 -> 00.
// left bit = flashing characters toggle (0.5 Hz)
// right bit = flashing cursor toggle (1 Hz)
void ts9347_device::update_blink_phase()
{
	m_blink_phase = (3 * (m_time_ns / BLINK_PERIOD_NS)) & 0x3;
}


// set busy flag and timer to clear it
void ts9347_device::set_busy_flag(int period)
{
	m_bf = 1;
	if (period < 0)
		m_busy_deadline_ns = NEVER;
	else
		m_busy_deadline_ns = m_time_ns + u64(period);
}

// draw a char in 40 char line mode
void ts9347_device::draw_char_40(u8 *c, u16 x, u16 y)
{
	const rgb_t *palette = m_pen;
	const int scan_xsize = std::min( m_screen_width - (x * 8), 8);
	const int scan_ysize = std::min( m_screen_height - (y * 10), 10);

	for (int i = 0; i < scan_ysize; i++)
		for (int j = 0; j < scan_xsize; j++)
			m_screen_out.pix(y * 10 + i, x * 8 + j) = palette[c[8 * i + j] & 0x07];
}

// draw a char in 80 char line mode
void ts9347_device::draw_char_80(u8 *c, u16 x, u16 y)
{
	const rgb_t *palette = m_pen;
	const int scan_xsize = std::min( m_screen_width - (x * 6), 6);
	const int scan_ysize = std::min( m_screen_height - (y * 10), 10);

	for (int i = 0; i < scan_ysize; i++)
		for (int j = 0; j < scan_xsize; j++)
			m_screen_out.pix(y * 10 + i, x * 6 + j) = palette[c[6 * i + j] & 0x07];
}


// set then ef9345 mode
void ts9347_device::set_video_mode()
{
	m_char_mode = parse_video_mode();

	u16 new_width = (m_char_mode == char_mode_t::MODE12x80 || m_char_mode == char_mode_t::MODE8x80) ? 492 : 336;

	if (m_screen_width != new_width)
	{
		m_screen_width = new_width;
		m_visarea.max_x = new_width - 1;
	}

	//border color
	memset(m_border, m_mat & 0x07, sizeof(m_border));

	//set the base for the m_videoram charset
	m_ram_base[0] = ((m_dor & 0x07) << 11);
	m_ram_base[1] = m_ram_base[0];
	m_ram_base[2] = ((m_dor & 0x30) << 8);
	m_ram_base[3] = m_ram_base[2] + 0x0800;

	//address of the current memory block
	m_block = 0x0800 * ((((m_ror & 0xf0) >> 4) | ((m_ror & 0x40) >> 5) | ((m_ror & 0x20) >> 3)) & 0x0c);
}

// initialize the ef9345 accented chars
void ts9347_device::init_accented_chars()
{
	for (u16 j = 0; j < 0x10; j++)
	{
		for (u16 i = 0; i < 0x200; i++)
			m_acc_char[(j << 9) + i] = m_charset[0x0600 + i];
	}

	for (u16 j = 0; j < 0x200; j += 0x40)
	{
		for (u16 i = 0; i < 4; i++)
		{
			m_acc_char[0x0200 + j + i +  4] |= 0x1c; //tilde
			m_acc_char[0x0400 + j + i +  4] |= 0x10; //acute
			m_acc_char[0x0400 + j + i +  8] |= 0x08; //acute
			m_acc_char[0x0600 + j + i +  4] |= 0x04; //grave
			m_acc_char[0x0600 + j + i +  8] |= 0x08; //grave

			m_acc_char[0x0a00 + j + i +  4] |= 0x1c; //tilde
			m_acc_char[0x0c00 + j + i +  4] |= 0x10; //acute
			m_acc_char[0x0c00 + j + i +  8] |= 0x08; //acute
			m_acc_char[0x0e00 + j + i +  4] |= 0x04; //grave
			m_acc_char[0x0e00 + j + i +  8] |= 0x08; //grave

			m_acc_char[0x1200 + j + i +  4] |= 0x08; //point
			m_acc_char[0x1400 + j + i +  4] |= 0x14; //trema
			m_acc_char[0x1600 + j + i + 32] |= 0x08; //cedilla
			m_acc_char[0x1600 + j + i + 36] |= 0x04; //cedilla

			m_acc_char[0x1a00 + j + i +  4] |= 0x08; //point
			m_acc_char[0x1c00 + j + i +  4] |= 0x14; //trema
			m_acc_char[0x1e00 + j + i + 32] |= 0x08; //cedilla
			m_acc_char[0x1e00 + j + i + 36] |= 0x04; //cedilla
		}
	}
}

// read a char in charset or in m_videoram
u8 ts9347_device::read_char(u8 index, u16 addr)
{
	if (index < 0x04)
		return m_charset[0x0800*index + addr];
	else if (index < 0x08)
		return m_acc_char[0x0800*(index&3) + addr];
	else if (index < 0x0c)
		return vram_r(m_ram_base[index-8] + addr);
	else
		return vram_r(addr);
}

// calculate the dial position of the char
u8 ts9347_device::get_dial(u8 x, u8 attrib)
{
	if (x > 0 && m_last_dial[x-1] == 1)         //top right
		m_last_dial[x] = 2;
	else if (x > 0 && m_last_dial[x-1] == 5)    //half right
		m_last_dial[x] = 10;
	else if (m_last_dial[x] == 1)               //bottom left
		m_last_dial[x] = 4;
	else if (m_last_dial[x] == 2)               //bottom right
		m_last_dial[x] = 8;
	else if (m_last_dial[x] == 3)               //lower half
		m_last_dial[x] = 12;
	else if (attrib == 1)                       //Left half
		m_last_dial[x] = 5;
	else if (attrib == 2)                       //half high
		m_last_dial[x] = 3;
	else if (attrib == 3)                       //top left
		m_last_dial[x] = 1;
	else                                        //none
		m_last_dial[x] = 0;

	return m_last_dial[x];
}

// zoom the char
void ts9347_device::zoom(u8 *pix, u16 n)
{
	u8 i, j;
	if ((n & 0x0a) == 0) // n = 1, 4, 5 (left side)
	{
		for (i = 0; i < 80; i += 8)
			for (j = 7; j > 0; j--)
				pix[i + j] = pix[i + j / 2];
	}
	if ((n & 0x05) == 0) // n = 2, 8, 10 (right side)
	{
		for (i = 0; i < 80; i += 8)
			for (j =0 ; j < 7; j++)
				pix[i + j] = pix[i + 4 + j / 2];
	}
	if ((n & 0x0c) == 0) // n = 1, 2, 3 (top side)
	{
		for (i = 0; i < 8; i++)
			for (j = 9; j > 0; j--)
				pix[i + 8 * j] = pix[i + 8 * ((j-1) / 2)];
	}
	if ((n & 0x03) == 0) // n = 4, 8, 12 (bottom side)
	{
		for (i = 0; i < 8; i++)
			for (j = 0; j < 9; j++)
				pix[i + 8 * j] = pix[i + 32 + 8 * ((j+1) / 2)];
	}
}


// calculate the address of the char x,y
u16 ts9347_device::indexblock(u16 x, u16 y)
{
	u16 i = x, j = indexrow(y);

	//right side of a double width character
	if ((m_tgs & 0x80) == 0 && x > 0)
	{
		if (m_last_dial[x - 1] == 1) i--;
		if (m_last_dial[x - 1] == 4) i--;
		if (m_last_dial[x - 1] == 5) i--;
	}

	return 0x40 * j + i;
}

// applies the insert, flash, conceal and negative attributes,
// considering whether the cursor is on this character or not.
std::tuple<u8, u8, bool> ts9347_device::makecolors(u8 c0, u8 c1, bool insert, bool flash, bool conceal, bool negative, bool cursor)
{
	u8 tmp, c_compl_mask = 0;
	bool underline = false;

	if (negative)
	{
		tmp = c1;
		c1 = c0;
		c0 = tmp;
	}

	if (cursor)
	{
		switch (m_mat & 0x70)
		{
		case 0x40:                  //00 = fixed complemented
			c_compl_mask = 0x7;
			break;
		case 0x50:                  //01 = fixed underlined
			underline = true;
			break;
		case 0x60:                  //10 = flash complemented
			if (m_blink_phase & 0x1)
				c_compl_mask = 0x7;
			break;
		case 0x70:                  //11 = flash underlined
			if (m_blink_phase & 0x1)
				underline = true;
			break;
		}
	}

	switch (m_pat & 0x30)           //insert mode
	{
	case 0x00: // inlay
		if (insert)
			c0 = 0, c1 = (c1 ^ c_compl_mask) | 0x8;
		else
			c0 = c1 = 0;
		break;
	case 0x10: // boxing
		if (insert)
			c0 = (c0 ^ c_compl_mask) | 0x8, c1 = (c1 ^ c_compl_mask) | 0x8;
		else
			c0 = c1 = 0;
		break;
	case 0x20: // character mark
		if (insert)
			c0 = (c0 ^ c_compl_mask) | 0x8, c1 = (c1 ^ c_compl_mask) | 0x8;
		else
			c0 = c0 ^ c_compl_mask, c1 = c1 ^ c_compl_mask;
		break;
	case 0x30: // active area mark
		c0 = (c0 ^ c_compl_mask) | 0x8, c1 = (c1 ^ c_compl_mask) | 0x8;
		break;
	}

	// Note: flashing characters blink on the opposite phase if negative.
	if ((flash && (m_pat & 0x40) && negative == !!(m_blink_phase & 0x2)) ||
		(conceal && (m_pat & 0x08)))
	{
		c1 = c0; // make foreground same as background
	}

	return std::make_tuple(c0, c1, underline);
}

// draw bichrome character (40 columns)
void ts9347_device::bichrome40(u8 type, u16 address, u8 dial, u16 iblock, u16 x, u16 y, u8 c0, u8 c1, bool insert, bool flash, bool conceal, bool negative, bool underline)
{
	u16 i;
	u8 pix[80];

	// test if the cursor is on this character
	i = (m_registers[6] & 0x1f);
	if (i < 8)
		i &= 1;
	if (dial > 0 && (dial & 0x05) == 0) // dial = 2, 8, 10 (right side)
		iblock++;
	bool cursor = iblock == 0x40 * i + (m_registers[7] & 0x3f);

	bool cursor_underline;
	std::tie(c0, c1, cursor_underline) = makecolors(c0, c1, insert, flash, conceal, negative, cursor);
	if ((type & 7) != 2 && (type & 7) != 3) // no underline cursor if semi-gr.
		underline |= cursor_underline;

	// generate the pixel table
	for (i = 0; i < 40; i+=4)
	{
		u8 ch = read_char(type, address + i);

		for (u8 b=0; b<8; b++)
			pix[i*2 + b] = (ch & (1<<b)) ? c1 : c0;
	}

	//draw the underline
	if (underline)
		memset(&pix[72], c1, 8);

	if (dial > 0)
		zoom(pix, dial);

	//doubles the height of the char
	// Local fix, not in MAME 0.288: the service row is never doubled, and the
	// half each row shows was the wrong way round -- 0x03 zooms the top half,
	// 0x0c the bottom, and an odd row is the one that needs the top.
	if (m_mat & 0x80 && y > 0)
		zoom(pix, !(y & 0x01) ? 0x0c : 0x03);

	draw_char_40(pix, x + 1 , y + 1);
}

// draw bichrome character (80 columns)
void ts9347_device::bichrome80(u8 c, u8 a, u16 x, u16 y, bool cursor)
{
	u8 c0, c1, pix[60];

	// Undocumented difference from the EF9345: on the TS9347 the insert bit
	// is taken directly from A0.
	bool const insert = BIT(a, 0);                      //insert = A0

	c1 = (m_dor >> (BIT(a, 0) ? 4 : 0)) & 7;            //foreground color = DOR
	c0 =  m_mat & 7;                                    //background color = MAT

	// The TS9347 only has the alphanumeric G0 set; the EF9345's dedicated
	// mosaic set is not reachable on this part. On the TS9347, G11 comes
	// right after G0 starting from char 128.
	u8 index = (c & 0x80) ? 3 : 0;

	//A0: D = color set
	//A1: U = underline
	//A2: F = flash
	//A3: N = negative
	//C0-6: character code
	bool underline = BIT(a, 1);
	bool flash = BIT(a, 2);
	bool negative = BIT(a, 3);

	bool cursor_underline;
	std::tie(c0, c1, cursor_underline) = makecolors(c0, c1, insert, flash, false, negative, cursor);
	underline ^= cursor_underline;

	const u16 d = ((c >> 2) & 0x1f) * 0x40 + (c & 0x03);  //char position

	for (u16 i=0, j=0; i < 10; i++)
	{
		u8 ch = read_char(index, d + 4 * i);
		for (u8 b=0; b<6; b++)
			pix[j++] = BIT(ch, b) ? c1 : c0;
	}

	//draw the underline
	if (underline)
		std::fill_n(&pix[54], 6, c1);

	draw_char_80(pix, x + 1, y + 1);
}

void ts9347_device::makechar_16x40(u16 x, u16 y)
{
	// Paired with the fix in bichrome40: row 0 is the service row and is never
	// doubled, so the doubled rows run 1..24 and pair as (1,2), (3,4), ... The
	// odd row of each pair carries the top half. MAME 0.288 pairs them from
	// row 2 instead, which drops the bottom half of the last row and shows the
	// service row as a half-height character.
	const u16 iblock = (m_mat & 0x80 && y > 0) ? indexblock(x, (y + 1) / 2) : indexblock(x, y);
	const u8 a = vram_r(m_block + iblock);
	const u8 b = vram_r(m_block + iblock + 0x0800);

	const u8 dial = get_dial(x, BIT(a, 7) ? 0 : bitswap<2>(a, 4, 5));

	//type and address of the char
	u8 type = ((b & 0x80) >> 4) | ((a & 0x80) >> 6);
	u16 address = ((b >> 2) & 0x1f) * 0x40 + (b & 0x03);

	//reset attributes latch
	if (x == 0)
	{
		m_latchm = m_latchi = m_latchu = m_latchc0 = 0;
	}

	//delimiter
	if ((b & 0xe0) == 0x80)
	{
		type = 0;
		address = ((127) >> 2) * 0x40 + (127 & 0x03); // Force character 127 (negative space) of first type.

		m_latchm = b & 1;
		m_latchi = (b & 2) >> 1;
		m_latchu = (b & 4) >> 2;
	}

	if (a & 0x80)
	{
		m_latchc0 = (a & 0x70) >> 4;
	}

	//char attributes
	const u8 c0 = m_latchc0;                   //background
	const u8 c1 = a & 0x07;                    //foreground
	const u8 i = m_latchi;                     //insert mode
	const u8 f  = BIT(a, 3);                   //flash
	const u8 m = m_latchm;                     //conceal
	const u8 n  = BIT(a, 7) ? 0 : BIT(a, 6);   //negative
	const u8 u = m_latchu;                     //underline

	bichrome40(type, address, dial, iblock, x, y, c0, c1, i, f, m, n, u);
}

// generate 24 bits 40 columns char
void ts9347_device::makechar_24x40(u16 x, u16 y)
{
	// Paired with the fix in bichrome40: row 0 is the service row and is never
	// doubled, so the doubled rows run 1..24 and pair as (1,2), (3,4), ... The
	// odd row of each pair carries the top half. MAME 0.288 pairs them from
	// row 2 instead, which drops the bottom half of the last row and shows the
	// service row as a half-height character.
	const u16 row = (m_mat & 0x80 && y > 0) ? (y + 1) / 2 : y;

	// A double-width character takes its glyph from the left cell of its pair,
	// which is what indexblock steps back to. Its attributes, though, belong to
	// the column actually being drawn: software gives the two halves different
	// colours to start and end a highlight on a half-cell boundary, and reading
	// both from the left cell throws that away. MAME 0.288 reads all three
	// planes through indexblock and loses it.
	const u16 iblock = indexblock(x, row);
	const u16 ablock = 0x40 * indexrow(row) + x;

	const u8 c = vram_r(m_block + iblock);
	const u8 b = vram_r(m_block + iblock + 0x0800);
	const u8 a = vram_r(m_block + ablock + 0x1000);

	if ((b & 0xc0) == 0xc0)
		return;         // quadrichrome, which the TS9347 does not support

	const u8 dial = get_dial(x, bitswap<2>(b, 1, 3));

	//type and address of the char
	const u16 address = ((c >> 2) & 0x1f) * 0x40 + (c & 0x03);
	u8 type = (b & 0xf0) >> 4;
	if (!(type & 0x8))
		type &= 0x3; // drop the i2 bit, which is not part of the type

	//char attributes
	const u8 c0 = a & 0x07;        //background
	const u8 c1 = (a >> 4) & 0x07; //foreground
	const u8 i = BIT(b, 0);        //insert
	const u8 f = BIT(a, 3);        //flash
	const u8 m = BIT(b, 2);        //conceal
	const u8 n = BIT(a, 7);        //negative
	const u8 u = (((type & 0x6) == 0) || ((type & 0xc) == 0x4)) ? BIT(b, 4) : 0; //underline

	bichrome40(type, address, dial, iblock, x, y, c0, c1, i, f, m, n, u);
}

// generate 12 bits 80 columns char
void ts9347_device::makechar_12x80(u16 x, u16 y)
{
	const u16 iblock = indexblock(x, y);
	const bool cursor_odd = BIT(m_registers[7], 7);

	//test if the cursor is on one of the two characters that we are rendering.
	u8 i = (m_registers[6] & 0x1f);
	if (i < 8)
		i &= 1;
	const bool cursor = iblock == (0x40 * i + (m_registers[7] & 0x3f));

	bichrome80(vram_r(m_block + iblock), (vram_r(m_block + iblock + 0x1000) >> 4) & 0x0f, 2 * x, y, cursor && !cursor_odd);
	bichrome80(vram_r(m_block + iblock + 0x0800), vram_r(m_block + iblock + 0x1000) & 0x0f, 2 * x + 1, y, cursor && cursor_odd);
}

void ts9347_device::draw_border(u16 line)
{
	if (m_char_mode == char_mode_t::MODE12x80 || m_char_mode == char_mode_t::MODE8x80)
	{
		for (int i = 0; i < 82; i++)
			draw_char_80(m_border, i, line);
	}
	else
	{
		for (int i = 0; i < 42; i++)
			draw_char_40(m_border, i, line);
	}
}

void ts9347_device::makechar(u16 x, u16 y)
{
	switch (m_char_mode)
	{
		case char_mode_t::MODE24x40:
			makechar_24x40(x, y);
			break;
		case char_mode_t::MODEVAR40:
		case char_mode_t::MODE8x80:
			logerror("Unemulated EF9345 mode: %02x\n", u8(m_char_mode));
			break;
		case char_mode_t::MODE12x80:
			makechar_12x80(x, y);
			break;
		case char_mode_t::MODE16x40:
			makechar_16x40(x, y);
			break;
	}
}

// Execute EF9345 command
void ts9347_device::ef9345_exec(u8 cmd)
{
	// Local fix, not in MAME 0.288: bit 2 is driven by the video counter --
	// set at line 0, cleared at line 250 -- so a command cannot clear it.
	// MAME wipes the whole status register here, which costs a program that
	// frame-syncs on it a whole frame every time: its work ends mid-frame with
	// the flag wrongly low, so the next "wait for high" blocks until the next
	// line 0 instead of falling straight through. The dino game runs at 25 fps
	// instead of 50 because of it.
	m_state &= 0x04;
	if ((m_registers[5] & 0x3f) == 39) m_state |= 0x10; //S4(LXa) set
	if ((m_registers[7] & 0x3f) == 39) m_state |= 0x20; //S5(LXm) set

	u16 a = indexram(7);

	switch(cmd)
	{
		case 0x00:  //KRF: R1,R2,R3->ram
		case 0x01:  //KRF: R1,R2,R3->ram + increment
			set_busy_flag(4000);
			vram_w(a, m_registers[1]);
			vram_w(a + 0x0800, m_registers[2]);
			vram_w(a + 0x1000, m_registers[3]);
			if (cmd&1) inc_x(7);
			break;
		case 0x02:  //KRG: R1,R2->ram
		case 0x03:  //KRG: R1,R2->ram + increment
			set_busy_flag(5500);
			vram_w(a, m_registers[1]);
			vram_w(a + 0x0800, m_registers[2]);
			if (cmd&1) inc_x(7);
			break;
		case 0x05:  //CLF: Clear page 24 bits
		case 0x07:  //CLG: Clear page 16 bits
			set_busy_flag(-1);
			for (int i = 0; i < 32 * 40; i++)
			{
				a = indexram(7);
				vram_w(a, m_registers[1]);
				vram_w(a + 0x0800, m_registers[2]);
				if (cmd == 0x05)
					vram_w(a + 0x1000, m_registers[3]);
				inc_x(7);
				if ((m_registers[7] & 0x3f) == 0)
					inc_y(6);
			}
			break;
		case 0x08:  //KRF: ram->R1,R2,R3
		case 0x09:  //KRF: ram->R1,R2,R3 + increment
			set_busy_flag(7500);
			m_registers[1] = vram_r(a);
			m_registers[2] = vram_r(a + 0x0800);
			m_registers[3] = vram_r(a + 0x1000);
			if (cmd&1) inc_x(7);
			break;
		case 0x0a:  //KRG: ram->R1,R2
		case 0x0b:  //KRG: ram->R1,R2 + increment
			set_busy_flag(7500);
			m_registers[1] = vram_r(a);
			m_registers[2] = vram_r(a + 0x0800);
			if (cmd&1) inc_x(7);
			break;
		case 0x30:  //OCT: R1->RAM, main pointer
		case 0x31:  //OCT: R1->RAM, main pointer + inc
			set_busy_flag(4000);
			vram_w(indexram(7), m_registers[1]);

			if (cmd&1)
			{
				inc_x(7);
				if ((m_registers[7] & 0x3f) == 0)
					inc_y(6);
			}
			break;
		case 0x34:  //OCT: R1->RAM, aux pointer
		case 0x35:  //OCT: R1->RAM, aux pointer + inc
			set_busy_flag(4000);
			vram_w(indexram(5), m_registers[1]);

			if (cmd&1)
				inc_x(5);
			break;
		case 0x38:  //OCT: RAM->R1, main pointer
		case 0x39:  //OCT: RAM->R1, main pointer + inc
			set_busy_flag(4500);
			m_registers[1] = vram_r(indexram(7));

			if (cmd&1)
			{
				inc_x(7);

				if ((m_registers[7] & 0x3f) == 0)
					inc_y(6);
			}
			break;
		case 0x3c:  //OCT: RAM->R1, aux pointer
		case 0x3d:  //OCT: RAM->R1, aux pointer + inc
			set_busy_flag(4500);
			m_registers[1] = vram_r(indexram(5));

			if (cmd&1)
				inc_x(5);
			break;
		case 0x50:  //KRL: 80 u8 - 12 bits write
		case 0x51:  //KRL: 80 u8 - 12 bits write + inc
			set_busy_flag(12500);
			vram_w(a, m_registers[1]);
			switch((a / 0x0800) & 1)
			{
				case 0:
				{
					u8 tmp_data = vram_r(a + 0x1000);
					vram_w(a + 0x1000, (tmp_data & 0x0f) | (m_registers[3] & 0xf0));
					break;
				}
				case 1:
				{
					u8 tmp_data = vram_r(a + 0x0800);
					vram_w(a + 0x0800, (tmp_data & 0xf0) | (m_registers[3] & 0x0f));
					break;
				}
			}
			if (cmd&1)
			{
				if ((m_registers[7] & 0x80) == 0x00) { m_registers[7] |= 0x80; return; }
				m_registers[7] &= ~0x80;
				inc_x(7);
			}
			break;
		case 0x58:  //KRL: 80 u8 - 12 bits read
		case 0x59:  //KRL: 80 u8 - 12 bits read + inc
			set_busy_flag(11500);
			m_registers[1] = vram_r(a);
			switch((a / 0x0800) & 1)
			{
				case 0:
					m_registers[3] = vram_r(a + 0x1000);
					break;
				case 1:
					m_registers[3] = vram_r(a + 0x0800);
					break;
			}
			if (cmd&1)
			{
				if ((m_registers[7] & 0x80) == 0x00)
				{
					m_registers[7] |= 0x80;
					break;
				}
				m_registers[7] &= 0x80;
				inc_x(7);
			}
			break;
		case 0x80:  //IND: R1->ROM (impossible ?)
			break;
		case 0x81:  //IND: R1->TGS
		case 0x82:  //IND: R1->MAT
		case 0x83:  //IND: R1->PAT
		case 0x84:  //IND: R1->DOR
		case 0x87:  //IND: R1->ROR
			set_busy_flag(2000);
			switch(cmd&7)
			{
				case 1:     m_tgs = m_registers[1]; break;
				case 2:     m_mat = m_registers[1]; break;
				case 3:     m_pat = m_registers[1]; break;
				case 4:     m_dor = m_registers[1]; break;
				case 7:     m_ror = m_registers[1]; break;
			}
			set_video_mode();
			m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0x88:  //IND: ROM->R1
		case 0x89:  //IND: TGS->R1
		case 0x8a:  //IND: MAT->R1
		case 0x8b:  //IND: PAT->R1
		case 0x8c:  //IND: DOR->R1
		case 0x8f:  //IND: ROR->R1
			set_busy_flag(3500);
			switch(cmd&7)
			{
				case 0:
				{
					u8 type = ((m_registers[6]&0x20)>>3) | ((m_registers[7]&0x40)>>5) | ((m_registers[7]&0x80)>>7);
					type &= 0x3; // 4-7 are aliases of 0-3 on the TS9347

					u16 addr = ((m_registers[6]&0x1f)<<6) | (m_registers[7]&0x3f);
					m_registers[1] = read_char(type, addr); // read slice from ROM
					break;
				}
				case 1:     m_registers[1] = m_tgs; break;
				case 2:     m_registers[1] = m_mat; break;
				case 3:     m_registers[1] = m_pat; break;
				case 4:     m_registers[1] = m_dor; break;
				case 7:     m_registers[1] = m_ror; break;
			}
			m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0x90:  //NOP: no operation
		case 0x91:  //NOP: no operation
		case 0x95:  //VRM: vertical sync mask reset
		case 0x99:  //VSM: vertical sync mask set
			set_busy_flag(1000);
			break;
		case 0xb0:  //INY: increment Y
			set_busy_flag(2000);
			inc_y(6);
			m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0xd5:  //MVB: move buffer MP->AP stop
		case 0xd6:  //MVB: move buffer MP->AP nostop
		case 0xd9:  //MVB: move buffer AP->MP stop
		case 0xda:  //MVB: move buffer AP->MP nostop
		case 0xe5:  //MVD: move double buffer MP->AP stop
		case 0xe6:  //MVD: move double buffer MP->AP nostop
		case 0xe9:  //MVD: move double buffer AP->MP stop
		case 0xea:  //MVD: move double buffer AP->MP nostop
		case 0xf5:  //MVT: move triple buffer MP->AP stop
		case 0xf6:  //MVT: move triple buffer MP->AP nostop
		case 0xf9:  //MVT: move triple buffer AP->MP stop
		case 0xfa:  //MVT: move triple buffer AP->MP nostop
		{
			u16 i, a1, a2;
			u8 n = (cmd>>4) - 0x0c;
			u8 r1 = (cmd&0x04) ? 7 : 5;
			u8 r2 = (cmd&0x04) ? 5 : 7;
			int busy = 2000;

			for (i = 0; i < 32 * 40; i++)
			{
				a1 = indexram(r1); a2 = indexram(r2);
				vram_w(a2, vram_r(a1));

				if (n > 1) vram_w(a2 + 0x0800, vram_r(a1 + 0x0800));
				if (n > 2) vram_w(a2 + 0x1000, vram_r(a1 + 0x1000));

				inc_x(r1);
				inc_x(r2);
				if ((m_registers[5] & 0x3f) == 0 && (cmd&1))
					break;

				if ((m_registers[7] & 0x3f) == 0)
				{
					if (cmd&1)
						break;
					else
					inc_y(6);
				}

				busy += 4000 * n;
			}
			m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			set_busy_flag(busy);
		}
		break;
		case 0x40:  //KRC: R1 -> ram
		case 0x41:  //KRC: R1 -> ram + inc
		case 0x48:  //KRC: 80 characters - 8 bits
		case 0x49:  //KRC: 80 characters - 8 bits
		default:
			logerror("Unemulated EF9345 cmd: %02x\n", cmd);
	}
}


/**************************************************************
            EF9345 interface
**************************************************************/

void ts9347_device::update_scanline(u16 scanline)
{
	update_blink_phase();

	if (scanline == 250)
	{
		// We are past the end of the screen, clear the VSYNC flag.
		m_state &= 0xfb;
	}

	// If we are interrupting a running command, delay its completion.
	if (busy_timer_enabled() && m_busy_deadline_ns != NEVER)
		m_busy_deadline_ns += 104000;

	// Draw the margin at the left and right sides of the row we are about to update.
	// Note: the row we are about to update is (scanline / 10) + 1.
	if (m_char_mode == char_mode_t::MODE12x80 || m_char_mode == char_mode_t::MODE8x80)
	{
		draw_char_80(m_border, 0, (scanline / 10) + 1);
		draw_char_80(m_border, 81, (scanline / 10) + 1);
	}
	else
	{
		draw_char_40(m_border, 0, (scanline / 10) + 1);
		draw_char_40(m_border, 41, (scanline / 10) + 1);
	}

	if (scanline == 0)
	{
		// Set the VSYNC flag.
		m_state |= 0x04;

		// Before starting with the first row of text, also draw a blank row as top margin.
		draw_border(0);

		// Update the first row of text.
		if (m_pat & 1)
		{
			for (u16 i = 0; i < 40; i++)
				makechar(i, (scanline / 10));
		}
		else
		{
			for (u16 i = 0; i < 42; i++)
				draw_char_40(m_border, i, 1);
		}
	}
	else if (scanline < 120)
	{
		// Update the current row.
		if (m_pat & 2)
		{
			for (u16 i = 0; i < 40; i++)
				makechar(i, (scanline / 10));
		}
		else
		{
			draw_border(scanline / 10);
		}
	}
	else if (scanline < 250)
	{
		// Update the current row.
		for (u16 i = 0; i < 40; i++)
			makechar(i, (scanline / 10));
	}
}

u8 ts9347_device::data_r(offs_t offset)
{
	if (offset & 7)
		return m_registers[offset & 7];

	update_busy_flag();

	const u8 result = m_bf ? (m_state | 0x80) : (m_state & 0x7f);
	m_state = result;

	return result;
}

void ts9347_device::data_w(offs_t offset, u8 data)
{
	m_registers[offset & 7] = data;

	if (offset & 8)
		ef9345_exec(m_registers[0] & 0xff);
}

ts9347_device::char_mode_t ts9347_device::parse_video_mode() const
{
	switch (bitswap<2>(m_tgs, 7, 6))
	{
	case 0b00:
		return char_mode_t::MODE24x40;
	case 0b01:
		return char_mode_t::MODE16x40;
	case 0b11:
		return char_mode_t::MODE12x80;
	case 0b10:
		return char_mode_t::MODE8x80;
	default: // unreachable: bitswap<2> only yields the four cases above
		return char_mode_t::MODE24x40;
	}
}

u16 ts9347_device::indexrow(u16 y)
{
	u16 j;

	// On the TS9347 the service row is displayed either at the top or at
	// the bottom, and it is always fetched from Y=0.
	if (m_tgs & 1)
		j = (y == 24) ? 0 : ((m_ror & 0x1f) + y);
	else
		j = (y == 0) ? 0 : ((m_ror & 0x1f) + y - 1);

	return (j > 31) ? (j - 24) : j;
}
