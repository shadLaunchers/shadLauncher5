# SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.24)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set_directory_properties(PROPERTIES
    EXCLUDE_FROM_ALL ON
    SYSTEM ON
)

#libdeflate

# enable SIMD instructions
if(MSVC)
    add_compile_options(/O2 /arch:AVX2)
else()
    add_compile_options(-O3 -march=native)
endif()

add_subdirectory(libdeflate)

# zstd (dependency of ZArchive)
if (NOT TARGET libzstd_static)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    add_subdirectory(zstd/build/cmake)
endif()

# ZArchive (.zar archive support)
if (NOT TARGET zarchive)
    add_library(zarchive STATIC
        zarchive/src/zarchivereader.cpp
        zarchive/src/zarchivewriter.cpp
        zarchive/src/sha_256.c
    )
    target_include_directories(zarchive PUBLIC zarchive/include)
    target_include_directories(zarchive PRIVATE zstd/lib)
    target_link_libraries(zarchive PRIVATE libzstd_static)
    target_compile_features(zarchive PRIVATE cxx_std_20)
    add_library(ZArchive::zarchive ALIAS zarchive)
endif()