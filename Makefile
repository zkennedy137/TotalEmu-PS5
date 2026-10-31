# PS Launcher - PS5 build, on the ps5-native-app-boilerplate tooling
# (tools/, tooling/, runtime/ as vendored by SwanStationPS5; GPL-3.0-or-later).
SHELL := /bin/bash
.DEFAULT_GOAL := all
-include .env

# Desktop-only sources are excluded by #if; everything in src/ is compiled.
export APP_VULKAN := 1
export APP_DEFINITIONS ?=
export APP_INCLUDE_PATHS ?= third_party third_party/psswanstation/src src/tls/mbedtls/include third_party/psswanstation/dep/stb/include third_party/psswanstation/dep/libretro-common/include .deps/native/radv-release/include
export APP_STATIC_ARCHIVES ?= build/psswanstation-ps5/libpsswanstation.a lib/libcxx.a lib/libcxxabi.a lib/libunwind.a lib/libc_sdk.a
export APP_RUNTIME_MODULES ?=
# malloc and friends go to the SDK's title heap in direct memory (ps5platform/heap.h):
# the console's libc heap is too small for the core and the canvas (the frame-124 crash).
export APP_WRAP_SYMBOLS ?= fclose fflush malloc calloc realloc free posix_memalign aligned_alloc memalign malloc_usable_size reallocf reallocarray getline getdelim
export APP_SOURCE_DIR ?= src
export APP_PARAM ?= sce_sys/param.json
export APP_SCE_SYS ?= sce_sys
export APP_ASSETS ?= assets
export APP_ROOT_FILES ?=
export APP_LAPY_HELPER ?= 0
export PACBREW_PACKAGES ?= sdl2
export PACBREW_INCLUDE_PATHS ?=
export PACBREW_STATIC_ARCHIVES ?=
export PS5_HOST ?=
export FTP_PORT ?= 2121
export DEPLOY_FORMAT ?= folder
export PS5_FTP_USER ?= anonymous
export PS5_FTP_PASSWORD ?= anonymous
export DEPLOY_DRY_RUN ?= 0
export TITLE_ID ?=
export APP_NAME ?=
export APP_CATEGORY ?= game
export CONTENT_SUFFIX ?=
export BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 2)
export USE_CCACHE ?= 0

RUNTIME := runtime/libc.prx

.PHONY: all app helper core radv check-imports deps desktop deploy clean distclean

# std::string needs the SDK's libc++ archive; the build script's paths can't contain '+'.
lib/libcxx.a lib/libcxxabi.a lib/libunwind.a lib/libc_sdk.a: | deps-fetch radv
	@mkdir -p lib
	@cp ".deps/native/ps5-payload-sdk/target/lib/libc++.a"    lib/libcxx.a
	@cp ".deps/native/ps5-payload-sdk/target/lib/libc++abi.a" lib/libcxxabi.a
	@cp ".deps/native/ps5-payload-sdk/target/lib/libc.a"      lib/libc_sdk.a
	@# RADV's bundled C11-threads impl defines thrd_*/mtx_*/cnd_* too; drop libc's copies.
	@llvm-ar-18 d lib/libc_sdk.a thrd.o mtx.o cnd.o tss.o call_once.o 2>/dev/null || 	 llvm-ar d lib/libc_sdk.a thrd.o mtx.o cnd.o tss.o call_once.o 2>/dev/null || true
	@cp ".deps/native/ps5-payload-sdk/target/lib/libunwind.a"      lib/libunwind.a

.PHONY: deps-fetch
deps-fetch:
	@bash tools/setup-native-dependencies.sh >/dev/null

all: app helper

# Mihawk-99's RADV Vulkan driver + the SDK platform layer (release assets SwanStationPS5 uses).
radv:
	@bash tools/fetch-radv.sh

# The PS1 core: PSSwanStation build 24 by Press5elect (SwanStation with its PS5 port,
# third_party/psswanstation) -> build/psswanstation-ps5/libpsswanstation.a, libretro API
# renamed swanstation_retro_*. Not linked into the app yet (next: the libretro host).
core: radv
	@bash tools/build-psswanstation.sh

# The helper payload (payload/helper.c): dist/pslauncher-helper.elf
helper:
	@bash tools/build-helper.sh

app: $(RUNTIME) lib/libcxx.a lib/libc_sdk.a build/psswanstation-ps5/libpsswanstation.a assets/sandbox-elevator.elf
	@PSL_LINKER_SCRIPT=ps5-pie-unwind.ld bash tools/build.sh Folder
	@python3 tools/check-imports.py dist/*/eboot.bin

# The sandbox elevator, sent to the ELF loader for the USB disc drive (src/disc).
assets/sandbox-elevator.elf: third_party/psswanstation/ps5/elevator/payload/main.cpp third_party/psswanstation/ps5/elevator/protocol.hpp
	@bash tools/build-elevator.sh

build/psswanstation-ps5/libpsswanstation.a:
	@bash tools/build-psswanstation.sh

# Fails if eboot.bin imports a module that stops the title from launching.
check-imports:
	@python3 tools/check-imports.py dist/*/eboot.bin

$(RUNTIME):
	@bash tools/rebuild-libc.sh

deps:
	@bash tools/setup-native-dependencies.sh
	@bash tools/setup-pacbrew-dependencies.sh --environment

# The same sources as a Linux window, for testing without the console.
desktop:
	@mkdir -p build/desktop
	g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-function -fno-exceptions -fno-rtti \
		-Isrc -Ithird_party -Ithird_party/psswanstation/dep/libretro-common/include \
		$$(sdl2-config --cflags) src/*.cpp -o build/desktop/pslauncher \
		$$(sdl2-config --libs)
	gcc -std=gnu11 -O2 -Wall -Wextra payload/helper.c -o build/desktop/pslauncher-helper

deploy: app
	@bash tools/deploy.sh

clean:
	@rm -rf build dist

distclean: clean
	@rm -rf .deps lib runtime/libc.prx
