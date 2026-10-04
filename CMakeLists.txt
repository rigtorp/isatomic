cmake_minimum_required(VERSION 3.20)
project(isatomic LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "isatomic requires Linux for CPU affinity support")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
  message(FATAL_ERROR "isatomic requires an x86 processor for AVX assembly")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$" OR
   CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  message(FATAL_ERROR "isatomic requires GCC or Clang with a GNU-style frontend")
endif()

include(GNUInstallDirs)

option(ISATOMIC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(Threads REQUIRED)

add_executable(isatomic src/isatomic.cpp)
target_compile_options(isatomic PRIVATE
  -mavx -mavx2 -mavx512f
  -Wall -Wextra -Wpedantic
)
if(ISATOMIC_WARNINGS_AS_ERRORS)
  target_compile_options(isatomic PRIVATE -Werror)
endif()
target_link_libraries(isatomic PRIVATE Threads::Threads)
target_compile_features(isatomic PRIVATE cxx_std_17)

install(TARGETS isatomic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
