# Name:        CMakeLists.txt
# Description: Makaimura Recomp's build: for now the reference tools (MAME's 6809 core and disassembler)
# Version:     0.1.0
# Created:     19-11-2026
#
#   cmake -S . -B build/msvc                (Windows, Visual Studio 2022)
#   cmake --build build/msvc --config Release
#   cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release && cmake --build build/linux -j
cmake_minimum_required(VERSION 3.20)
# The version: the one place it is set (the menu shows it; tools/make_release.ps1 names the package from it)
project(makaimura VERSION 0.2.2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
	set(MAKAIMURA_BIN_DEFAULT ${CMAKE_SOURCE_DIR}/build/bin)
else()
	string(TOLOWER "${CMAKE_SYSTEM_NAME}" MAKAIMURA_SYSTEM)
	set(MAKAIMURA_BIN_DEFAULT ${CMAKE_SOURCE_DIR}/build/bin-${MAKAIMURA_SYSTEM})
endif()
set(MAKAIMURA_BIN_DIR ${MAKAIMURA_BIN_DEFAULT} CACHE PATH "Where the executables go")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()
foreach(config IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${config} ${MAKAIMURA_BIN_DIR})
endforeach()

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_options(/utf-8)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ---- The reference tools (tools/reference): MAME's code, never part of the game ----
set(MAKAIMURA_MAME ${CMAKE_SOURCE_DIR}/tools/reference/mame)

# MAME's 6809 disassembler as an oracle for tools/m6809/disassembler.py (tools/m6809/compare_with_mame.py)
add_executable(oracle_m6809 tools/reference/oracle_m6809.cpp ${MAKAIMURA_MAME}/m6809/6x09dasm.cpp)
target_include_directories(oracle_m6809 PRIVATE tools/reference/stub ${MAKAIMURA_MAME}/m6809)
target_compile_features(oracle_m6809 PRIVATE cxx_std_20)

# ---- The board (runtime/): the game's own code ----
add_library(gng_runtime STATIC runtime/src/board.c runtime/src/video.c runtime/src/inflate.c runtime/src/rom_set.c)
target_include_directories(gng_runtime PUBLIC runtime/include)

# ---- The reference: the original program on MAME's MC6809 core (m6809make.py writes the core's states from
# m6809.lst at build time, as MAME's build does) ----
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MAKAIMURA_M6809 ${MAKAIMURA_MAME}/m6809)
set(MAKAIMURA_GENERATED ${CMAKE_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx
	COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/reference/stdout_to.py
		${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx ${MAKAIMURA_M6809}/m6809make.py ${MAKAIMURA_M6809}/m6809.lst
	DEPENDS ${MAKAIMURA_M6809}/m6809make.py ${MAKAIMURA_M6809}/m6809.lst ${MAKAIMURA_M6809}/base6x09.lst
	VERBATIM)
# The reference sound board: MAME 0.289's Z80 (its steps written from z80.lst at build time, as MAME does) and
# ymfm's YM2203, over arcade-decomp's stand-ins for MAME's framework (tools/core/system32/reference/sound)
set(MAKAIMURA_Z80 ${MAKAIMURA_MAME}/z80)
set(MAKAIMURA_YMFM ${MAKAIMURA_MAME}/ymfm)
add_custom_command(OUTPUT ${MAKAIMURA_GENERATED}/cpu/z80/z80.hxx
	COMMAND ${CMAKE_COMMAND} -E make_directory ${MAKAIMURA_GENERATED}/cpu/z80
	COMMAND Python3::Interpreter ${MAKAIMURA_Z80}/z80make.py z80 ${MAKAIMURA_Z80}/z80.lst
		${MAKAIMURA_GENERATED}/cpu/z80/z80.hxx
	DEPENDS ${MAKAIMURA_Z80}/z80make.py ${MAKAIMURA_Z80}/z80.lst
	VERBATIM)
add_library(gsound STATIC tools/reference/sound/gsound.cpp ${MAKAIMURA_GENERATED}/cpu/z80/z80.hxx
	${MAKAIMURA_YMFM}/ymfm_opn.cpp ${MAKAIMURA_YMFM}/ymfm_ssg.cpp ${MAKAIMURA_YMFM}/ymfm_adpcm.cpp)
target_include_directories(gsound PUBLIC tools/reference/sound
	PRIVATE ${CMAKE_SOURCE_DIR}/tools/core/system32/reference/sound ${MAKAIMURA_Z80} ${MAKAIMURA_YMFM}
	${MAKAIMURA_GENERATED})
target_compile_features(gsound PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(gsound PRIVATE /wd4244 /wd4146 /wd4102 /wd4267 /wd4305 /bigobj)
endif()

add_executable(gref tools/reference/gref.cpp ${MAKAIMURA_M6809}/m6809.cpp ${MAKAIMURA_M6809}/6x09dasm.cpp
	${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx)
target_include_directories(gref PRIVATE tools/reference/stub ${MAKAIMURA_M6809} ${MAKAIMURA_GENERATED})
target_compile_features(gref PRIVATE cxx_std_20)
target_link_libraries(gref PRIVATE gng_runtime gsound)
if(MSVC)
	target_compile_options(gref PRIVATE /wd4244 /wd4146 /wd4102)
endif()

# The lift's oracle: MAME's MC6809 core running one instruction (or the IRQ's entry) from given states
# (tools/m6809/compare_lift_with_mame.py)
add_executable(oracle_m6809_step tools/reference/oracle_m6809_step.cpp ${MAKAIMURA_M6809}/m6809.cpp
	${MAKAIMURA_M6809}/6x09dasm.cpp ${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx)
target_include_directories(oracle_m6809_step PRIVATE tools/reference/stub ${MAKAIMURA_M6809} ${MAKAIMURA_GENERATED})
target_compile_features(oracle_m6809_step PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(oracle_m6809_step PRIVATE /wd4244 /wd4146 /wd4102)
endif()

# ---- The translation (milestone M4): build/generated/program, written by tools/m6809/translate.py from the
# user's ROM (never committed), on the runtime's MC6809 (runtime/src/m6809.c); gng_headless runs it ----
file(GLOB MAKAIMURA_PROGRAM CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/build/generated/program/*.c)
file(GLOB MAKAIMURA_SOUND_PROGRAM CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/build/generated/sound/*.c)
if(MAKAIMURA_PROGRAM AND MAKAIMURA_SOUND_PROGRAM)
	add_library(gng_program STATIC runtime/src/m6809.c runtime/src/z80.c runtime/src/sound.c runtime/src/ym2203.c
		runtime/src/state.c runtime/src/fair_knight.c
		${MAKAIMURA_PROGRAM}
		${MAKAIMURA_SOUND_PROGRAM})
	target_include_directories(gng_program PUBLIC runtime/include ${CMAKE_SOURCE_DIR}/build/generated/program
		${CMAKE_SOURCE_DIR}/build/generated/sound)
	target_link_libraries(gng_program PUBLIC gng_runtime)
	if(NOT MSVC)
		target_link_libraries(gng_program PUBLIC m)    # (lround in sound.c: libm is separate outside MSVC)
	endif()
	if(MSVC)
		# (the generated C: big switches, labels nothing jumps to)
		target_compile_options(gng_program PRIVATE /wd4102 /bigobj)
	endif()
	add_executable(gng_headless ports/headless/main.c)
	target_link_libraries(gng_headless PRIVATE gng_program)

	# The readable C (milestone M6): src/, written by tools/m6809/decompile.py from the user's ROM (never
	# committed), on runtime/src/m6809_program.c; gng_headless_c is gng_headless running it, for the checks
	set(MAKAIMURA_DECOMPILED_DIR ${CMAKE_SOURCE_DIR}/src CACHE PATH "Where the readable C is (tools/m6809/decompile.py --out)")
	file(GLOB MAKAIMURA_DECOMPILED CONFIGURE_DEPENDS ${MAKAIMURA_DECOMPILED_DIR}/*.c)
	if(MAKAIMURA_DECOMPILED)
		add_library(gng_decompiled STATIC runtime/src/m6809_program.c runtime/src/z80.c runtime/src/sound.c
			runtime/src/ym2203.c runtime/src/state.c runtime/src/fair_knight.c
			${MAKAIMURA_DECOMPILED}
			${MAKAIMURA_SOUND_PROGRAM})
		target_include_directories(gng_decompiled PUBLIC runtime/include ${MAKAIMURA_DECOMPILED_DIR}
			${CMAKE_SOURCE_DIR}/build/generated/sound)
		target_link_libraries(gng_decompiled PUBLIC gng_runtime)
		if(MSVC)
			target_compile_options(gng_decompiled PRIVATE /wd4102 /bigobj)
		endif()
		add_executable(gng_headless_c ports/headless/main.c)
		target_link_libraries(gng_headless_c PRIVATE gng_decompiled)
	endif()

	# The game in a window (ports/sdl2): SDL2 from the system, or on Windows its development package
	# (SDL2-devel-2.x.x-VC.zip) unpacked in build/downloads/sdl2
	file(GLOB MAKAIMURA_SDL2_HINT ${CMAKE_SOURCE_DIR}/build/downloads/sdl2/SDL2-*/cmake)
	find_package(SDL2 CONFIG QUIET HINTS ${MAKAIMURA_SDL2_HINT})
	if(SDL2_FOUND)
		# makaimura, the game: the window on the readable C (M6, src/), checked 1:1 against the reference as the
		# translation is; makaimura_translated, the same window on the translation, kept to compare them. Without
		# src/ (tools/m6809/decompile.py not run), makaimura is the translation's
		# Fair Knight (docs/fair-knight/): its hooks are in the translation only (tools/m6809/translate.py), so
		# with the option on makaimura is the translation's window, Fair Knight's changes in it
		option(MAKAIMURA_FAIR_KNIGHT "makaimura on the translation, with Fair Knight's changes" ON)
		set(MAKAIMURA_WINDOWS makaimura)
		if(MAKAIMURA_FAIR_KNIGHT)
			message(STATUS "Fair Knight: makaimura runs the translation (its hooks)")
		elseif(TARGET gng_decompiled)
			list(APPEND MAKAIMURA_WINDOWS makaimura_translated)
		else()
			message(STATUS "No readable C (python tools/m6809/decompile.py): makaimura runs the translation")
		endif()
		foreach(window IN LISTS MAKAIMURA_WINDOWS)
			add_executable(${window} ports/sdl2/main.c ports/sdl2/settings.c ports/sdl2/audio_out.c
				ports/sdl2/resampler.c ports/sdl2/menu.c ports/sdl2/recorder.c ports/sdl2/hud.c ports/sdl2/hiscore.c
				ports/sdl2/replays.c ports/sdl2/rom_drop.c ports/sdl2/updater.c ports/sdl2/crt.c)
			if(window STREQUAL "makaimura" AND TARGET gng_decompiled AND NOT MAKAIMURA_FAIR_KNIGHT)
				target_link_libraries(${window} PRIVATE gng_decompiled SDL2::SDL2)
			else()
				target_link_libraries(${window} PRIVATE gng_program SDL2::SDL2)
			endif()
			target_compile_definitions(${window} PRIVATE MAKAIMURA_VERSION="${PROJECT_VERSION}")
			if(WIN32)
				target_link_libraries(${window} PRIVATE winhttp)    # (updater.c: the newest release looked up and downloaded)
			endif()
			if(TARGET SDL2::SDL2main)
				target_link_libraries(${window} PRIVATE SDL2::SDL2main)
			endif()
			if(WIN32)
				set_target_properties(${window} PROPERTIES WIN32_EXECUTABLE TRUE)
				add_custom_command(TARGET ${window} POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${window}>)
			endif()
		endforeach()
	else()
		message(STATUS "No SDL2: the game's window (makaimura) is not built")
	endif()
else()
	message(STATUS "No translated program (python tools/m6809/translate.py, tools/z80cpu/translate.py): gng_headless is not built")
endif()

# The sound lift's oracle: MAME 0.289's Z80 running one instruction (or the IRQ's entry) from given states
# (tools/z80cpu/compare_lift_with_mame.py)
add_executable(oracle_z80_step tools/reference/sound/oracle_z80_step.cpp ${MAKAIMURA_GENERATED}/cpu/z80/z80.hxx)
target_include_directories(oracle_z80_step PRIVATE ${CMAKE_SOURCE_DIR}/tools/core/system32/reference/sound
	${MAKAIMURA_Z80} ${MAKAIMURA_YMFM} ${MAKAIMURA_GENERATED})
target_compile_features(oracle_z80_step PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(oracle_z80_step PRIVATE /wd4244 /wd4146 /wd4102 /wd4267 /wd4305 /bigobj)
endif()

# The game's YM2203 (runtime/src/ym2203.c) against ymfm's, sample for sample
add_executable(ymcheck tools/reference/sound/ymcheck.cpp runtime/src/ym2203.c
	${MAKAIMURA_YMFM}/ymfm_opn.cpp ${MAKAIMURA_YMFM}/ymfm_ssg.cpp ${MAKAIMURA_YMFM}/ymfm_adpcm.cpp)
target_include_directories(ymcheck PRIVATE runtime/include ${MAKAIMURA_YMFM})
target_compile_features(ymcheck PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(ymcheck PRIVATE /wd4244 /wd4267 /wd4305)
endif()

# The window's resampler (ports/sdl2/resampler.c) on a WAV of the board's mix: tools/check_resampler.py
add_executable(resample_check tests/resample_check.c ports/sdl2/resampler.c)
target_include_directories(resample_check PRIVATE ports/sdl2)
