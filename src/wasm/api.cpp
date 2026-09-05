// license:BSD-3-Clause
//
// The C entry points the web front end calls. Kept deliberately flat -- plain
// exported functions and a couple of static buffers -- so the module needs no
// embind, no filesystem and no malloc plumbing on the JavaScript side.

#include "minitel.h"

#include "charset_rom.h"

#include <cstddef>
#include <new>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT extern "C"
#endif

namespace {

// Placement-new'd on first init so the module has no static constructors to
// run and no dependency on the C++ runtime's exit handling.
alignas(minitel_machine) unsigned char g_storage[sizeof(minitel_machine)];
minitel_machine *g_machine = nullptr;

// The ROM the page hands us. A Minitel program ROM image is at most 64K.
u8 g_rom_buffer[0x10000];

// Cropped, tightly packed RGBA for the canvas.
u32 g_rgba[minitel_machine::SCREEN_WIDTH * minitel_machine::SCREEN_HEIGHT];

// Where mt_audio_read() leaves what it drained. Big enough for the several
// frames' worth a page can ask for after a stall -- 8192 samples is 170 ms at
// 48 kHz -- so no realistic tick has to call twice.
float g_audio[8192];

} // anonymous namespace


EXPORT void mt_init()
{
	if (!g_machine)
		g_machine = new (g_storage) minitel_machine(charset_rom);
}

EXPORT void mt_reset()
{
	g_machine->reset();
}

// The video rate in Hz: 50 is the real machine, 60 is what MAME's driver
// declares. Out-of-range values leave the rate alone, so the page should pace
// itself off mt_refresh_hz() rather than off what it asked for.
EXPORT void mt_set_refresh_hz(int hz)
{
	g_machine->set_refresh_hz(hz);
}

EXPORT int mt_refresh_hz()
{
	return g_machine->refresh_hz();
}

// The page writes the image into mt_rom_buffer() and then calls this.
EXPORT u8 *mt_rom_buffer()
{
	return g_rom_buffer;
}

EXPORT int mt_rom_buffer_size()
{
	return int(sizeof(g_rom_buffer));
}

EXPORT void mt_load_rom(int size)
{
	if (size <= 0 || size > int(sizeof(g_rom_buffer)))
		return;

	g_machine->load_cart(g_rom_buffer, std::size_t(size));
	g_machine->reset();
}

EXPORT void mt_run_frame()
{
	g_machine->run_frame();
}

EXPORT int mt_width()
{
	return g_machine->fb_width();
}

EXPORT int mt_height()
{
	return g_machine->fb_height();
}

// Crop the video chip's frame buffer to its visible area and swizzle ARGB into
// the byte order an ImageData expects.
EXPORT const u32 *mt_rgba()
{
	int const w = g_machine->fb_width();
	int const h = g_machine->fb_height();
	int const stride = g_machine->fb_stride();
	const u32 *src = g_machine->framebuffer();

	u32 *dst = g_rgba;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			u32 const p = src[x];
			*dst++ = 0xff000000u | ((p & 0x0000ffu) << 16) | (p & 0x00ff00u) | ((p >> 16) & 0xffu);
		}
		src += stride;
	}

	return g_rgba;
}

// 0 = the grey levels the Minitel 2's monochrome tube shows, 1 = the Videotex
// colors those greys stand for.
EXPORT void mt_set_color(int color)
{
	g_machine->set_color(color != 0);
}

EXPORT void mt_set_key(int row, int bit, int pressed)
{
	g_machine->set_key(row, bit, pressed != 0);
}

EXPORT void mt_release_all_keys()
{
	g_machine->release_all_keys();
}

// SOUND
//
// Mono samples, nominally in [-1, +1], at mt_audio_rate(). The machine
// produces them continuously as it runs, silence included, so a drained frame
// is a full frame's worth of samples that happen to be zero rather than
// nothing at all -- which is what lets the page keep one audio timeline and
// never have to splice.

EXPORT int mt_audio_rate()
{
	return g_machine->audio_rate();
}

// Set this to the output rate the page is going to play at and nothing gets
// resampled on the way out. Out-of-range values leave the rate alone, so the
// page should read mt_audio_rate() back rather than assume.
EXPORT void mt_set_audio_rate(int hz)
{
	g_machine->set_audio_rate(hz);
}

EXPORT float *mt_audio_buffer()
{
	return g_audio;
}

EXPORT int mt_audio_buffer_size()
{
	return int(sizeof(g_audio) / sizeof(g_audio[0]));
}

// Move up to buffer-size samples into mt_audio_buffer() and say how many.
EXPORT int mt_audio_read()
{
	return int(g_machine->audio_read(g_audio, sizeof(g_audio) / sizeof(g_audio[0])));
}

EXPORT u8 *mt_nvram()
{
	return g_machine->nvram();
}

EXPORT int mt_nvram_size()
{
	return int(minitel_machine::NVRAM_SIZE);
}
