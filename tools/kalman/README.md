# Kalman CLI

A small, cross‑platform command‑line tool to run [`MoistureKalmanFilter`](../../components/utils/utils/scheduling/MoistureKalmanFilter.hpp) over a CSV input file.

## Build

Requirements:

- CMake (3.15+)
- A C++17 compiler
- Ninja (recommended) or any CMake generator

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable will be at `tools/kalman/build/kalman_cli[.exe]`.
