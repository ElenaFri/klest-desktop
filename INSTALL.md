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
    dpkg-dev debhelper \
    meson ninja-build \
    pkg-config \
    libwlroots-0.18-dev \
    libwayland-dev libwayland-bin \
    wayland-protocols \
    libxkbcommon-dev \
    libsystemd-dev dbus-daemon \
    libgtk-4-dev foot
```

- `build-essential` — C compiler (GCC) and core tools (ADR 0001)
- `dpkg-dev`, `debhelper` — build a local Debian package
- `meson`, `ninja-build` — build system (ADR 0006)
- `libwlroots-0.18-dev` — compositor foundation (ADR 0004). The package name is
  **version-specific**; if `0.18` is unavailable, find the right one with
  `apt-cache search wlroots`.
- `libwayland-dev`, `wayland-protocols` — Wayland support (ADR 0002)
- `libwayland-bin` — provides `wayland-scanner`, used to generate protocol headers
- `libxkbcommon-dev` — keyboard keymaps
- `libsystemd-dev` — sd-bus, for the notification daemon (ADR 0011)
- `dbus-daemon` — provides `dbus-run-session` for its integration test
- `libgtk-4-dev` — GUI tools (ADR 0005)
- `foot` — Wayland terminal; the `klest` package installs it as a dependency.

## Build

From the project root:

```sh
meson setup build
ninja -C build
```

For a separate debug build with ASan/UBSan (ADR 0006):

```sh
meson setup build-asan --buildtype=debug -Db_sanitize=address,undefined
ninja -C build-asan
meson test -C build-asan
```

## Build a local Debian package

On Debian, build the package and run its test suite with:

```sh
dpkg-buildpackage -us -uc -b
```

This produces `../klest_0.1.0~alpha.1-1_amd64.deb` and
`../klest-settings_0.1.0~alpha.1-1_amd64.deb`. Install both with:

```sh
sudo apt install ../klest_0.1.0~alpha.1-1_amd64.deb ../klest-settings_0.1.0~alpha.1-1_amd64.deb
```

Choose **Klest** from the display manager's Wayland session list. On first
launch, `klest-session` copies the shipped example configuration to your user
configuration directory without replacing an existing file. `klest-settings`
is separate so the core package does not require GTK.

## Run

Klest is a Wayland compositor. You can run it two ways:

- **Nested**, inside an existing Wayland or X11 session (useful for development):

  ```sh
  ./build/src/klest
  ```

- **From a TTY**, as a standalone session: switch to a free virtual terminal
  (e.g. `Ctrl+Alt+F3`), log in, then run the same command.

Without desktop components, the compositor output is blank except for the cursor.
Klest can launch its components for you: list them under `[startup]` in the
configuration (see [Configuration](#configuration)). The shipped
`examples/config.toml` already autostarts `klest-background`, `klest-panel` and
`klest-notify`, so copying it gives you a full session. When running from a build
tree rather than an install, use paths instead of bare names, e.g.
`autostart = ["./build/src/klest-background", "./build/src/klest-panel"]`.

To start a component by hand instead, use the `WAYLAND_DISPLAY` name printed by
Klest (examples below assume `wayland-0`):

```sh
WAYLAND_DISPLAY=wayland-0 ./build/src/klest-background
```

It fills the output with `[appearance].background_color` from the Klest TOML
configuration. This first version supports a solid colour, not wallpaper images.
Start the clock panel from another terminal or VT with:

```sh
WAYLAND_DISPLAY=wayland-0 ./build/src/klest-panel
```

The panel shows four workspace indicators; the active one is highlighted. Click
an indicator to switch workspace (or use the keybindings below). To the right of
the indicators the panel lists the windows of the active workspace: click a
window to focus it (or restore it if minimized), or click the focused window to
minimize it.

To get a window you need a **Wayland-native** terminal (Klest has no XWayland,
so X11-only apps won't show). The Debian package installs `foot` as the default
terminal; press `Alt`+`Enter` to open it. For source runs, install `foot` from
the requirements above; the built-in defaults and `examples/config.toml` already
use it.

Alternatively, launch any Wayland client from another terminal, pointing it at
Klest's socket:

```sh
WAYLAND_DISPLAY=wayland-0 foot
```

### Application launcher

`klest-launcher` is a keyboard-driven, full-screen overlay listing the
executables found in `$PATH`. Type to filter, `Up`/`Down` (or `Tab`) to select,
`Enter` to launch, `Escape` to cancel. Bind it to `Alt`+`Space` by setting
`launcher = "klest-launcher"` under `[keybindings]` (use the build-tree path,
e.g. `"./build/src/klest-launcher"`, when running uninstalled). You can also run
it directly: `WAYLAND_DISPLAY=wayland-0 ./build/src/klest-launcher`.

### Notifications

`klest-notify` is a notification daemon speaking the standard
`org.freedesktop.Notifications` D-Bus interface (ADR 0011). Start it like any
other component (e.g. add it to `[startup]`, or run it directly), then any
application can post notifications:

```sh
WAYLAND_DISPLAY=wayland-0 ./build/src/klest-notify &
notify-send "Hello" "from Klest"   # needs libnotify-bin
```

Cards stack in the top-right corner and expire on their timeout. It needs a
running session bus; only one notification daemon can own the name at a time.

### Keybindings

| Shortcut            | Action                        |
| ------------------- | ----------------------------- |
| `Alt` + `Enter`     | Launch the configured terminal |
| `Alt` + `Space`     | Launch the configured launcher |
| `Alt` + `Escape`    | Quit Klest                    |
| `Alt` + `F1`        | Cycle focus between windows    |
| `Alt` + `F9`        | Minimize the focused window    |
| `Alt` + `F10`       | Maximize / restore the focused window |
| `Alt` + `1`…`4`      | Switch to workspace 1–4        |
| `Alt` + `Shift` + `1`…`4` | Move the focused window to workspace 1–4 |
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

## Testing in a QEMU VM

A Debian 13 VM provides an isolated full-session test. If `klest-debian.qcow2` already exists, skip image creation.

### Create and start the VM (host)

```sh
sudo apt install qemu-system-x86 qemu-system-gui qemu-utils libguestfs-tools
wget -O klest-debian.qcow2 https://cloud.debian.org/images/cloud/trixie/latest/debian-13-genericcloud-amd64.qcow2
qemu-img resize klest-debian.qcow2 20G
sudo virt-customize -a klest-debian.qcow2 \
    --root-password password:klest \
    --run-command 'printf "[Match]\nName=en*\n\n[Network]\nDHCP=yes\n" > /etc/systemd/network/20-dhcp.network' \
    --run-command 'systemctl enable systemd-networkd'
