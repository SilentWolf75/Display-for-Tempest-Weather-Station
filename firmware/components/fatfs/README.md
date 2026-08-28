# Vendored ESP-IDF `fatfs` component (v5.5.3) with exFAT enabled

This is a copy of `$IDF_PATH/components/fatfs` from **ESP-IDF 5.5.3**, present
for exactly one reason: to turn on exFAT for the microSD card.

## Why a whole component and not a flag

`src/ffconf.h` has:

```c
#define FF_FS_EXFAT   0
```

It is a plain `#define`, not `#ifndef`-guarded, and ESP-IDF exposes **no
Kconfig option** for it. So it cannot be overridden with `-D` on the command
line, and patching the IDF installation would be global to every project on
this machine and lost on the next IDF update. A project-local component of the
same name overrides the IDF one, which is the supported mechanism.

## What was changed

1. `src/ffconf.h` — `FF_FS_EXFAT` set to `1`, with a comment at the site.
2. Nothing else. Everything here is otherwise byte-identical to 5.5.3.

`CONFIG_FATFS_LFN_HEAP=y` in `sdkconfig.defaults` is also required: exFAT needs
`FF_USE_LFN >= 1`, and the previous `CONFIG_FATFS_LFN_NONE` would have left
`FF_FS_EXFAT` doing nothing.

## Cost of this

- **Pinned to 5.5.3.** On an IDF upgrade, re-copy from the new version and
  re-apply the one-line change, or drop this directory and reformat the card
  FAT32. Diff against `$IDF_PATH/components/fatfs` to check for drift.
- **exFAT is Microsoft-licensed**, which is why Espressif ships it disabled.
  Fine for a personal device; worth knowing before shipping anything.
- Test directories (`test_apps`, `host_test`, `test_fatfsgen`) were not copied;
  they take no part in the build.

The alternative, if this ever becomes a nuisance, is one reformat of the card
to FAT32 and deleting this directory.
