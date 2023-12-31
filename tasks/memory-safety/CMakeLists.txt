cmake_minimum_required(VERSION 3.18)
project(llvm-pass-memory-safety)

# Set this to a valid LLVM installation dir
set(LT_LLVM_INSTALL_DIR "" CACHE PATH "Path to LLVM")

# Add the location of LLVMConfig.cmake to CMake search paths (so that
# find_package can locate it)
list(APPEND CMAKE_PREFIX_PATH "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm/")

find_package(LLVM REQUIRED CONFIG HINTS)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# LLVM is normally built without RTTI. Be consistent with that.
if (NOT LLVM_ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og -g3 -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -g3")

add_library(MemorySafety SHARED MemorySafety.cpp)

# Allow undefined symbols in shared objects on Darwin (this is the default
# behaviour on Linux)
target_link_libraries(MemorySafety
        "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")

add_library(MemorySafetyRuntimeC MemorySafetyRuntime.c)

add_library(MemorySafetyRuntimeCPP MemorySafetyRuntime.cpp)