```

Create a disposable overlay once; keep it beside the base image:

```sh
qemu-img create -f qcow2 -F qcow2 -b klest-debian.qcow2 klest-package-test.qcow2
```

Start or restart the VM with:

```sh
qemu-system-x86_64 \
    -enable-kvm -m 4096 -smp 2 \
    -vga none -device virtio-vga,xres=1920,yres=1080 -display gtk \
    -device virtio-keyboard-pci -device virtio-tablet-pci \
    -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2222-:22 \
    -drive file=klest-package-test.qcow2,if=virtio
```

Virtio-GPU host acceleration (`virtio-vga-gl` + `gl=on`) is fragile and often
produces a black screen; software GL in the VM also produces **graphical
artifacts**. The reliable option is to render with the pixman software backend
(artifact-free, just slower); it is enabled in the next step. `xres`/`yres` pin
the virtual display resolution (the virtio-vga default is only 1280x800); on
real hardware Klest picks the monitor's preferred mode automatically.

Skip overlay creation if `klest-package-test.qcow2` already exists.

Log in on the VM console as `root` / `klest`. If KVM is unavailable, remove `-enable-kvm`.

### Prepare Debian

The cloud kernel lacks the Virtio GPU driver. In the VM, install the standard
kernel and remove the cloud kernel so GRUB cannot select it, and enable the
pixman software renderer (artifact-free graphics):

```sh
apt update
apt install -y linux-image-amd64 openssh-server vim
apt-get purge -y linux-image-cloud-amd64 'linux-image-.*-cloud-amd64'
echo 'WLR_RENDERER=pixman' >> /etc/environment
ssh-keygen -A
update-grub
reboot
```

After reboot, `uname -r` should not contain `cloud`; `modprobe virtio_gpu && ls /dev/dri` should show the GPU device.

### Transfer the source over SSH

In the VM console, enable password login for this local-only QEMU port forward:

```sh
vim /etc/ssh/sshd_config.d/00-klest-vm.conf
```

Add and save:

```text
PermitRootLogin yes
PasswordAuthentication yes
```

Apply the change with `sshd -t && systemctl enable ssh && systemctl restart ssh`. From the host, connect with `ssh -p 2222 root@localhost` (password `klest`). Verify the host-key fingerprint using `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub` in the VM before accepting it. If the VM was recreated, remove the stale key with `ssh-keygen -R '[localhost]:2222'`.

From the project root on the host, copy the source:

```sh
tar --exclude=build --exclude='build-*' --exclude=.git --exclude=.cache --exclude='*.qcow2' -czf /tmp/klest.tar.gz .
scp -P 2222 /tmp/klest.tar.gz root@localhost:/root/
```

### Build and run Klest

In the VM over SSH:

```sh
mkdir -p /root/klest
tar -xzf /root/klest.tar.gz -C /root/klest
apt install -y build-essential meson ninja-build pkg-config \
    libwlroots-0.18-dev libwayland-dev libwayland-bin wayland-protocols \
    libxkbcommon-dev libsystemd-dev dbus-daemon libgtk-4-dev foot
