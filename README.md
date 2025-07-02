[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct-single.svg)](https://stand-with-ukraine.pp.ua)
# redumper
Copyright 2021-2025 Hennadiy Brych

## Intro
redumper is a low-level byte-perfect CD disc dumper. It supports incremental dumps, advanced SCSI/C2 repair, intelligent audio CD offset detection and a lot of other features.

redumper is also a general purpose DVD/HD-DVD/Blu-ray disc dumper. DVD and later media format dumping is implemented using high-level mode thus most of the complexities described in this document are related to the CD technology. If you intend to dump DVD media and later, you can usually use any drive and it just works.

Everything is written from scratch in C++.

## Operating Systems Support
Available for Windows, Linux and macOS.

## General
redumper operates using commands.

The preferred way is to run it without arguments (equivalent to running `redumper cd`). This is the most common use case that will dump the disc and generate the dump files. Everything that can be automated, is automated. If the `--drive` option is not specified, the first available drive with an inserted disc will be used. If the `--image-name` option is not specified, the image name will be generated based on the current date and drive name.

The command list is available by running `redumper --help`.

## Windows
The drive is specified using drive letter: e.g. `--drive=E:` or `--drive=E`.

## Linux
The drive is specified using the full path of its device file: `--drive=/dev/sr0`. The disc has to be unmounted before running redumper. I suggest disabling removable drives automounting.

At the time of writing, drive autodetection doesn't work for some Linux distributions. Autodetection is based on kernel sysfs guidelines but some distributions deviate from that (or I didn't implement it right). This will be fixed at some point and the current suggested workaround is to specify the drive directly. One thing to mention is that the device file has to represent a generic SCSI device as write commands will be executed (not in a sense of disc burning, but to alter drive state such as setting drive speed). 

## macOS
Before running for the first time, you'll need to install an `llvm` prerequisite via Homebrew: `brew install llvm@18`. (Without this installed, you will get an error: `Library not loaded: /opt/homebrew/opt/llvm/lib/c++/libc++.1.dylib`.) Additionally, you may also require removing the quarantine block that macOS may add to a downloaded redumper executable: `xattr -dr com.apple.quarantine {your/path/to/}redumper`, adjusting the path to redumper for your machine.

The drive is specified using its BSD name, e.g. for /dev/disk2 that will be: `--drive=disk2`. As with Linux, the disc has to be unmounted before running redumper. Insert the disc, use `diskutil list` to see a list of all disks in the system. Identify your disc drive and execute `diskutil unmountDisk /dev/disk2`. After redumper finishes executing, macOS takes over and mounts the disc again, which is very annoying. Also my Mac Mini seems to scan a disc before mounting. I haven't found a way how to disable that, and this will be blocking the disc access until done.

Currently, the macOS version is distributed as an ELF binary only - there is no installation package available. You should download and extract the binary and run it from the command line. Another issue is that the binary requires SCSI drive access and doesn't run from Desktop / Downloads / Documents directories. I suggest to copy it to /usr/local/bin and run it from there. Overall, the macOS version needs more work (auto unmount, better package support etc.) and I will work on it at some point but right now it's low priority.

