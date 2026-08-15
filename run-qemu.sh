#!/bin/bash

SYSTEM_BIOS="/usr/share/edk2/x64/OVMF.4m.fd"
FALLBACK_BIOS="./firmware/OVMF.fd"

if [[ -f "$SYSTEM_BIOS" ]]; then
    BIOS="$SYSTEM_BIOS"
elif [[ -f "$FALLBACK_BIOS" ]]; then
    BIOS="$FALLBACK_BIOS"
else
    echo "Failed to find OVMF firmware. Install edk2-ovmf or ensure firmware/OVMF.fd exists."
    echo "See for more info: https://github.com/tianocore/edk2"
    exit 1
fi

# the +invtsc flag is what gives us the invariant TSC
qemu-system-x86_64 -accel kvm -cpu host,+invtsc,migratable=no -bios ${BIOS} -serial stdio -m 256M -cdrom hltos.iso
