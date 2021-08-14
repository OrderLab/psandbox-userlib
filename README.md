# PerfSandbox User-Level Library

![build workflow](https://github.com/OrderLab/psandbox-userlib/actions/workflows/build.yml/badge.svg)

## Dependencies

PSanbox user-level library depends on the `pkg-config` and `glib-2.0` libraries.

Install them with

```bash
sudo apt-get install pkg-config libglib2.0-dev
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j $(nproc)
```

