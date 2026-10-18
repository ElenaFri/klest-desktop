# Installing & running Klest

> **Early development.** Klest currently builds a minimal compositor that brings
> up outputs with a background (see [TODO.md](TODO.md)); window management and
> input are not implemented yet. The procedure below targets Debian (ADR 0003).

## Requirements

Klest targets **Debian**. Install the build tools and development libraries:

```sh
sudo apt update
sudo apt install \
    build-essential \
    meson ninja-build \
    pkg-config \
    libwlroots-0.18-dev \
    libwayland-dev \
    wayland-protocols \
    libgtk-4-dev
```

- `build-essential` — C compiler (GCC) and core tools (ADR 0001)
- `meson`, `ninja-build` — build system (ADR 0006)
- `libwlroots-0.18-dev` — compositor foundation (ADR 0004). The package name is
  **version-specific**; if `0.18` is unavailable, find the right one with
  `apt-cache search wlroots`.
- `libwayland-dev`, `wayland-protocols` — Wayland support (ADR 0002)
- `libgtk-4-dev` — GUI tools (ADR 0005)

## Build

From the project root:

```sh
meson setup build
ninja -C build
```

For a debug build with strict warnings and sanitizers (ADR 0006):

```sh
meson setup build --buildtype=debug -Db_sanitize=address,undefined
ninja -C build
```

## Run

Klest is a Wayland compositor. You can run it two ways:

- **Nested**, inside an existing Wayland or X11 session (useful for development):

  ```sh
  ./build/src/klest
  ```

- **From a TTY**, as a standalone session: switch to a free virtual terminal
  (e.g. `Ctrl+Alt+F3`), log in, then run the same command.

## Testing in a VM (QEMU)

A VM is useful for testing a full session in isolation, on a clean Debian, without risking your working machine. For day-to-day development, prefer **nested** mode (above); for CI, prefer the **headless** backend (`WLR_BACKENDS=headless WLR_RENDERER=pixman`).

### Create the Debian image

First create an empty virtual disk, then install Debian into it from the netinst ISO (download it from <https://www.debian.org/distrib/>):

```sh
# 1. Create a 20 GB disk image
qemu-img create -f qcow2 klest-debian.qcow2 20G

# 2. Boot the installer once, with the ISO attached
qemu-system-x86_64 \
    -enable-kvm \
    -m 4096 \
    -smp 2 \
    -drive file=klest-debian.qcow2,if=virtio \
    -cdrom debian-netinst.iso \
    -boot d
```

Follow the Debian installer, then shut the guest down. From now on, boot from the disk (without `-cdrom`/`-boot d`) using the launch command below.

### Launch the VM

Start the Debian guest with a virtio GPU so wlroots has a DRM device to drive:

```sh
qemu-system-x86_64 \
    -enable-kvm \
    -m 4096 \
    -smp 2 \
    -vga virtio \
    -display gtk,gl=on \
    -device virtio-keyboard-pci \
    -device virtio-tablet-pci \
    -drive file=klest-debian.qcow2,if=virtio
```

- `-vga virtio` + `-display gtk,gl=on` — virtio-gpu with host GL, so rendering
  is reasonable; without `gl=on`, Klest falls back to software rendering
  (`WLR_RENDERER=pixman`).
- `-device virtio-tablet-pci` — gives absolute pointer positioning, which makes
  the mouse usable inside the guest window.
- `-enable-kvm` — hardware acceleration (Linux host); drop it if unavailable.

Inside the guest, install the requirements above, then launch Klest from a TTY:

```sh
./build/src/klest
```

If it fails to find a GPU, force software rendering:

```sh
WLR_RENDERER=pixman ./build/src/klest
```

## Configuration

Klest reads its settings from a TOML file (ADR 0007). Default location:

```
~/.config/klest/klest.toml
```

Sensible defaults apply when no file is present.

## Tests

```sh
meson test -C build
```
