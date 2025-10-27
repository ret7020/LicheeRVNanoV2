# LicheeRV Nano V2

This is set of examples that use Sophgo SDK for RTSP and NPU.

Checkout SDK submodules:

```bash
git submodule update --init --recursive --remote
```

Export cross-compilers path:

```bash
export COMPILER=<PATH>/gcc/riscv64-linux-musl-x86_64/bin
```

Configure and build project:

```bash
mkdir build && cd build
cmake ..
make
```

Result binary location is: `./build/bin/app`
