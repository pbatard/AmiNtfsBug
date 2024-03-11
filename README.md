AmiNtfsBug: A test application demonstrating the AMI UEFI NTFS driver bug
=========================================================================

[![Build status](https://img.shields.io/github/actions/workflow/status/pbatard/AmiNtfsBug/build.yml?style=flat-square)](https://github.com/pbatard/AmiNtfsBug/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/release-pre/pbatard/AmiNtfsBug.svg?style=flat-square)](https://github.com/pbatard/AmiNtfsBug/releases)
[![Github stats](https://img.shields.io/github/downloads/pbatard/AmiNtfsBug/total.svg?style=flat-square)](https://github.com/pbatard/AmiNtfsBug/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)

The AMI UEFI NTFS driver (version `0x10000`), commonly used in UEFI firmware produced by the likes of Asus,
Gigabyte, intel and so on, is buggy, and this repository aims at providing the relevant application and test
files to highlight this bug.

## Discovery of the bug

This bug was discovered when extracting UEFI bootable ISO images onto NTFS media, and then trying to validate
the MD5 hashes of the files from within the UEFI environment, using https://github.com/pbatard/uefi-md5sum.

When validating MD5 using a small-enough buffer, and if the application that extracted the files on Windows
also used a small buffer for writing data, we found that MD5 checksum errors were consistently generated
whereas, when validating the same media for MD5 from Linux or Windows, all the checksums matched.

After ruling out the possibility of a bug in the UEFI application, and especially after running the same tests
when using a different UEFI NTFS driver (which produced no errors), we came to the conclusion that the AMI
UEFI NTFS driver has a bug when reading data in chunks that are smaller or equal to the NTFS cluster size
**when** the application that wrote to the file also wrote it in small chunks (typically 2 or 4 KB).

## Replicating the bug on your own

1. On Windows, format an NTFS drive using the facility of your choice, with a cluster size of 4096 or 8192
   (which should be the default for most regular-sized USB Flash Drives).
2. Write content on the drive in blocks of 2048 or 4096 bytes. Note that you **cannot** use Windows file
   explorer or applications like 7-zip to accomplish this, as they use a large write buffer size, which
   does not appear to trigger the bug (or at least not in a manner we have attempted to detect).
3. On a UEFI system using the AMI NTFS driver, use a UEFI application to read the same content in
   sequential blocks of 2048 or 4096 bytes and compare the data with the expected one.
4. If you have written and read a large enough amount of files and data, you **will** find data errors.
   Especially, you are going to find that the AMI driver appears to reset to the start of the file on some
   sequential reads, instead of continuing from the last file position.

## Replicating the bug using this content

The applications and files provided in this repository are designed specifically to trigger the bug.

1. On Windows, format an NTFS drive using the facility of your choice, with a cluster size of 4096 or 8192
   (which should be the default for most regular-sized USB Flash Drives).
2. Extract the full content from the provided archive into a single directory. Make sure that you have all
   of `AmiNtfsBug.exe`, `AmiNtfsBug.efi`, `runme.nsh` and `list.txt` in the same directory.
3. Pick an x64 UEFI shell executable from a trusted source (e.g. https://github.com/pbatard/UEFI-Shell/releases)
   and copy is, as `shellx64.efi` into the same directory where you have `AmiNtfsBug.exe`.
4. From a command line, run `AmiNtfsBug.exe X:` where `X` is the letter of the drive you formatted to NTFS.
5. Let the application create the file structure and boot/test data.
6. Unplug the drive and plug it onto a UEFI system that uses the AMI NTFS driver. Make sure that Secure Boot
   is disabled.
7. Boot the drive into the UEFI Shell and navigate to the NTFS drive you created (e.g. by typing `FS0:`)
8. Type `runme.nsh` and let the script valiate every file through the `AmiNtfsBug.efi` application.
9. By the time the script completes, it should have reported multiple files failing file system driver
   validation.

## Description of the files

`AmiNtfsBug.exe` (source in `src_windows`) is designed to take a list of files with sizes (see below on how
you can generate your own list) and then replicate that file structure onto a target drive, while writing
chunks of alternating bytes (`0x00`, `0x11`, `0x22` ... `0xff`) every `BUFFER_SIZE`.

Once that is done, it also copies the `AmiNtfsBug.efi` and `runme.nsh` from the current directory onto the
target, as well as `shellx64.efi` (into `efi\boot\bootx64.efi`) to make it UEFI bootable.

`AmiNtfsBug.efi` (source in `src_uefi`) is designed to read the start of any individual file larger than
`BUFFER_SIZE * 2`, first through a single `BUFFER_SIZE * 2` read and then through a sequence of two
`BUFFER_SIZE` reads, and then compare the data to validate that it matches.
If it doesn't match, it reports a bug notice along with the name of the file being processed.

`runme.nsh` is a simple recursive UEFI Shell script, that goes through every single file on the current
volume to invoke `AmiNtfsBug.efi` on it.

`list.txt` is the source of the content replication for `AmiNtfsBug.exe`. It is currently based on the
directory structure from the Windows 11 installation image `Win11_23H2_English_x64v2.iso` (minus the 5 GB
`sources/install.wim`), as this is the content we mostly originally tested with and where we had the bug
consistently replicated.

If needed, you can generate your own list content from a Linux system, where, for instance, you have
mounted an ISO into `/mnt/iso`, by issuing something like
`find /mnt/iso -type f -exec stat --format="%s %n" {} \; > list.txt`
and then removing the directory prefix and changing all slashes to backslashes.

## Preemptive rebuttals

* This it **NOT** a toolchain issue as the issue was replicated when using gnu-efi/VS2022 instead of EDK2/gcc
  to create the UEFI test app, or when using MinGW instead of VS2022 win32 to create the Windows test app.
* This is **NOT** a local dev environment issue, as **all** the executables used for testing, along with the
  downloadable archive, were produced through GitHub Actions.
* This is **NOT** a UEFI Shell issue as the issue was replicated when using a UEFI bootloader.
* This is **NOT** a parameters issue (e.g. `EFI_FILE_READ_ONLY`) as the issue still exists with different flags.
* This is **NOT** a `File->SetPosition()` issue, as the issue can be replicated without using `SetPosition` at
  all if one knows the expected data and we only use `SetPosition` as convenience for testing.
* This is **NOT** a POSIX-like vs Win32 API issue in the file creation app, as the issue still exists when using
 `WriteFile()` and friends there.
* This is **NOT** a formatting application issue, as this was tested using different means of formatting a drive
  in Windows.
* This is **NOT** an environmental issue as multiple systems, from different manufacturers, using the AMI NTFS
  driver were tested, and the issue was replicated on all these systems.
* This is **NOT** a selective writing issue (such as trying to replicate a Windows installation drive structure)
  as the issue was replicated with a Debian netinst ISO file structure.
* Furthermore, switching to using the UEFI NTFS driver from https://github.com/pbatard/ntfs-3g/releases (`unload`
  the AMI NTFS driver and `load` the ntfs-3g one) does make the issue disappear.

Therefore, we can only conclude that the AMI UEFI NTFS driver (version `0x10000`) **is** buggy.

## Secure Boot threat assessment

Due to the widespread use of the AMI NTFS driver in systems from ASUS, Gigabyte and Intel, and because it should
be assumed that a malicious actor can produce an NTFS media that will trigger the bug in a Secure Boot enabled
environment, we must consider the possibility of exploitation of this bug as a means to defeat Secure Boot.

In the absolute, this bug could potentially be used to deduplicate existing data from a file at an incorrect
location which could mean running some improper code. However, since this would have to be existing Secure Boot
signed code (rather than arbitrary code) code residing at very specific boundaries, and since this should only
happen if the file is read in small chunks whereas UEFI API calls such as `LoadImage()` are expected to always
read a file in one go, exploitation of this bug in term of code execution looks very difficult.

We therefore assert that the threat assessment of this bug, for Secure Boot, is very low.
