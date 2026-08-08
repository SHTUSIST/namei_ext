# What a read-only operation pays for a writable layer

Same content, same host kernel. `native` reads the lower filesystem directly. `overlay_dN` reads it through an overlayfs mount with a writable upper directory and N read-only lower directories, `metacopy` and `redirect_dir` enabled.

| condition | depth | stat p50 (ns) | vs native | open p50 (ns) | vs native | readdir p50 (us) | vs native | cold open+read p50 (us) | vs native |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| native | 0 | 2903 | -- | 3555 | -- | 36.9 | -- | 2700.5 | -- |
| overlay_d1 | 1 | 3490 | 1.202 [1.197, 1.207] | 4822 | 1.356 [1.353, 1.360] | 40.8 | 1.106 [1.101, 1.109] | 2119.8 | 0.785 [0.687, 0.909] |
| overlay_d2 | 2 | 3536 | 1.218 [1.213, 1.223] | 4793 | 1.348 [1.344, 1.352] | 52.1 | 1.412 [1.406, 1.418] | 2704.2 | 1.001 [0.848, 1.225] |
| overlay_d5 | 5 | 3466 | 1.194 [1.189, 1.199] | 4798 | 1.350 [1.346, 1.353] | 56.2 | 1.523 [1.511, 1.533] | 2766.9 | 1.025 [0.931, 1.220] |
| overlay_d10 | 10 | 3405 | 1.173 [1.167, 1.178] | 4802 | 1.351 [1.347, 1.355] | 62.9 | 1.706 [1.694, 1.716] | 3396.4 | 1.258 [1.116, 1.468] |
| overlay_d25 | 25 | 3465 | 1.194 [1.187, 1.199] | 4811 | 1.353 [1.349, 1.357] | 87.5 | 2.374 [2.367, 2.380] | 4844.7 | 1.794 [1.585, 2.128] |
| overlay_d50 | 50 | 3626 | 1.249 [1.243, 1.255] | 4799 | 1.350 [1.346, 1.353] | 123.9 | 3.361 [3.343, 3.378] | 7424.5 | 2.749 [2.521, 3.182] |

## Object identity as the view reports it

| condition | st_dev | st_ino | realpath | /proc/self/fd |
| --- | ---: | ---: | --- | --- |
| native | 2052 | 2490375 | `/orpt/content/d000/f00000` | `/orpt/content/d000/f00000` |
| overlay_d1 | 93 | 2490375 | `/orpt/s0_1/v/d000/f00000` | `/orpt/s0_1/v/d000/f00000` |
| overlay_d2 | 93 | 2490375 | `/orpt/s0_2/v/d000/f00000` | `/orpt/s0_2/v/d000/f00000` |
| overlay_d5 | 93 | 2490375 | `/orpt/s0_5/v/d000/f00000` | `/orpt/s0_5/v/d000/f00000` |
| overlay_d10 | 93 | 2490375 | `/orpt/s0_10/v/d000/f00000` | `/orpt/s0_10/v/d000/f00000` |
| overlay_d25 | 93 | 2490375 | `/orpt/s0_25/v/d000/f00000` | `/orpt/s0_25/v/d000/f00000` |
| overlay_d50 | 93 | 2490375 | `/orpt/s0_50/v/d000/f00000` | `/orpt/s0_50/v/d000/f00000` |
