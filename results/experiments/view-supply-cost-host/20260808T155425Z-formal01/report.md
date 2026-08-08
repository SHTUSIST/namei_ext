# Per-view supply cost on the host kernel

namei_ext is not in this table. It needs the patched kernel in KVM, so its column is absent rather than estimated.

## Supply cost and resident cost

| condition | views | supply p50 (us) | supply p99 (us) | slab per view (KB) | next mount-namespace clone p50 (us) |
| --- | ---: | ---: | ---: | ---: | ---: |
| btrfs_snapshot | 1 | 27804.9 | 254198.2 | 0.00 | 808.8 |
| btrfs_snapshot | 10 | 10525.1 | 19623.5 | 0.00 | 807.8 |
| btrfs_snapshot | 100 | 11426.6 | 22965.4 | 0.12 | 804.8 |
| btrfs_snapshot | 1000 | 13372.8 | 26453.6 | 0.22 | 839.3 |
| control_process | 1 | 685.2 | 802.7 | 0.00 | 783.0 |
| control_process | 10 | 508.2 | 721.3 | 0.00 | 856.0 |
| control_process | 100 | 599.7 | 831.5 | 72.48 | 778.4 |
| control_process | 1000 | 1978.1 | 4299.6 | 945.49 | 818.0 |
| mountns_bind | 1 | 815.5 | 839.8 | 0.00 | 798.3 |
| mountns_bind | 10 | 575.0 | 879.9 | 0.80 | 795.4 |
| mountns_bind | 100 | 622.0 | 928.1 | 85.16 | 793.7 |
| mountns_bind | 1000 | 1947.3 | 3853.5 | 956.85 | 905.2 |
| overlayfs_mount | 1 | 666.6 | 715.1 | 56.00 | 795.8 |
| overlayfs_mount | 10 | 357.1 | 588.2 | 208.80 | 781.0 |
| overlayfs_mount | 100 | 338.2 | 535.1 | 246.12 | 905.7 |
| overlayfs_mount | 1000 | 368.4 | 660.7 | 260.13 | 1859.0 |

## Page cache held by N views of the same content

| condition | views | MiB read | MiB of page cache |
| --- | ---: | ---: | ---: |
| bind_same_source | 64 | 1024 | 16.4 |
| btrfs_snapshot | 64 | 1024 | 1101.3 |
| overlay_same_lower | 64 | 1024 | 17.7 |
| bind_same_source | 64 | 1024 | 16.3 |
| btrfs_snapshot | 64 | 1024 | 1106.2 |
| overlay_same_lower | 64 | 1024 | 17.5 |
| bind_same_source | 64 | 1024 | 16.9 |
| btrfs_snapshot | 64 | 1024 | 1106.3 |
| overlay_same_lower | 64 | 1024 | 17.4 |

## Re-pointing a view

- idle, trial 0: p50 1167.8 us, p99 2065.4 us
- in use, trial 0: umount returned 32, `umount: /root/agent_fs/namei_ext/.build/view-supply-cost-host/scratch/switch/view: target is busy.`
- idle, trial 1: p50 1207.5 us, p99 2322.1 us
- in use, trial 1: umount returned 32, `umount: /root/agent_fs/namei_ext/.build/view-supply-cost-host/scratch/switch/view: target is busy.`
- idle, trial 2: p50 1208.0 us, p99 1422.7 us
- in use, trial 2: umount returned 32, `umount: /root/agent_fs/namei_ext/.build/view-supply-cost-host/scratch/switch/view: target is busy.`
