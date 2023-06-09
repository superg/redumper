# redumper
Copyright 2021-2023 Hennadiy Brych

## Ukraine
![alt text](https://github.com/superg/redumper/blob/main/ukraine.jpg)

Support Ukrainian Army in fighting back russian aggression. 

https://bank.gov.ua/en/news/all/natsionalniy-bank-vidkriv-spetsrahunok-dlya-zboru-koshtiv-na-potrebi-armiyi

https://savelife.in.ua/en

https://prytulafoundation.org/en

Preservation is not important if people are denied their right to exist. Слава Україні!

## Intro
A fresh take on a CD dumper utility. The main goals are to develop a portable, byte perfect CD dumper which supports incremental dumps, advanced SCSI/C2 repair and is future proof. Everything is written from scratch in C++. Available for both Windows and Linux, MacOSX version is planned.

## General
The dumper operates using modes. The current modes are "dump", "protection", "refine", "split" and "info" but for the convenience sake, the aggregate mode "cd" does it all in a row and covers most of the use cases.

**dump**: Dumps cd and stores it in a set of files.

**protection**: Scans dump for protections.

**refine**: Improves the dump, tries to correct found SCSI/C2 errors and fill missing sectors based on a drive features, example: refining an initial LG/ASUS dump on PLEXTOR will add missing lead-in and possibly more lead-out sectors based on what originally was extracted from LG/ASUS cache. You can refine as many times as you want. You can refine the same disc on a different (but supported) drive. If you have two identical discs with different damage, you can refine from both (TOC has to be identical).

**split**: Performs track split and generates a CUE-sheet, disc is not required at this point.

**info**: Generates an info file with the specific information tailored for redump.org.

Everything is being actively developed so modes / options may change, always use --help to see the latest information.

## Supported Drives
Known good PLEXTOR/LG/ASUS/LITE-ON drive models are fully supported and recommended for the perfect dump, the full list is [HERE](drive.ixx#L130).
GENERIC drive support is implemented. In some cases, GENERIC drives will not provide a perfect dump but redumper will do it's best given the drive hardware/firmware limitations. Read on for GENERIC drive features evaluation guide.

## Good Drives Technical
D8 and BE CDDA read opcodes are supported using the compatible drives (PLEXTOR and LG/ASUS/LITE-ON respectively). Everything, including data tracks, is read as audio. On a dump stage, each sector is dumped sequentially from the start to end in a linear fashion. Known drive inaccessible / slow sectors (e.g. multisession gaps) are skipped forward but dumper never seeks back and a great care is exercised not to put an excessive wear on drive. Everything that is possible to extract will be extracted. Lead-out session overread is supported. Lead-in/pre-gap session reading is implemented by using PLEXTOR negative LBA range feature, tricks are in place to be able to extract this data for both sessions whenever possible. The main scrambled dump output prepends an extra space of 45150 sectors (10 minutes) in order to save first session lead-in (10 minutes is a maximum addressable negative MSF range that can be used to access first session lead-in). LG/ASUS/LITE-ON cache session lead-out extraction is supported for 8Mb and 3Mb cache configurations. Both PLEXTOR lead-in/pre-gap and LG/ASUS/LITE-ON lead-out are multiplexed into main scrambled dump file using absolute addressing from subchannel Q in order to minimize the chance of error.

The resulting dump is drive read offset corrected but not combined offset corrected (the disc write offset is determined at a later track split stage). Sector dump state (SUCCESS / SUCCESS_SCSI_OFF / SUCCESS_C2_OFF / ERROR_C2 / ERROR_SKIP) is stored for each sample, 1 sample is 4 bytes (2 16-bit signed samples of stereo audio data) e.g. for 1 sector there are 588 state values. All this allows an incremental dump improvement with sample granularity using different drives with different read offsets.

Subchannel data is stored uncorrected RAW (multiplexed). Both TOC based and Subchannel Q based splits are supported, subchannel Q is corrected in memory and never stored on disk. This allows to keep subchannel based protection schemes (libcrypt, SecuROM etc) for a later analysis, as well as future R-W packs extraction (CD+G CD+MIDI). Disc write offset detection is calculated based on a variety of methods, data track as an addressing difference between data sector MSF and subchannel Q MSF, data / audio track intersection of BE read method was used, silence based Perfect Audio Offset detection, CDi-Ready data in index 0 offset detection. Split will fail if track sector range contains SCSI/C2 errors or inaccessible, example: track split of ASUS dump without cache lead-out data if the combined offset is positive. 

## GENERIC Drives Feature Evaluation Guide
### Requirements
Any not too scratched mixed mode disc where the first track is a data track and following tracks are audio tracks will work (e.g. no multisession).

### Steps
**1. Identify drive sector order**

According to SCSI MMC specifications, RAW CD sector order is data-c2-subcode (DATA_C2_SUB). In practice, this is not always the case. For some drives it's data-subcode-c2 (DATA_SUB_C2). This is complicated by the fact that there are drives that don't support C2 error vectors and there are drives that don't support subcode (subchannel) reading. Some, but not all, drive features can be queried using SCSI GET_CONFIGURATION and MODE_SENSE commands but again, sometimes these declarations don't match what drive firmware actually does. For these reasons redumper doesn't use any feature information from the drive, instead, the logic is based on executing the actual commands and checking returned status codes. It's recommended to always specify the drive speed as some drives don't have good defaults (too slow or too fast) and in particular for this test it's adviced to keep drive speed on the lower end so 8 is a good default. Setting drive type to GENERIC is absolutely required as by default redumper accepts only known good drives. The default drive sector order is DATA_C2_SUB but it's specified here to make it obvious. The other arguments are making redumper verbose, allow overwrite of output files and specify the default dump name. Dump files will be overwritten by the subsequent redumper invocations and after the test, dump files can be safely deleted, they don't serve any purpose. redumper can be killed any moment from the console by sending it SIGINT signal (Ctrl+C), no need to wait for the full dump if it's obvious that something is not right.

Run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=DATA_C2_SUB --verbose --overwrite --image-name=drive_test` and analyze the output, 3 outcomes are possible:

 - normal dump operation, sector counter steadily grows, "Q" counter is low and grows slowly (some subcode errors are likely), ocassional SCSI/C2 errors are possible, this is a likely outcome and an indication that drive sector order is successfully identified, wait until the dump process finishes to make sure that the same sector order works for audio sectors and you can proceed to the next step
 - mass SCSI errors, an indication of unsupported C2 or subcode, restart the test with --drive-sector-order=DATA_SUB (no C2 support is more likely), if that didn't help, restart again with --drive-sector-order=DATA_C2 (no subcode support, less likely)
 - mass C2 errors, sector, "SCSI/C2" and "Q" counters are growing equally fast, this hints at swapped C2/subcode order, restart the test with --drive-sector-order=DATA_SUB_C2 to confirm the assumption

**2. Identify if drive can read lead-in**

Lead-in consists of Table of Contents (TOC) and 2 seconds of pre-gap. In general, TOC is not interesting as data from there is accessible using READ_TOC SCSI command. On the other hand, being able to dump pre-gap is very important for audio discs because of two reasons: shifted first track data due to a negative combined offset (left) or HTOA (Hidden Track One Audio) which can use whole 2 seconds of pre-gap. It's less important for the data discs as the negative combined offset is compensated by the drive firmware if BE read command is used (more about that later) and according to the specifications, pre-gap of a data track should be zeroed (but scrambled) and it really is in almost all cases.

If redumper doesn't know the GENERIC drive, by default it reads starting from LBA 0 which usually is the first sector of a first track. When previous step was executed to identify drive sector order, we omitted setting it not to affect sector order detection. Now that sector order is known, we can safely test it. As it was mentioned earlier, total pre-gap size is 2 seconds, that's 150 sectors total (1 second is 75 sectors). By using negative LBA range [-150..-1] we can access disc pre-gap. In theory it's all good, in practice, most drives are not able to access early pre-gap sectors and there is pretty reasonable explanation for that. As drive reads disc from left to right (from center outward), in order to seek requested sector LBA, drive has to position "somewhere earlier" in that area (left) and using subcode Q positional information, and read (from left to right) until it finds the requested LBA. The problem is, that pre-gap is preceeded by TOC and TOC information is stored in subcode Q using different format e.g. there is no positional information there so by seeking "before" pre-gap the drive doesn't know where is it located and gets confused, will try to reposition, read again and so on. That said, usually skipping first 15 pre-gap sectors is a good default for the next test.

Run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=-135 --verbose --overwrite --image-name=drive_test` and analyze the output, a number of outcomes are possible:

 - no SCSI errors, drive dumps specified negative LBA pre-gap range and continues dumping from LBA 0, all set, the specified value is the earliest pre-gap LBA the drive can dump, proceed to the next step
 - SCSI errors, error number is equal to the specified pre-gap start (for -135 it will be 135 errors), drive firmware blocks negative LBA range and no pre-gap sectors are accessible at all, proceed to the next step
 - SCSI errors, error number is less than the specified pre-gap start, reduce pre-gap start value and try again
 - sector counter froze and drive makes weird noises, reduce pre-gap start value and restart the test
 - some drives can read pre-gap but give a single SCSI error for a single LBA such as -1, just use your best judgement, redumper can handle anything and it will mark such a sector as inaccessible and if this particular sector is required later (depending on a track type / combined offset / split options) it will fail on a track split, and even that can be overriden, for science of course. 

**3. Identify if drive can read lead-out**

Being able to dump a few lead-out sectors is sometimes important for the discs with positive combined offset (shifted right). There can be non zero sample values if the last track is audio track or some non zero data bytes in case of a data track. Depending on a combination of drive read offset and disc write offset the missing amount can be quite significant, more than 1 sector. The exact conditions where lead-out sectors are needed depend on combined offset, drive read method and nature of data stored in the end of the last track. It often can be audio silence or empty late data sectors which can be easily generated but there is no guarantee.

redumper is greedy, it tries to overread lead-out sectors until it can. It stops reading either on SCSI error (usually blocked by firmware) or on a slow sector (session boundary or a disc edge).

This test is quite simple, run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --verbose --overwrite --image-name=drive_test`
Note the total sectors count value at start, the number after sector counter "/" symbol and let the dump process complete. After redumper exits, compare same total sectors count value, if it grew, the difference would be the number of lead-out sectors the drive successfully read. An unlikely outcome will be that drive never stops, total sectors count value continuously grow and "Q" counter is stale, this means that drive firmware is buggy and it just returns the last valid sector value from the cache, kill the process.

**4. Identify better drive read method**

Previous steps were executed with the default drive read method BE. BE is guaranteed to be supported by all drives but has a number of disadvantages such as sync aligned and possibly corrected data sectors by the drive firmware and inability to read sectors on data/audio track boundary. Aligned data sectors mean that it won't be possible to detect a true disc write offset based on a data track. This is very important for a mixed mode disc as disc write offset is used to shift audio tracks appropriately and that is an absolute requirement for a perfect dump. There are two alternative read methods that might be supported by the drive, D8 and BE CDDA. Both rely on a concept where data sectors are read as audio sectors thus preventing drive data track offset correction and possible data sector altering. D8 used to be a legacy command to read audio sectors from early SCSI MMC specifications. Some drives like good PLEXTOR preserved such a command and it's unlocked for any sector type, that's why good PLEXTOR is awesome! Next best way is to use BE read method but specify CDDA expected sector type for all sectors. According to SCSI MMC specifications, BE CDDA expected sector type setting should return SCSI error if data sector is encountered, but some drives like good ASUS do not enforce that requirement and that's why good ASUS is nice too!

First try to use BE CDDA method, run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=BE_CDDA --verbose --overwrite --image-name=drive_test`

If you get no mass SCSI errors, the drive supports it, in general prefer BE_CDDA over BE mode but keep in mind that some drives alter and offset correct data sectors even if BE CDDA is used, a good test would be either to inspect .scram file manually or perform a full redumper dump with track split and see if detected disc write offset is reasonable (known CD from redump.org database and offsets match). 

Finally, try your luck with D8, run `redumper dump --speed=8 --drive-type=GENERIC --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=D8 --verbose --overwrite --image-name=drive_test`

Your chances grow if it's an early drive (or PLEXTOR in disguise). If you get no SCSI errors, congratulations, this is the best read method available, go get yourself a beer! If you get mass SCSI errors, the last thing to try is to exclude C2 from drive sector order and restart the test, some drives are unable to use C2 in that mode. 

**5. Is it a good PLEXTOR or a good ASUS rebadge?**

If D8 read method is available, it might be a good hint that the drive is PLEXTOR in a disguise, there are good PLEXTORs in QPS external enclosures, HP and Creative rebadges and some other rebadge rumors. If BE_CDDA read method is available and drive can read at least 135 pre-gap sectors, there is a chance that drive might be a good ASUS in a disguise, for instance recently found LITE-ON, two cache varieties here, 8Mb and 3Mb. There is a way to check both assumptions.

By setting --drive-type value, you can instruct to use good drive specific features, such as read lead-in using PLEXTOR negative range or read lead-out from ASUS cache.

To check for a good PLEXTOR, run `redumper dump --speed=8 --drive-type=PLEXTOR --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=D8 --verbose --overwrite --image-name=drive_test`

Look for "PLEXTOR: reading lead-in" message, if process ends with "PLEXTOR: lead-in found" message, this is a good PLEXTOR. If LBA counter decreases all the way and it's at least a minute long, you can kill the process, it's not good PLEXTOR.

To check for a good ASUS, you will have to check for two possible cache configurations, first run `redumper dump --speed=8 --drive-type=LG_ASU8 --drive-sector-order=<sector_order_from_previous_step> --drive-pregap-start=<pregap_start_from_previous_step> --drive-read-method=BE_CDDA --verbose --overwrite --image-name=drive_test`

Look for "LG/ASUS: searching lead-out in cache" message which will appear right after reading last LBA, if the next message is "LG/ASUS: lead-out found", it's a good ASUS. If you get "error: read cache failed", there is no read cache command and it's not a good ASUS. If you don't get SCSI error but no lead-out sectors count, it might be a different cache configuration, restart the test with --drive-type=LG_ASU3. Proper distinction between different cache sizes is out of scope here, it requires manual cache dump analysis.

I would love to know your results if you think you found good PLEXTOR or good ASUS, please contact me so I can evaluate it and add it to the known drives database.


## Examples
**1. Super lazy:**

`redumper`

If run without arguments, the dumper will use the first available supported drive with disc inside and "cd" aggregate mode. The image name will be autogenerated based on a date/time and drive path and will be put in the current process working directory. If you have two drives and the first drive is already busy dumping, running it again will dump the disc in the next available drive and so on, easy daisy chaining. Please take into account that no SCSI/C2 rereads will be performed on errors unless you specify --retries=100, but don't worry as it won't let you split to tracks if tracks have errors.

**2. Concerned citizen:**

`redumper cd --verbose --drive=F: --retries=100 --image-name=my_dump_name --image-path=my_dump_directory`

or (you can use spaces and = interchangeably)

`redumper cd --verbose --drive F: --retries 100 --image-name my_dump_name --image-path my_dump_directory`

Will dump a disc in drive F: with 100 retries count in case of errors (refine). The dump files will be stored in my_dump_directory directory and dump files will have my_dump_name base name and you will get verbose messages.

**3. You know what you do:**

`redumper refine --verbose --drive=G: --speed=8 --retries=500 --image-name=my_dump_name --image-path=my_dump_directory`

Refine a previous dump from my_dump_directory with base name my_dump_name on a different drive using lowest speed with different retries count.

**4. Advanced:**

`redumper split --verbose --force --image-name=my_dump_name --image-path=my_dump_directory`

Force generation of track split with track errors, just because you really want to see and experiment with unscrambled tracks.

## Building from Source

[OUTDATED]

Some users may wish to build the software from source, particularly Linux users who may not have the same system configuration as was used to generate the Release versions. This process is fairly simple.

1. Ensure you have the `cmake` and `build-essentials` packages on your system.
2. Clone this repository. Because this repository includes `fmt` as a submodule, you will need to include this in your clone command. Therefore it should look like: `git clone --recurse-submodules git@github.com:superg/redumper.git`
3. `cd redumper`
4. This is optional, but it is preferred to created a dedicated `build` directory separate from the source code. Therefore, `mkdir build` followed by `cd build`.
5. `cmake ..`
6. `cmake --build . --target redumper`

This should create the `redumper` executable within your `build` directory. You can then move the executable anywhere you please.


## Contacts
E-mail: gennadiy.brich@gmail.com

Discord: superg#9200
