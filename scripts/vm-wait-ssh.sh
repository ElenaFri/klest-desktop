#!/bin/sh
# Waits until the Klest test VM answers SSH on the forwarded port (see
# scripts/vm-headless.sh and INSTALL.md, "Testing in a QEMU VM").
#
# Usage: scripts/vm-wait-ssh.sh [timeout-seconds]
# Exits 0 once SSH answers, 1 on timeout.
set -u

PORT="${KLEST_VM_PORT:-2222}"
TIMEOUT="${1:-300}"

SSH_OPTS="-o StrictHostKeyChecking=accept-new -o BatchMode=yes -p ${PORT}"

i=0
while [ "${i}" -lt "${TIMEOUT}" ]; do
	if ssh ${SSH_OPTS} root@localhost 'echo SSH_OK' >/dev/null 2>&1; then
		echo "VM is up (SSH on port ${PORT})."
		exit 0
	fi
	i=$((i + 5))
	sleep 5
done

echo "VM did not answer SSH within ${TIMEOUT}s." >&2
exit 1
