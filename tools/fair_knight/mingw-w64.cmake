# Fair Knight: Windows builds from Linux with MinGW-w64 (docs/fair-knight/technical.md, "Build").
#   cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=tools/fair_knight/mingw-w64.cmake \
#     -DSDL2_MINGW=/path/to/SDL2-2.30.x/x86_64-w64-mingw32
# SDL2_MINGW: the x86_64 folder of SDL2's SDL2-devel-*-mingw package. libSDL2.dll.a is linked once more at the end
# of the line, as SDL2main needs it after itself and ld reads the libraries once.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(SDL2_MINGW "" CACHE PATH "SDL2's mingw package, its x86_64-w64-mingw32 folder")
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 ${SDL2_MINGW})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(SDL2_DIR ${SDL2_MINGW}/lib/cmake/SDL2 CACHE PATH "")
set(CMAKE_C_STANDARD_LIBRARIES "${SDL2_MINGW}/lib/libSDL2.dll.a" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static")
