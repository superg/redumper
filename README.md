# redumper — Low-Level Optical Disc Dumper

redumper is a cross-platform command-line tool for creating accurate optical disc dumps. It provides a low-level, byte-perfect workflow for CDs, including incremental refinement, SCSI/C2 error recovery, raw subchannel preservation, and automatic audio-offset detection. It also supports dumping of DVD, HD DVD, and Blu-ray media.

redumper was originally created for the [Redump](https://redump.info/) disc-preservation initiative and is now the default dumping software used for Redump optical-disc submissions.

redumper is written from scratch in C++ and runs on Windows, Linux, and macOS.

## Features

- Low-level CD acquisition with compatible PLEXTOR and MediaTek-based drives
- Sample-granular dump state for incremental refinement across multiple drives
- SCSI and C2 error detection, retries, and repair
- Automatic drive read-offset correction and disc write-offset detection
- Raw, uncorrected subchannel storage with TOC- and Q-based track splitting
- Lead-in, pre-gap, and lead-out extraction on supported drives
- Protection analysis, image information, hashing, and skeleton generation
- High-level DVD, HD DVD, and Blu-ray dumping
- Raw DVD and Blu-ray sector dumping with [OmniDrive](https://github.com/RibShark/OmniDrive) firmware
- Nintendo GameCube, Wii, and Wii U disc dumping with OmniDrive firmware
- DVD CSS key extraction and title-key recovery
- Xbox and Xbox 360 DVD dumping with compatible Kreon or OmniDrive firmware
- Drive capability testing and selected firmware flashing tools

## Quick start

Download and extract the archive for your platform from the [latest GitHub release](https://github.com/superg/redumper/releases/latest). The release workflow publishes x86, x64, and ARM64 builds where supported.

Insert a disc and run redumper without arguments:

```console
redumper
```

This is equivalent to `redumper disc`, the aggregate workflow that dumps the disc and produces the applicable output files. Unless explicitly configured, redumper selects the first available drive containing a disc and generates an image name from the date and drive identity.

By default, redumper records errors but performs no sector retries. For a dump that retries SCSI/C2 errors, specify an appropriate retry count:

```console
redumper disc --retries=100
```

Existing output files are protected unless `--overwrite` is supplied.

For the complete command and option reference, run:

```console
redumper --help
```

## Platform notes

### Windows

Specify a drive by its drive letter:

```console
redumper --drive=E:
```

The trailing colon is optional.

### Linux

The disc must be unmounted before redumper accesses it. Specify the generic SCSI device, not the block device:

```console
redumper --drive=/dev/sg1
```

Use `/dev/sgN`, rather than `/dev/srN`, because redumper sends SCSI commands that change drive state, such as the requested read speed. If automatic drive detection does not work on a distribution whose sysfs layout differs from the kernel convention, pass the device path explicitly. Disabling removable-media automounting is recommended on dedicated dumping systems.

### macOS

Specify the drive by its BSD name, which can be found with `diskutil list`:

```console
redumper --drive=disk2
```

The macOS archive contains `bin/redumper` together with private C++ runtime libraries under `lib`. The executable resolves those libraries relative to its own location through `@executable_path/../lib`, so extract and move the complete directory tree rather than copying the executable by itself.

macOS 10.15 and later place Desktop, Documents, and Downloads behind its [privacy-controlled Files and Folders policy](https://support.apple.com/guide/security/controlling-app-access-to-files-in-macos-secddd1d86a6/web). That policy applies to command-line programs through the terminal application that launched them. redumper also requires a lower-level operation than normal file access: after unmounting the disc, it asks IOKit to create an MMC device user client, obtains a `SCSITaskDeviceInterface`, and requests exclusive access so it can send raw SCSI commands directly to the drive.

When redumper is launched from a privacy-controlled directory and macOS blocks creation of that IOKit user-client plug-in, `IOCreatePlugInInterfaceForService` returns `kIOReturnNoResources`. IOKit formats that generic status as `(iokit/common) resource shortage`; in this situation it does not indicate that the Mac is low on memory or disk space. redumper reports the failure as:

```text
error: failed to create service plugin interface, MACH ((iokit/common) resource shortage)
```

Move the entire extracted redumper directory to a normal local location outside Desktop, Documents, and Downloads—for example `~/Applications/redumper`—and run `bin/redumper` from there. This both preserves the executable-to-library layout and avoids the privacy boundary while redumper establishes its IOKit SCSI connection.

## Drive support

For accurate CD dumping, use a drive recognized by redumper. Compatible PLEXTOR drives use vendor-specific negative-range D8 CDDA access, which allows redumper to recover the complete disc lead-in; supported LG, ASUS, LITE-ON, and other MediaTek-based drives use the BE CDDA path and, where available, vendor-specific cache access.

The authoritative drive database is maintained in [`drive.ixx`](drive.ixx). The installed executable can display it directly:

```console
redumper --list-recommended-drives
redumper --list-all-drives
```

Generic MMC drives can be used experimentally, but firmware limitations may prevent a complete or byte-perfect CD dump. DVD, HD DVD, and Blu-ray dumping generally does not require a specific model. Xbox and Xbox 360 DVD media are the exception and require a compatible drive with Kreon or OmniDrive firmware.

Compatible drives running [OmniDrive](https://github.com/RibShark/OmniDrive) firmware can use redumper's raw DVD and Blu-ray acquisition paths. Enable them explicitly with `--dvd-raw` or `--bd-raw`:

```console
redumper disc --drive=<drive> --dvd-raw
redumper disc --drive=<drive> --bd-raw
```

OmniDrive also enables redumper to dump Xbox, Xbox 360, Nintendo GameCube, Wii, and Wii U discs. redumper detects these proprietary formats and automatically uses the appropriate raw acquisition path; Nintendo discs are also descrambled as part of the dump. No additional raw-mode option is required. If you are getting SCSI errors with OmniDrive, try lower the transfer length (`--dump-read-size`).

### Test an unknown drive

`drive::test` replaces the former manual generic-drive evaluation procedure. Insert a known-good mixed-mode CD containing at least one data track and one audio track, then run:

```console
redumper drive::test --drive=<drive>
```

The test probes supported READ CD variants and sector layouts, D8 support, PLEXTOR lead-in access, readable pre-gap and lead-out ranges, and the MediaTek F1 cache-read command. Add `--verbose` to include SCSI failures. If a drive hangs while a vendor-specific probe is attempted, power-cycle it and skip that probe on the next run:

```console
redumper drive::test --drive=<drive> --drive-test-skip-plextor-leadin
redumper drive::test --drive=<drive> --drive-test-skip-cache-read
```

The normal `disc` workflow can also attempt to detect the sector order of an unrecognized generic drive:

```console
redumper disc --drive=<drive> --drive-type=GENERIC --auto-detect
```

## Common workflows

### Choose the output location

```console
redumper disc --drive=F: --retries=100 --image-name=my_dump --image-path=my_dump_directory
```

This dumps the disc in drive `F:`, retries erroneous sectors up to 100 times, and writes files using `my_dump` as the basename under `my_dump_directory`. Add `--verbose` for detailed diagnostics.

Options accept either `--option=value` or `--option value` syntax.

### Refine an existing dump

```console
redumper refine --drive=G: --speed=8 --retries=500 --image-name=my_dump --image-path=my_dump_directory
```

This re-reads unresolved areas of an existing dump, possibly with a different drive or speed. Keep the same image path and name so redumper can find the dump state created by the initial pass.

### Force a track split

```console
redumper split --force-split --image-name=my_dump --image-path=my_dump_directory
```

Normally, track splitting fails if required sectors contain errors or are inaccessible. `--force-split` generates the BIN/CUE output anyway for analysis or experimentation; it should not be treated as a verified dump.

## How CD dumping works

Compatible drives read every track as audio, including data tracks, using D8 or BE CDDA commands. The initial pass proceeds linearly from beginning to end and skips known slow regions such as multisession gaps. Avoiding backward seeks during this pass reduces unnecessary drive wear.

Supported PLEXTOR drives can read session lead-in through a negative LBA range. Many supported MediaTek-based drives read about 135 of the 150 pre-gap sectors and may recover otherwise locked lead-out sectors through a cache-read command. Some rare discs contain non-zero data earlier in the lead-in and therefore require a capable PLEXTOR drive.

The primary scrambled CD dump (`.scram`) reserves 45,150 sectors before LBA 0: ten minutes, the largest negative MSF range addressable for first-session lead-in access.

redumper corrects the drive read offset during acquisition. The combined offset remains uncorrected until splitting, when the disc write offset is derived. Dump state is recorded for every stereo sample—588 state values per CD sector—using states such as `SUCCESS`, `SUCCESS_SCSI_OFF`, `SUCCESS_C2_OFF`, `ERROR_C2`, and `ERROR_SKIP`. This allows later refinement at sample granularity with drives that have different read offsets.

Subchannel data is stored raw, multiplexed, and uncorrected. redumper can split from either the drive TOC or corrected Subchannel Q information without rewriting the stored subchannel data. This preserves protection-related data, including LibCrypt and SecuROM patterns, for later analysis and leaves room for future extraction of R-W content such as CD+G and CD+MIDI.

Write-offset detection combines several available signals, including the difference between data-sector MSF and Subchannel Q MSF, data/audio boundaries, silence-based Perfect Audio Offset detection, and CD-i Ready data in index 0. Unless forced, splitting stops when its required sector range contains SCSI/C2 errors or cannot be read.

## Firmware flashing

redumper contains flashing commands for selected MT1339, MT1959, Samsung SD-616F/T, and PLEXTOR drives:

```console
redumper flash::mt1339 --drive=<drive> --firmware=<firmware.bin>
redumper flash::mt1959 --drive=<drive> --firmware=<firmware.bin>
redumper flash::sd616 --drive=<drive> --firmware=<firmware.bin>
redumper flash::plextor --drive=<drive> --firmware=<firmware.bin>
```

> [!WARNING]
> Firmware flashing is inherently dangerous and can permanently brick a drive. Use only firmware intended for the exact drive and chipset, keep the drive connected throughout the operation, and ensure power cannot be interrupted. `--force-flash` bypasses vendor/model verification and increases the risk; use it only when you fully understand the target hardware.

## Building from source

redumper uses C++20 modules. The CI-supported build environments use:

- CMake 3.28 or newer
- LLVM/Clang 18 and Ninja on Linux and macOS
- Microsoft Visual Studio 2022 on Windows

The checked-in toolchain files under [`cmake/toolchains`](cmake/toolchains) define the supported platform and architecture combinations. For example, an x64 Linux release build is configured and tested with:

```console
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x64.cmake
cmake --build build
ctest --test-dir build --output-on-failure
```

On x64 Windows with Visual Studio 2022:

```console
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-x64.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

If a suitable GoogleTest installation is not available, the test configuration downloads it during configuration and requires network access at that stage. The complete release matrix is in [`.github/workflows/cmake.yml`](.github/workflows/cmake.yml).

## Author

redumper is created and maintained by [Hennadiy Brych](https://github.com/superg). Discord: **supermegag**.

## Need help?

Start with `redumper --help` and, for drive questions, include the output of `redumper --list-all-drives` or `redumper drive::test --verbose` as applicable. For bugs and project questions, open an issue in the [redumper issue tracker](https://github.com/superg/redumper/issues) and include the exact command plus the complete error or warning output. Do not upload or attach copyrighted disc contents.

## License

redumper is free software licensed under the [GNU General Public License, version 3](LICENSE).
