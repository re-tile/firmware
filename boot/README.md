# Boot chain and firmware

## `boot-chain/`

The on-board boot binaries as they sit in the boot partition

- `hv`, `hv_l1boot`, `hv_lhboot` - the TILE-Gx hypervisor and its level-1 / late-handoff
  boot stages.
- `vmlinux_mboot` - the mboot-wrapped kernel image.
- `classifier`, `sromboot.bin`, `pci_preboot.bin`, `mboot.hvc` - supporting boot-time
  blobs.

## `artifacts/`

- `boot/` - a stock MDE 4.3.4 boot set: `vmlinux-3.10.90-MDE-4.3.4` (the kernel),
  its `config-*` and `System.map-*`, the hypervisor binaries again, and four initramfs
  variants (`initramfs`, `-tiny`, `-diskless-iscsi`, `-diskless-nfs`).
- `usr_lib_boot/` - recovery and board bootroms (`recovery.bootrom`, `tilempower.bootrom`,
  `tilextreme*.bootrom`).
- `tile-mkboot.py`, `vmlinux.hvc`, `hvc/` - the boot-image build tool and hvc config.
- `gx72-memtest/` - a standalone Gx72 memory-test bootrom (`memorydebug.rom`) and
  its source archive, useful for exercising DDR on bare Gx72 silicon.