cd /root/klest
meson setup build && ninja -C build && meson test -C build
mkdir -p /root/.config/klest
cp examples/config.toml /root/.config/klest/config.toml
```

On the VM's graphical console (not over SSH), run `cd /root/klest && ./build/src/klest`. Press `Alt`+`Enter` to open `foot`, then `Alt`+`Escape` to quit.

### Test the Debian packages

On the host, from the project root, transfer the locally built packages:

```sh
scp -P 2222 ../klest_0.1.0~alpha.1-1_amd64.deb ../klest-settings_0.1.0~alpha.1-1_amd64.deb root@localhost:/root/
```

In the VM, install them:

```sh
apt install -y dbus-daemon
apt install --reinstall -y /root/klest_0.1.0~alpha.1-1_amd64.deb /root/klest-settings_0.1.0~alpha.1-1_amd64.deb
```

From the VM's graphical console, start the packaged session:

```sh
dbus-run-session -- klest-session
```

On first launch `klest-session` copies the example configuration to
`~/.config/klest/config.toml` (here `/root/.config/klest/config.toml`) without
overwriting an existing file. This is a **persistent** location you can then
edit — by hand or with `klest-settings` — with changes applied at the next
session start. Do not point `XDG_CONFIG_HOME` at `/tmp`: it is wiped on every
reboot, so settings (including the background colour) would silently revert.

The first run should show the background and panel; `Alt`+`Enter` opens `foot`,
and `Alt`+`Space` opens the launcher. Note: in the QEMU GTK window the host
grabs the **left** Alt, so use the **right** Alt to trigger Klest's Alt
shortcuts (launcher, terminal, workspaces…).

### Headless testing over SSH

For automated runs without a graphical console, [`scripts/vm-headless.sh`](scripts/vm-headless.sh)
boots the VM with `-display none`, waits for SSH, installs the locally built
packages, and smoke-tests the compositor and its components.

It needs key-based SSH. Inject your public key into the overlay once (VM off):

```sh
sudo virt-customize -a klest-package-test.qcow2 \
    --ssh-inject root:file:$HOME/.ssh/id_ed25519.pub \
    --run-command 'systemctl enable ssh'
```

Then build the packages (`dpkg-buildpackage -us -uc -b`) and run:

```sh
scripts/vm-headless.sh all   # start -> install -> smoke -> stop
```

Individual steps are also available: `start`, `install`, `smoke`, `ssh`, `stop`.
Override the image with `KLEST_VM_IMAGE`. The smoke test runs the compositor
with the headless backend and Pixman renderer (no GPU needed), then launches
`klest-panel --once` and `klest-background --once`, printing `PANEL_OK` and
`BACKGROUND_OK` on success.

## Tests

```sh
meson test -C build
```
