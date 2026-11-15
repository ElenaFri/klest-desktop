#!/bin/sh
# Builds the Klest VM cloud-init seed ISO (see INSTALL.md, "Testing in a QEMU VM").
#
# Substitutes your public SSH key into scripts/vm-seed/user-data so the
# provisioned VM accepts key-based SSH for scripts/vm-headless.sh. The key is
# resolved from $KLEST_VM_PUBKEY, else ~/.ssh/id_ed25519.pub, else
# ~/.ssh/id_rsa.pub.
#
# Usage: scripts/vm-seed.sh [output.iso]   (default: klest-vm-seed.iso)
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
seed_dir="${root}/scripts/vm-seed"
output="${1:-${root}/klest-vm-seed.iso}"

pubkey="${KLEST_VM_PUBKEY:-}"
if [ -z "${pubkey}" ]; then
    for candidate in "${HOME}/.ssh/id_ed25519.pub" "${HOME}/.ssh/id_rsa.pub"; do
        if [ -f "${candidate}" ]; then
            pubkey="${candidate}"
            break
        fi
    done
fi
if [ -z "${pubkey}" ] || [ ! -f "${pubkey}" ]; then
    echo "No SSH public key found: set KLEST_VM_PUBKEY or create ~/.ssh/id_ed25519.pub" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
sed "s|__KLEST_VM_PUBKEY__|$(cat "${pubkey}")|" \
    "${seed_dir}/user-data" > "${work}/user-data"
cp "${seed_dir}/meta-data" "${seed_dir}/network-config" "${work}/"

mkisofs -quiet -output "${output}" -volid cidata -joliet -rock \
    "${work}/meta-data" "${work}/user-data" "${work}/network-config"
echo "Seed ISO written to ${output} (key from ${pubkey})"
