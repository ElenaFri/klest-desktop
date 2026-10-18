# Installing & running Klest

> **Early development.** Klest currently builds a usable minimal compositor:
> it brings up outputs, runs xdg-shell client windows, and handles keyboard and
> pointer input, including interactive move/resize (see [TODO.md](TODO.md)).
> The procedure below targets Debian (ADR 0003).

## Requirements

Klest targets **Debian**. Install the build tools and development libraries:

```sh
sudo apt update
sudo apt install \
    build-essential \
    meson ninja-build \
    pkg-config \
    libwlroots-0.18-dev \
    libwayland-dev libwayland-bin \
    wayland-protocols \
    libxkbcommon-dev \
    libgtk-4-dev
```

- `build-essential` — C compiler (GCC) and core tools (ADR 0001)
- `meson`, `ninja-build` — build system (ADR 0006)
- `libwlroots-0.18-dev` — compositor foundation (ADR 0004). The package name is
  **version-specific**; if `0.18` is unavailable, find the right one with
  `apt-cache search wlroots`.
- `libwayland-dev`, `wayland-protocols` — Wayland support (ADR 0002)
- `libwayland-bin` — provides `wayland-scanner`, used to generate protocol headers
- `libxkbcommon-dev` — keyboard keymaps
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

On startup you'll see an empty desktop: a dark background and the cursor. This is
expected — Klest is a minimal compositor with no panel or launcher, so nothing is
open yet.

To get a window you need a **Wayland-native** terminal (Klest has no XWayland, so
X11-only apps won't show). Install one, e.g.:

```sh
sudo apt install foot
```

Then set it up once as your launcher, and press `Alt`+`Return` to open it:

```sh
mkdir -p ~/.config/klest
cp examples/config.toml ~/.config/klest/config.toml
sed -i 's/^terminal = ""/terminal = "foot"/' ~/.config/klest/config.toml
```

Alternatively, launch any Wayland client from another terminal, pointing it at
Klest's socket (printed on startup, usually `wayland-0`):

```sh
WAYLAND_DISPLAY=wayland-0 foot
```

### Keybindings

| Shortcut            | Action                        |
| ------------------- | ----------------------------- |
| `Alt` + `Return`    | Launch the configured terminal |
| `Alt` + `Escape`    | Quit Klest                    |
| `Alt` + `F1`        | Cycle focus between windows    |
| `Alt` + left-drag   | Move the window under the cursor (client-initiated) |

Client windows can also be moved and resized interactively when the application
requests it (e.g. dragging a title bar).

## Configuration

Klest reads `~/.config/klest/config.toml` (override with `$KLEST_CONFIG`). Every
key is optional — see [`examples/config.toml`](examples/config.toml) for the full
schema and defaults.

You can edit it by hand, or use the GTK tool (built when GTK 4 is available):

```sh
./build/src/klest-settings
```

## Testing in a VM (QEMU)

A VM is useful for testing a full session in isolation, on a clean Debian, without risking your working machine. For day-to-day development, prefer **nested** mode (above); for CI, prefer the **headless** backend (`WLR_BACKENDS=headless WLR_RENDERER=pixman`).

### Create the Debian image

Download Debian's prebuilt cloud image, grow it, and set a root password:

```sh
wget -O klest-debian.qcow2 https://cloud.debian.org/images/cloud/trixie/latest/debian-13-genericcloud-amd64.qcow2
qemu-img resize klest-debian.qcow2 20G
sudo virt-customize -a klest-debian.qcow2 \
    --root-password password:klest \
    --run-command 'printf "[Match]\nName=en*\n\n[Network]\nDHCP=yes\n" > /etc/systemd/network/20-dhcp.network' \
    --run-command 'systemctl enable systemd-networkd'
```

Boot it with the launch command below and log in as `root` / `klest`; networking
comes up automatically via DHCP.

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
    -nic user,model=virtio-net-pci \
    -drive file=klest-debian.qcow2,if=virtio
```

- `-vga virtio` + `-display gtk,gl=on` — virtio-gpu with host GL, so rendering
  is reasonable; without `gl=on`, Klest falls back to software rendering
  (`WLR_RENDERER=pixman`).
- `-nic user,model=virtio-net-pci` — NAT networking, so the guest has internet
  (needed for `apt install`) with no host setup.
- `-device virtio-tablet-pci` — gives absolute pointer positioning, which makes
  the mouse usable inside the guest window.
- `-enable-kvm` — hardware acceleration (Linux host); drop it if unavailable.

### Working over SSH (so copy-paste works)

To paste commands into the guest, work over SSH from your host terminal. Add an
SSH port forward to the NIC:

```sh
    -nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22 \
```

Then, inside the guest, install the SSH server:

```sh
apt install -y openssh-server
```

Cloud images often ship without SSH host keys, which makes the service fail to
start. Generate them:

```sh
ssh-keygen -A
```

Allow root login by editing the config (no copy-paste needed yet):

```sh
vim /etc/ssh/sshd_config
```

Find the `PermitRootLogin` line and make it read exactly:

```
PermitRootLogin yes
```

Save and restart the server:

```sh
systemctl restart ssh
```

From the host, connect with your normal terminal (clipboard works as usual):

```sh
ssh -p 2222 root@localhost
```

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
