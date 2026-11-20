#!/bin/sh
# Rebuild the bundled SDL2 / SDL_image / SDL_ttf from their local source trees.
set -eu
deps_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$deps_dir/SDL" -B "$deps_dir/build_sdl" \
        -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TEST=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$deps_dir/build_sdl" --parallel
cmake -S "$deps_dir/SDL_ttf" -B "$deps_dir/build_ttf" \
        -DBUILD_SHARED_LIBS=OFF -DSDL2TTF_VENDORED=OFF -DSDL2TTF_SAMPLES=OFF
cmake --build "$deps_dir/build_ttf" --parallel
cmake -S "$deps_dir/SDL_image" -B "$deps_dir/build_img" \
        -DBUILD_SHARED_LIBS=OFF -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF \
        -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF
cmake --build "$deps_dir/build_img" --parallel
