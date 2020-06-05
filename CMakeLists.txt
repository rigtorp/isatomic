cmake_minimum_required(VERSION 3.2)    
project(isatomic CXX)

find_package(Threads REQUIRED)

add_executable(isatomic src/isatomic.cpp)
target_compile_options(isatomic PRIVATE -mavx -mavx2 -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(isatomic Threads::Threads)
target_compile_features(isatomic PRIVATE cxx_std_17)

install(TARGETS isatomic DESTINATION bin/)