## Supported Drives
Known good PLEXTOR/LG/ASUS/LITE-ON drive models are fully supported and recommended for the perfect dump. The full list is [HERE](drive.ixx#L130). Drives listed under "//OTHER" are bad drives that I own, they are listed purely for experimentation - please do not buy these drives!

While there is some GENERIC drive support implemented, GENERIC drives often will not provide a perfect dump due to drive firmware limitations. Feel free to use this functionality at your leisure but at the moment I don't have the bandwidth for supporting such cases, so you're on your own.

## Good Drives Technical
D8 and BE CDDA read opcodes are supported using compatible drives (PLEXTOR and LG/ASUS/LITE-ON respectively). Everything, including data tracks, is read as audio. On an initial dump pass, each sector is dumped from start to end in a linear fashion. Known slow sectors such as multisession gaps (session Lead-in area) are skipped. redumper never seeks back and a great care is exercised not to put an excessive wear on the drive.

If drive is a good PLEXTOR, the session lead-in will be read using a negative LBA range. Several lead-in read attempts are made to ensure data integrity. Sometimes it's possible to capture other session lead-in but there is no direct control of it.

A good LG/ASUS/LITE-ON can natively read ~135 out of 150 pre-gap sectors, which is usually enough for 99% of discs. Very rarely, there is some non zero data in early lead-in sectors (TOC area). Such drives are unable to extract this data and a PLEXTOR is needed.

A good PLEXTOR can read ~100 lead-out sectors. Good LG/ASUS/LITE-ONs have lead-out sectors locked but these drives usually have a cache read opcode that is used to extract available lead-out sectors. Finally, an ASUS BW-16D1HT flashed with RibShark custom 3.10 firmware has fully unlocked lead-out. All these cases are supported.

In order to accomodate for first session lead-in sectors, primary scrambled dump output (.scram file) prepends an extra space of 45150 sectors (10 minutes, a maximum addressable negative MSF range that can be used to access first session lead-in).

The resulting dump is drive read offset corrected but not combined offset corrected (the disc write offset is determined at a later track split stage). Sector dump state (SUCCESS / SUCCESS_SCSI_OFF / SUCCESS_C2_OFF / ERROR_C2 / ERROR_SKIP) is stored for each sample. 1 sample is 4 bytes (2 16-bit signed samples of stereo audio data), e.g. for 1 sector there are 588 state values. All this allows an incremental dump improvement with sample granularity using different drives with different read offsets.

Subchannel data is stored uncorrected RAW (multiplexed). Both TOC-based and Subchannel Q-based splits are supported; subchannel Q is corrected in memory and never stored on disk. This allows to keep subchannel-based protection schemes (libcrypt, SecuROM etc.) for a later analysis, as well as future R-W packs extraction (CD+G, CD+MIDI). Disc write offset detection is calculated based on a variety of methods: data track as an addressing difference between data sector MSF and subchannel Q MSF, data / audio track intersection of BE read method was used, silence based Perfect Audio Offset detection, CDi-Ready data in index 0 offset detection. The split will fail if the track sector range contains SCSI/C2 errors or is inaccessible, e.g.: a track split of an ASUS dump without cache lead-out data if the combined offset is positive. 

## GENERIC Drives Feature Evaluation Guide
### General
This is a guide that will help you to evaluate your GENERIC drive features and make it work with redumper manually. Eventually it will be automated but it's a low priority for now.
I do not provide support and I don't need feedback. Please do not create issues related to adding GENERIC drives to redumper source code - I don't have bandwidth for that.

### Requirements
Any not too scratched mixed-mode disc where the first track is a data track and following tracks are audio tracks will work (e.g. no multisession).

### Steps
**1. Identify drive sector order**

According to SCSI MMC specifications, RAW CD sector order is data-c2-subcode (DATA_C2_SUB). In practice, this is not always the case. For some drives it's data-subcode-c2 (DATA_SUB_C2). This is complicated by the fact that there are drives that don't support C2 error vectors and there are drives that don't support subcode (subchannel) reading. Some, but not all, drive features can be queried using SCSI GET_CONFIGURATION and MODE_SENSE commands but again, sometimes these declarations don't match what the drive firmware actually does. For these reasons, redumper doesn't use any feature information from the drive, instead, the logic is based on executing the actual commands and checking returned status codes. It's recommended to always specify the drive speed as some drives don't have good defaults (too slow or too fast) and in particular for this test it's advised to keep drive speed on the lower end so 8 is a good default. Setting drive type to GENERIC is absolutely required as by default redumper accepts only known good drives. The default drive sector order is DATA_C2_SUB but it's specified here to make it obvious. The other arguments are making redumper verbose, allowing overwrite of output files and specifying the default dump name. Dump files will be overwritten by the subsequent redumper invocations and after the test, dump files can be safely deleted - they don't serve any purpose. redumper can be killed any moment from the console by sending it SIGINT signal (Ctrl+C); there is no need to wait for the full dump if it's obvious that something is not right.

Run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=DATA_C2_SUB --verbose --overwrite --image-name=drive_test` and analyze the output. 3 outcomes are possible:

 - normal dump operation, sector counter steadily grows, "Q" counter is low and grows slowly (some subcode errors are likely), ocassional SCSI/C2 errors are possible: This is a likely outcome and an indication that drive sector order is successfully identified. Wait until the dump process finishes to make sure that the same sector order works for audio sectors and you can proceed to the next step
 - mass SCSI errors are an indication of unsupported C2 or subcode: Restart the test with --drive-sector-order=DATA_SUB (no C2 support is more likely). If that didn't help, restart again with --drive-sector-order=DATA_C2 (no subcode support, less likely)
 - mass C2 errors, sector, "SCSI/C2" and "Q" counters are growing equally fast: This hints at a swapped C2/subcode order. Restart the test with --drive-sector-order=DATA_SUB_C2 to confirm the assumption

**2. Identify if drive can read lead-in**

Lead-in consists of the Table of Contents (TOC) and 2 seconds of pre-gap. In general, TOC is not interesting as data from there is accessible using READ_TOC SCSI command. On the other hand, being able to dump pre-gap is very important for audio discs for two reasons: shifted first track data due to a negative combined offset (left) or HTOA (Hidden Track One Audio) which can use whole 2 seconds of pre-gap. It's less important for data discs as the negative combined offset is compensated by the drive firmware if the BE read command is used (more about that later) and according to the specifications, the pre-gap of a data track should be zeroed (but scrambled) and it really is in almost all cases.

If redumper doesn't know the GENERIC drive, by default it reads starting from LBA 0 which usually is the first sector of the first track. When the previous step was executed to identify the drive sector order, we omitted setting it not to affect sector order detection. Now that the sector order is known, we can safely test it. As was mentioned earlier, the total pre-gap size is 2 seconds, which is 150 sectors total (1 second is 75 sectors). By using a negative LBA range [-150..-1] we can access the disc pre-gap. In theory it's all good, but in practice most drives are not able to access early pre-gap sectors and there is a pretty reasonable explanation for that. As the drive reads discs from left to right (from center outward), in order to seek the requested sector LBA, the drive has to position "somewhere earlier" in that area (left) and using subcode Q positional information, and read (from left to right) until it finds the requested LBA. The problem is, that the pre-gap is preceeded by the TOC and TOC information is stored in subcode Q using a different format. For example, there is no positional information there so by seeking "before" the pre-gap the drive doesn't know where is it located and gets confused, will try to reposition, read again and so on. That said, usually skipping the first 15 pre-gap sectors is a good default for the next test.

Run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=-135 --verbose --overwrite --image-name=drive_test` and analyze the output. A number of outcomes are possible:

 - no SCSI errors, drive dumps specified negative LBA pre-gap range and continues dumping from LBA 0: All set, the specified value is the earliest pre-gap LBA the drive can dump, proceed to the next step
 - SCSI errors, error number is equal to the specified pre-gap start (for -135 it will be 135 errors): Drive firmware blocks negative LBA range and no pre-gap sectors are accessible at all, proceed to the next step
 - SCSI errors, error number is less than the specified pre-gap start: Reduce pre-gap start value and try again
 - sector counter froze and drive makes weird noises: Reduce pre-gap start value and restart the test
 - some drives can read pre-gap but give a single SCSI error for a single LBA such as -1: Just use your best judgement, redumper can handle anything and it will mark such a sector as inaccessible and if this particular sector is required later (depending on a track type / combined offset / split options) it will fail on a track split, and even that can be overriden, for science of course.

**3. Identify if drive can read lead-out**

Being able to dump a few lead-out sectors is sometimes important for discs with a positive combined offset (shifted right). There can be non-zero sample values if the last track is an audio track or some non-zero data bytes in case of a data track. Depending on a combination of drive read offset and disc write offset, the missing amount can be quite significant, more than 1 sector. The exact conditions where lead-out sectors are needed depend on the combined offset, the drive read method and the nature of data stored at the end of the last track. It often can be audio silence or empty late data sectors which can be easily generated but there is no guarantee.

redumper is greedy, it tries to overread lead-out sectors while it can. It stops reading either on a SCSI error (usually blocked by firmware) or on a slow sector (a session boundary or a disc edge).

This test is quite simple, run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --verbose --overwrite --image-name=drive_test`

Note the total sectors count value at start, the number after sector counter "/" symbol and let the dump process complete. After redumper exits, compare the same total sectors count value - if it grew, the difference would be the number of lead-out sectors the drive successfully read. An unlikely outcome will be that drive never stops, the total sectors count value continuously grows and the "Q" counter is stale. This means that the drive firmware is buggy and it just returns the last valid sector value from the cache, so kill the process.

**4. Identify better drive read method**

The previous steps were executed with the default drive read method BE. BE is guaranteed to be supported by all drives but has a number of disadvantages such as sync aligned and possibly corrected data sectors by the drive firmware and inability to read sectors on a data/audio track boundary. Aligned data sectors mean that it won't be possible to detect a true disc write offset based on a data track. This is very important for a mixed-mode disc as disc write offset is used to shift audio tracks appropriately and that is an absolute requirement for a perfect dump.

There are two alternative read methods that might be supported by the drive, D8 and BE CDDA. Both rely on a concept where data sectors are read as audio sectors thus preventing drive data track offset correction and possible data sector altering. D8 used to be a legacy command to read audio sectors from early SCSI MMC specifications. Some drives like good PLEXTORs preserved such a command and it's unlocked for any sector type, that's why a good PLEXTOR is awesome! The next best way is to use BE read method but specify CDDA expected sector type for all sectors. According to the SCSI MMC specifications, BE CDDA expected sector type setting should return a SCSI error if a data sector is encountered, but some drives like good ASUS do not enforce that requirement and that's why a good ASUS is nice too!

First try to use BE CDDA method, run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=BE_CDDA --verbose --overwrite --image-name=drive_test`

If you get no mass SCSI errors, the drive supports it. In general, prefer BE_CDDA over BE mode but keep in mind that some drives alter and offset correct data sectors even if BE CDDA is used. A good test would be either to inspect the .scram file manually or perform a full redumper dump with track split and see if the detected disc write offset is reasonable (known CD from redump.org database and offsets match). 

Finally, try your luck with D8. Run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=D8 --verbose --overwrite --image-name=drive_test`

Your chances grow if it's an early drive (or a PLEXTOR in disguise). If you get no SCSI errors, congratulations, this is the best read method available, go get yourself a beer! If you get mass SCSI errors, the last thing to try is to exclude C2 from the drive sector order and restart the test, as some drives are unable to use C2 in that mode. 

**5. Is it a good PLEXTOR or a good ASUS rebadge?**

If the D8 read method is available, it might be a good hint that the drive is a PLEXTOR in disguise. There are good PLEXTORs in QPS external enclosures, HP and Creative rebadges, and some other rebadge rumors. If the BE_CDDA read method is available and the drive can read at least 135 pre-gap sectors, there is a chance that the drive might be a good ASUS in disguise, for instance a recently found LITE-ON. There are a few cache partition configurations possible, all based on 8 MB and 3 MB cache sizes.

By setting the --drive-type value, you can instruct redumper to use good drive specific features, such as reading lead-in using PLEXTOR negative range or reading lead-out from ASUS cache.

To check for a good PLEXTOR, run `redumper dump --speed=8 --drive-type=PLEXTOR --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=D8 --verbose --overwrite --image-name=drive_test`

Look for a "PLEXTOR: reading lead-in" message. If the process ends with a "PLEXTOR: lead-in found" message, this is a good PLEXTOR. If the LBA counter decreases all the way and it's at least a minute long, you can kill the process, as it's not a good PLEXTOR.

To check for a good ASUS, run `redumper dump --speed=8 --drive-type=LG_ASU8 --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=BE_CDDA --verbose --overwrite --image-name=drive_test`

Look for a "LG/ASUS: searching lead-out in cache" message which will appear right after reading the last LBA. If the next message is "LG/ASUS: lead-out found", it's a good ASUS. If you get "error: read cache failed", there is no read cache command and it's not a good ASUS.

A proper distinction between different cache partition configurations is very important but it requires manual cache dump analysis. In a nutshell, first you have to establish the true cache size. The above command hardcodes an 8 MB cache size as it's the maximum possible size. If the cache size is smaller, for example 3 MB, and it's being read as 8 MB, it wraps around by the firmware. Basically you have to open the .asus cache file in a hex editor and determine where it starts repeating data. For example, search for the next appearance of the first 8 bytes you see at the start of the file. If you found a match, it's address will basically be the cache size. If it's not found, it's likely a 8 MB cache size. Even if the cache size is the same, multiple partition configurations are possible. Some are already enumerated [HERE](drive.ixx#L256), where the first value is the cache size in MB, and the second value is a number of sectors that are stored in cache. Determining the second value is out of scope here - it requires parsing cache subchannel to see where the boundary is. I have some tools for that but it's not user friendly at the moment. When you determine the cache size, predefined cache configurations most likely will work but that requires extensive testing using discs with various write offsets and comparing to a known good dump.


## Examples
**1.**

`redumper`

If run without arguments, redumper will use the first available supported drive with a disc inside and "cd" aggregate mode. The image name will be autogenerated based on a date/time and drive path and will be put in the current process working directory. If you have two drives and the first drive is already busy dumping, running redumper again will dump the disc in the next available drive and so on, for easy daisy chaining. Please take into account that no SCSI/C2 rereads will be performed on errors unless you specify --retries=100, but don't worry as it won't let you split to tracks if tracks have errors.

**2.**

`redumper cd --verbose --drive=F: --retries=100 --image-name=my_dump_name --image-path=my_dump_directory`

or (you can use spaces and = interchangeably)

`redumper cd --verbose --drive F: --retries 100 --image-name my_dump_name --image-path my_dump_directory`

This will dump a disc in drive F: with 100 retries count in case of errors (refine). The dump files will be stored in the my_dump_directory directory, dump files will have a my_dump_name base name, and you will get verbose messages.

**3.**

`redumper refine --verbose --drive=G: --speed=8 --retries=500 --image-name=my_dump_name --image-path=my_dump_directory`

This will refine a previous dump from the my_dump_directory with a base name of my_dump_name on a different drive using lowest speed with 500 retries count.

**4.**

`redumper split --verbose --force --image-name=my_dump_name --image-path=my_dump_directory`

This will force generation of track split with track errors, just because you really want to see and experiment with unscrambled tracks.

## Building from Source

redumper uses C++ modules, which requires a C++20 compiler and build tools:
- LLVM/Clang 18 (Linux & macOS) or Microsoft Visual Studio 2022 (Windows)
- Ninja 1.11 (Linux & macOS only)
- CMake 3.28

For detailed build instructions, please check my GitHub Actions workflow [HERE](.github/workflows/cmake.yml). I do not provide a Docker container and I will not be able to troubleshoot your build issues. If my GitHub Actions CI/CD can build it, so can you.

## Contact
Discord: **supermegag**
