# Tilera TILE-Gx Firmware and Source

This repository holds the complete TILE-Gx hypervisor source, the MDE source trees,
and the on-board boot chain, and is based on MDE 4.3.4. The companion documentation
set lives in the sibling [`re-tile/docs`](https://github.com/re-tile/docs) repository.

The TILE architecture is extinct, and to my knowledge none of this material is available
anywhere else. It is preserved here for archival purposes.

## Layout

| Path                       | Contents                                                                                   |
| -------------------------- | ------------------------------------------------------------------------------------------ |
| `hypervisor-source/4.3.4/` | The TILE-Gx hypervisor source tree                                                         |
| `mde-source/4.3.4/`        | MDE source trees (`sys/`, `tools/`) and the `.get` patch sets for the toolchain components |
| `boot/boot-chain/`         | The on-board boot binaries                                                                 |
| `boot/artifacts/`          | Holds the stock MDE boot set                                                               |

## Provenance and a note on the boot images

This was recovered from a decommissioned TILEmpower-Gx72 unit, and the boot and
initramfs images are stock MDE recovery images.

## Licensing

This is Tilera/EZchip proprietary documentation, mirrored for archival purposes
from a defunct vendor. No license is granted. Current rights holders who want something
removed can open an issue or reach out to me via email.
