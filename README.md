# isatomic

*isatomic* is a tool to test if AVX vector loads and stores are atomic.

See https://rigtorp.se/isatomic/ for an article investigating the behavior of
the AVX loads and stores on different microarchitectures.

Build with CMake 3.20 or newer and GCC or Clang on x86 Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Set `-DISATOMIC_WARNINGS_AS_ERRORS=ON` when configuring to treat compiler
warnings as errors. The install directory defaults to `bin` and can be
changed with `-DCMAKE_INSTALL_BINDIR=...`.
