#!/bin/sh
# Headless QEMU + SSH helper for testing Klest in a Debian VM.
#
# Boots the VM with no display, waits for SSH (key auth), installs the locally
# built .deb packages, and runs a headless smoke test of the compositor and its
# components. See INSTALL.md ("Testing in a QEMU VM") for the one-time setup
# (base image, overlay, kernel, SSH key injection).
#
# Usage: scripts/vm-headless.sh {start|stop|install|smoke|ssh|all}
#   start    boot the VM detached and wait until SSH answers
#   install  scp + dpkg -i the two .deb packages built in the parent directory
#   smoke    run compositor (headless/pixman) + panel/background --once over SSH
#   ssh      open an interactive SSH session into the running VM
#   stop     power off the VM (falls back to killing qemu)
#   all      start -> install -> smoke -> stop
#
# Override the overlay image with KLEST_VM_IMAGE.
set -eu

IMAGE="${KLEST_VM_IMAGE:-klest-package-test.qcow2}"
PORT=2222
SSH_HOST="root@localhost"
LOG=/tmp/klest-vm.log
SSH_OPTS="-o StrictHostKeyChecking=accept-new -o BatchMode=yes -p ${PORT}"

ssh_vm() {
    # shellcheck disable=SC2086
    ssh ${SSH_OPTS} "${SSH_HOST}" "$@"
}

cmd_start() {
    if ! [ -f "${IMAGE}" ]; then
        echo "Image not found: ${IMAGE}" >&2
        exit 1
    fi
    # Forget a stale host key from a recreated overlay.
    ssh-keygen -R "[localhost]:${PORT}" >/dev/null 2>&1 || true
    echo "Booting ${IMAGE} (headless)..."
    setsid qemu-system-x86_64 \
        -enable-kvm -m 4096 -smp 2 -display none \
        -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:${PORT}-:22" \
        -drive "file=${IMAGE},if=virtio" \
        >"${LOG}" 2>&1 </dev/null &
    echo "Waiting for SSH on port ${PORT}..."
    i=0
    while [ "${i}" -lt 60 ]; do
        if ssh_vm 'echo ok' >/dev/null 2>&1; then
            echo "VM is up."
            return 0
        fi
        i=$((i + 1))
        sleep 3
    done
    echo "VM did not answer SSH in time. See ${LOG}." >&2
    exit 1
}

cmd_install() {
    version="$(dpkg-parsechangelog -S Version)"
    deb_core="../klest_${version}_amd64.deb"
    deb_settings="../klest-settings_${version}_amd64.deb"
    for f in "${deb_core}" "${deb_settings}"; do
        if ! [ -f "${f}" ]; then
            echo "Missing package: ${f} (run dpkg-buildpackage -us -uc -b)" >&2
            exit 1
        fi
    done
    echo "Copying packages to the VM..."
    scp -o StrictHostKeyChecking=accept-new -P "${PORT}" \
        "${deb_core}" "${deb_settings}" "${SSH_HOST}:/root/"
    echo "Installing packages..."
    ssh_vm "apt-get install -y /root/klest_${version}_amd64.deb /root/klest-settings_${version}_amd64.deb"
}

cmd_smoke() {
    echo "Running headless smoke test..."
    ssh_vm 'sh -eu' <<'REMOTE'
rm -f /tmp/klest.log
WLR_BACKENDS=headless WLR_RENDERER=pixman klest >/tmp/klest.log 2>&1 &
server=$!
# Wait for the compositor to advertise its socket name in the log.
socket=""
i=0
while [ "$i" -lt 30 ]; do
    socket=$(sed -n 's/.*WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' /tmp/klest.log | head -n1)
    [ -n "$socket" ] && break
    i=$((i + 1))
    sleep 1
done
if [ -z "$socket" ]; then
    echo "compositor did not start" >&2
    cat /tmp/klest.log >&2
    kill "$server" 2>/dev/null || true
    exit 1
fi
echo "compositor socket: $socket"
status=0
WAYLAND_DISPLAY="$socket" klest-panel --once && echo PANEL_OK || status=1
WAYLAND_DISPLAY="$socket" klest-background --once && echo BACKGROUND_OK || status=1
kill "$server" 2>/dev/null || true
wait "$server" 2>/dev/null || true
exit "$status"
REMOTE
}

cmd_ssh() {
    # Interactive: drop BatchMode so a password prompt still works if needed.
    exec ssh -o StrictHostKeyChecking=accept-new -p "${PORT}" "${SSH_HOST}"
}

cmd_stop() {
    echo "Powering off the VM..."
    ssh_vm 'systemctl poweroff' >/dev/null 2>&1 || true
    i=0
    while [ "${i}" -lt 15 ]; do
        if ! pgrep -f "qemu-system-x86_64.*${IMAGE}" >/dev/null 2>&1; then
            echo "VM stopped."
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    echo "Forcing shutdown..."
    pkill -f "qemu-system-x86_64.*${IMAGE}" || true
}

case "${1:-}" in
    start) cmd_start ;;
    install) cmd_install ;;
    smoke) cmd_smoke ;;
    ssh) cmd_ssh ;;
    stop) cmd_stop ;;
    all)
        cmd_start
        cmd_install
        cmd_smoke
        cmd_stop
        ;;
    *)
        echo "Usage: $0 {start|stop|install|smoke|ssh|all}" >&2
        exit 2
        ;;
esac
