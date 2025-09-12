# Kalman CLI

A small, cross‑platform command‑line tool to run [`MoistureKalmanFilter`](../../components/utils/utils/scheduling/MoistureKalmanFilter.hpp) over a CSV input file.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable will be at `tools/kalman/build/kalman_cli[.exe]`.
