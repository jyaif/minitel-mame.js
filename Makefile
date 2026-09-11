# Minitel 2 emulator -- WebAssembly build
#
#   make            build web/minitel.js + web/minitel.wasm
#   make serve      build, then serve web/ on http://localhost:8000
#   make serve-docs refresh docs/, then serve it on http://localhost:8001
#   make dist       build and zip web/ for upload to itch.io
#   make pages      build, then refresh docs/ for GitHub Pages
#   make clean
#
# The two servers use different ports so both can run at once, which is the
# point of having them: web/ is what you are editing, docs/ is what visitors
# will get. Override with PORT= or DOCS_PORT=.

CORE     := $(wildcard src/core/*.cpp)
WASM_SRC := $(CORE) src/wasm/api.cpp

# The character generator ROM is compiled into the module -- nothing can be
# drawn without it. The C array is generated at build time rather than kept in
# the tree, so the ROM lives here in one form only.
CHARSET  := build/charset_rom.h
CHARROM  := src/ts9347.bin

# The C entry points api.cpp exports, plus emscripten's own module init.
EXPORTS := _mt_init,_mt_reset,_mt_rom_buffer,_mt_rom_buffer_size,_mt_load_rom, \
           _mt_run_frame,_mt_width,_mt_height,_mt_rgba,_mt_set_key, \
           _mt_release_all_keys,_mt_nvram,_mt_nvram_size,_mt_set_color, \
           _mt_set_refresh_hz,_mt_refresh_hz, \
           _mt_audio_rate,_mt_set_audio_rate,_mt_audio_buffer, \
           _mt_audio_buffer_size,_mt_audio_read, \
           _mt_serial_set_baud,_mt_serial_baud,_mt_serial_set_format, \
           _mt_serial_databits,_mt_serial_parity,_mt_serial_stopbits, \
           _mt_serial_in_buffer,_mt_serial_in_buffer_size,_mt_serial_write, \
           _mt_serial_out_buffer,_mt_serial_out_buffer_size,_mt_serial_read, \
           _mt_serial_pending,_mt_serial_errors
EXPORTS := $(subst $(subst ,, ),,$(EXPORTS))

CXXFLAGS := -std=c++17 -Isrc/core -Ibuild -Wall

# -Oz plus stripping the C++ runtime we do not use: no exceptions, no RTTI, and
# MINITEL_QUIET removes the diagnostic printf()s, which is what keeps stdio (and
# a few tens of KB of formatting code) out of the module entirely.
#
# The module allocates once, for the video chip's frame buffer, so dlmalloc's
# bins and coalescing are 5 KB spent on a single call: emmalloc does the same
# job here for a fraction of the code. MALLOC=none would be better still but
# does not link -- the runtime itself wants malloc -- and replacing that one
# allocation with a fixed array saves only another 25 bytes, which is not worth
# the change. --converge reruns the binaryen passes until the size stops
# falling, and INCOMING_MODULE_JS_API=[] drops the glue that reads options out
# of a Module object, which the page does not pass.
EMFLAGS := -Oz -DMINITEL_QUIET -fno-exceptions -fno-rtti \
           -flto \
           -s ENVIRONMENT=web \
           -s FILESYSTEM=0 \
           -s MALLOC=emmalloc \
           -s BINARYEN_EXTRA_PASSES=--converge \
           -s INCOMING_MODULE_JS_API=[] \
           -s MODULARIZE=1 \
           -s EXPORT_NAME=createMinitel \
           -s ALLOW_MEMORY_GROWTH=0 \
           -s INITIAL_MEMORY=4194304 \
           -s STACK_SIZE=131072 \
           -s ASSERTIONS=0 \
           -s DISABLE_EXCEPTION_CATCHING=1 \
           -s EXPORTED_FUNCTIONS=$(EXPORTS) \
           -s EXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32 \
           --no-entry \
           --closure 1

# A page cannot be opened over file:// -- the module is fetched, so it needs an
# origin. Both defaults are arbitrary; they differ only so the two can coexist.
PORT      ?= 8000
DOCS_PORT ?= 8001

.PHONY: all serve serve-docs dist pages clean

all: web/minitel.js

web/minitel.js: $(WASM_SRC) $(CHARSET) $(wildcard src/core/*.h) Makefile
	emcc $(CXXFLAGS) $(EMFLAGS) $(WASM_SRC) -o $@
	@ls -l web/minitel.js web/minitel.wasm
	@printf 'gzipped: '; gzip -9c web/minitel.wasm | wc -c

$(CHARSET): $(CHARROM) tools/mkcharset.py
	@mkdir -p build
	python3 tools/mkcharset.py $(CHARROM) $@

serve: all
	@echo "http://localhost:$(PORT)/"
	cd web && python3 -m http.server $(PORT)

# The published page, as it will actually be served: docs/ keeps its own
# config.js and its own ROMs, so this is the only way to see what a visitor
# gets rather than what web/ happens to be set up for. Depends on pages so the
# build under test is never a stale copy.
serve-docs: pages
	@echo "http://localhost:$(DOCS_PORT)/"
	cd docs && python3 -m http.server $(DOCS_PORT)

dist: all
	@rm -f build/minitel-web.zip
	@mkdir -p build
	cd web && zip -9 -r ../build/minitel-web.zip . -x '.*'
	@ls -l build/minitel-web.zip

# GitHub Pages serves docs/ off the default branch. Only the built page goes
# there: docs/config.js and the ROMs beside it are the published site's own,
# and are not what web/ happens to be holding.
PAGES := index.html minitel.js minitel.wasm

pages: all
	cp $(addprefix web/,$(PAGES)) docs/
	@ls -l $(addprefix docs/,$(PAGES))

clean:
	rm -rf build web/minitel.js web/minitel.wasm
