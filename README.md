AmiNtfsBug: Detailing the AMI UEFI NTFS driver bug
==================================================

[![Release](https://img.shields.io/github/release-pre/pbatard/AmiNtfsBug.svg?style=flat-square)](https://github.com/pbatard/AmiNtfsBug/releases)
[![Github stats](https://img.shields.io/github/downloads/pbatard/AmiNtfsBug/total.svg?style=flat-square)](https://github.com/pbatard/AmiNtfsBug/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)

The AMI UEFI NTFS driver (version `0x10000`), commonly used in UEFI firmware produced by the likes of Asus,
Gigabyte, Dell, intel and so on, is buggy, and this repository provides the relevant application and test
files, and explanation relating to this bug.

Note that this bug was reported to AMI in early 2024, who acknowledged the report... but did **ABSOLUTELY NOTHING**.

So thanks for nothing, AMI, and, especially, thanks for forcing me to waste 3 full days analysing your
stupid code with Ghidra and Claude, when I had much better things to do, just to figure out how broken your
closed-source driver was and how I could **finally** work around that bug in Rufus, because, of course your
company couldn't be bothered to contribute an **Open Source** NTFS driver to EDK2, which would have
benefited everyone, and especially end users of your software, greatly, and where the bug would have been
identified and fixed a long time ago!

## Discovery of the bug

This bug was discovered when extracting UEFI bootable ISO images onto NTFS media, and then trying to validate
the MD5 hashes of the files from within the UEFI environment, using https://github.com/pbatard/uefi-md5sum.

When validating MD5 using a small-enough buffer, and if the application that extracted the files on Windows
also used a small buffer for writing data, we found that MD5 checksum errors were consistently generated
whereas, when validating the same media for MD5 from Linux or Windows, all the checksums matched.

After ruling out the possibility of a bug in the UEFI application, and especially after running the same tests
when using a different UEFI NTFS driver (which produced no errors), we came to the conclusion that the AMI
UEFI NTFS driver had a major bug where it could return completely wrong data when reading a file.

## Replicating the bug with QEMU on Linux, using the provided NTFS VHD

1. Extract the AMI NTFS executable driver as `AmiNtfs.efi` from one of the many UEFI firmware ROMs you can
   find from ASUS, Gigabyte, MSI, Dell and so on, using something like `UEFITool` (since this is proprietary
   copyrighted binary which we don't believe we are legally allowed to distribute).
2. Create a `image/` folder in the current directory and copy `AmiNtfs.efi` there.
3. Unzip the `AmiNtfsBug.vhd.bz2` from the latest release, so that you end up with `AmiNtfsBug.vhd` in the
   current directory.
4. Assuming that you have an x64 `OVMF.fd` UEFI firmware in that same directory, along with QEMU installed
   run:
   ```
   qemu-system-x86_64 -M q35 -nodefaults -nographic -serial stdio -net none -L . -drive if=pflash,format=raw,unit=0,file=OVMF.fd,readonly=on -drive format=raw,file=fat:rw:image -drive format=raw,file=AmiNtfsBug.vhd
   ```
5. In the QEMU environment, from the Shell, issue the commands:
   ```
   load FS0:\AmiNtfs.efi
   map -r
   FS1:\AmiNtfsBug.efi FS1:\sources\setupprep.exe
   ```

You will then see the output, which indicates that reading data that **should** be identical isn't:
```
FS DRIVER BUG! sources\setupprep.exe
o ONE-SHOT READ:
  0x0000: 11111111...
  0x1000: 22222222...
  0x2000: 33333333...
  0x3000: 44444444...
o TWO-PART READ:
  0x0000: 11111111...
  0x1000: 22222222...
  0x2000: DEADBEEF...
  0x3000: BADC0FFE..
```

Now, if you mount the VHD image on Windows or Linux and look at the `sources\setupprep.exe` binary (which is
not an actual Windows executable but generated data) you will see that nowhere in that file do the bytes
`DEADBEEF` or `BADC0FFE` appear. And you can validate that the NTFS volume is not corrupted. And you are of
course invited to look at the source of the `AmiNtfs.efi` application (which is published in this repo) and
recompile your own to validate that this is not a test application issue.

Yet, the AMI NTFS driver does return bytes, that appear in clusters on the NTFS volume that do **NOT**
belong to the `sources\setupprep.exe` file. &rarr; **BUG!!!**

## Comprehensive bug description

### NTFS layout of the file that triggers the bug

This bug occurs when the data of an NTFS file does not occupy a continuous sequence of clusters, but is
split into multiple sequences of continuous ranges, called, in NTFS parlance, VCNs.

On Windows, you can see these VCNs with the command `fsutil`. For instance, assuming that you have the
VHD mounted as `F:`, for the file that triggers the bug, you will see:
```
C:\>fsutil file queryextents F:\sources\setupprep.exe
VCN: 0x0        Clusters: 0x2        LCN: 0x1de3
VCN: 0x2        Clusters: 0x6        LCN: 0x1c3f
VCN: 0x8        Clusters: 0xa        LCN: 0xa93
VCN: 0x12       Clusters: 0x12d      LCN: 0x28de7
```

This tells us that the file is fragmented in 4 parts:
- 1 run of 2 contiguous clusters starting at LCN `0x1de3`
- 1 run of 6 contiguous clusters starting at LCN `0x1c3f`
- 1 run of 10 contiguous clusters starting at LCN `0xa93`
- 1 run of 301 contiguous clusters starting at LCN `0x28de7`

And with the cluster for our VHD being 4096 bytes (`0x1000`) we can actually easily recompose the data
knowing that an LCN is just the sequential cluster number on the volume which means that, since our volume
starts at offset `0x0000000` of the VHD, the first `0x2000` bytes of our file can be found at offset
`0x1de3000` on the VHD. In our case that data is one cluster (`0x1000` bytes) of `0x11...` bytes, and one
cluster of `0x22...` bytes, which is indeed what the `AmiNtfsBug.efi` reported, for both the one-shot and
two-part reads.

At this stage, the next 2 clusters of data a **working** NTFS driver should read for the file are located
at LCN `0x1c3f`, which translates to offset `0x1c3f000` on the VHD. And if we continue to look at our data
with an hex editor, we find `0x33...` and `0x44...`, which is what the one-shot read does indeed report.

Which means that the AMI NTFS driver is reading a completely wrong NTFS cluster, since there is exactly
**one** location on the whole VHD where you can find clusters containing `0xDEADBEEF` and `0xBADC0FFE`
which are at offset `0x1de5000` (i.e. LCN `0x1de5`) and `0x1de6000` (i.e. LCN `0x1de6`) respectively.

Again, per `fsutil file queryextents` these LCNs are **NOT** part of our file, even as they do happen
to be situated right after our first VCN run (which starts at LNC `0x1de3` and "runs" for 2 cluster,
therefore ending right before LCN `0x1de5`).

But, as we will see below, this is not a mere _"The AMI NTFS driver simply doesn't support non contiguous
NTFS data files"_ (which would be a major bug anyway, as, unlike NTFS compression, non-contiguous sequences
of clusters are part of the NTFS specs and not an optional addon, and failure to support anything but
continuous data would have rendered the AMI NTFS utterly worthless as, by default, Windows does create NTFS
files where data isn't contiguous). So there's a lot more to it than a simple unconditional bug where,
every time you have non-contiguous data and switch VCNs, the AMI driver ignores the VCN switch and continues
to read into the wrong cluster sequence.

And for this, we need to look at the actual AMI code.

### The problematic AMI code

The problematic code is located in a function which the AMI driver uses to read data from an NTFS file and
which we logically called `NTFSReadFile()`. In the driver I extracted, it is located at address `0x00004228`.

Note that the following code was mostly generated by Claude, from the Ghidra decompilation:

```C
#pragma pack(1)
typedef struct {
  EFI_FILE_PROTOCOL Protocol;      // 0x00  Revision + 10 function pointers = 0x58 bytes
  EFI_STATUS        Status;        // 0x58  Sticky per-handle error
  NTFS_NODE        *Node;          // 0x60
  VOID             *OnClose;       // 0x68
  UINT64            Position;      // 0x70  Byte offset in the file
  UINT64            CurLcn;        // 0x78  Absolute LCN of the cursor
  UINT32            CurClusterOff; // 0x80  Byte offset inside CurLcn
  INT64             CurRunLen;     // 0x84  Length (clusters) of the run last decoded; written, never read back
  UINT8            *RunList;       // 0x8C  &node->Runs[0], i.e. node+0xD2. NEVER advanced
  UINT8             Pad;           // 0x94
  LIST_ENTRY        Link;          // 0x95  Linked into the volume's open-handle list
} NTFS_FILE_HANDLE;
#pragma pack()

EFI_STATUS NtfsReadFile(NTFS_FILE_HANDLE *File, VOID *Buffer, UINT64 *Len)
{
  NTFS_NODE  *Node = File->Node;
  NTFS_VOL   *Vol  = Node->Vol;
  UINT64      Pos  = File->Position;
  UINT64      Total = 0;
  EFI_STATUS  Status;

  // ---------------------------------------------------------------- resident
  if (Node->Resident == 1) {
    UINT8 *Rec = Vol->MftScratch;                // vol+0x1EF, shared scratch
    Vol->ScratchBusy = 1;
    Status = ReadMftRecord(Vol, Node->MftRef, Rec, NULL);
    if (!EFI_ERROR(Status)) {
      if (*(UINT32 *)Rec == 'FILE') {
        NTFS_ATTR *Attr;
        Status = FindAttribute(Vol, Rec, 0x80 /*$DATA*/, &Attr);
        if (!EFI_ERROR(Status)) {
          // BUG (secondary): *Len was already clamped to (Size - Pos) by FileRead.
          // This should be MIN(ValueLength - Pos, *Len).
          UINT64 N = Attr->ValueLength;
          if (*Len - Pos <= N) N = *Len - Pos;
          CopyMem(Buffer, (UINT8 *)Attr + Attr->ValueOffset + Pos, N);
          Vol->ScratchBusy = 0;
          File->Position += N;
          *Len = N;
          return EFI_SUCCESS;
        }
      } else {
        Status = EFI_VOLUME_CORRUPTED;
      }
    }
    Vol->ScratchBusy = 0;
    return Status;
  }

  // ------------------------------------------------------------ non-resident
  UINT8  *Run = File->RunList;                   // start of the node's runlist copy
  INT64   RunLcn = 0, RunLen = 0;                // RunLcn is the running accumulator

  // Step 1: find the run containing the cursor, searching by LCN.
  //   Run 0: Start=0x1de3 Len=2   covers [0x1de3, 0x1de4]
  //   Run 1: Start=0x1c3f Len=6   covers [0x1c3f, 0x1c44]
  UINT64 RunStartLcn;
  do {
    RunStartLcn = RunLcn;
    if (*Run == 0) break;                                    // end of runlist
    if (DecodeRun(&Run, &RunLen, &RunLcn, TRUE, Run) != 0) break;
    RunStartLcn = RunLcn;
  } while (File->CurLcn <  (UINT64)RunLcn ||                 // cursor below this run, or
           (UINT64)(RunLcn + RunLen) <= File->CurLcn);       // at/above its end

  // BUG (root cause, part 1): if the cursor is in NO run (e.g. 0x1de5), the loop
  // exits at the end of the list. RunStartLcn/RunLen then hold the LAST run
  // decoded (run 3), which has nothing to do with the cursor, and no error is raised.

  // Step 2: read loop
  do {
    UINT32 Off = File->CurClusterOff;
    UINT64 BytesLeftInRun =
        (((RunStartLcn + RunLen) - File->CurLcn) * Vol->SectorsPerCluster << Vol->SectorShift) - Off;
    // BUG (part 1b): with a stale run the subtraction is huge but positive, so the
    // clamp below never trips. With run 3: (0x28de7 + 0x12d - 0x1de5) clusters.
    UINT64 N = MIN(BytesLeftInRun, *Len);
    if (N == 0) return EFI_VOLUME_CORRUPTED;     // 0x800000000000000A

    Status = ReadBytes(Vol, Buffer,
                       Vol->SectorsPerCluster * File->CurLcn + Off / Vol->BytesPerSector,
                       Off % Vol->BytesPerSector, N, 3 /* uncached DiskIo */);
    if (EFI_ERROR(Status)) break;

    UINT32 OldOff = File->CurClusterOff;
    Pos            += N;
    Total          += N;
    *Len           -= N;
    Buffer          = (UINT8 *)Buffer + N;
    File->Position  = Pos;
    File->CurClusterOff = (UINT32)((OldOff + N) % Vol->BytesPerCluster);
    File->CurLcn       += (OldOff + N) / Vol->BytesPerCluster;

    // End-of-run test: did we consume the rest of this run?
    if (N == BytesLeftInRun || File->CurLcn == RunStartLcn + RunLen) {

      // BUG (root cause, part 2): if the caller's request is now satisfied, leave
      // without moving the cursor to the next run. The cursor sits at RunStartLcn+RunLen
      // (one cluster PAST the run, not a cluster of this file), while Position says
      // we are at the start of the next run's VCN. The next call cannot find a run
      // that contains this LCN (see Step 1).
      if (*Len == 0) break;

      if (*Run == 0 ||
          DecodeRun(&Run, &RunLen, &RunLcn, TRUE, Run) < 0) {
        Status = EFI_SUCCESS;                     // ran out of runs: short read, no error
        break;
      }
      File->CurLcn    = RunLcn;                   // cursor = first cluster of next run
      File->CurRunLen = RunLen;
      RunStartLcn     = RunLcn;
    }
  } while (*Len != 0);

  *Len = Total;
  return Status;
}
```

### The bug in the code

The key point is that the cursor is `(CurLcn, CurClusterOff)`, **an absolute cluster, and not
a VCN**. The runlist itself is shared and read-only. Each call re-finds the current run by
searching the runs for the one whose LCN range contains `CurLcn`.

#### Trace of `sources\setupprep.exe` (4 KB clusters)

| Run | VCN | LCN range | Bytes |
|-----|-----|-----------|-------|
| `0` | `0x00–0x01` | `0x1de3–0x1de4` | `0x2000` |
| `1` | `0x02–0x07` | `0x1c3f–0x1c44` | `0x6000` |
| `2` | `0x08–0x11` | `0xa93–0xa9c` | `0xA000` |
| `3` | `0x12–` | `0x28de7–` | |

#### Single 0x4000 "one-shot" read, which is correct.

1. The cursor starts at LCN `0x1de3`, offset `0`. The run walk stops at run `0`.
2. It reads `0x2000` bytes and the cursor advances to `0x1de5`, which is the end of run `0`.
3. `*Len` is still `0x2000`, so the code passes the `*Len == 0` test and decodes run `1`.
4. The cursor becomes `0x1c3f` and it reads `0x2000` bytes from there. Blocks `11` `22` `33` `44` come back right.

#### First `0x2000` read of the "two-shot" pair, which returns the right data.

1. It reads `0x2000` bytes from LCN `0x1de3`, and the cursor advances to `0x1de5`.
2. `N == BytesLeftInRun`, so it enters the end-of-run block. `*Len == 0`, so it takes the `break`.
3. The handle is left with `CurLcn = 0x1de5` and `Position = 0x2000`. However LCN `0x1de5` belongs to no run of this file.
   **PROPER** (non buggy) code would make sure that if the end of the file has not been reached, then the LCN is set to
   the next LCN value where the data continues.

#### Second `0x2000` read of the "two-shot" pair, which returns the wrong data.

1. Step 1, which expects an LCN in the valid VCN ranges, tests the cursor `0x1de5` against each run.
   Run `0` covers `[0x1de3, 0x1de4]` and ends exactly at the cursor, so it is rejected.
   Runs `1` and `2` are below the cursor. Run `3` starts above it.
2. The list ends and the loop exits with run `3` as the stale run. Despite the invalid LCN no error has been detected.
3. `BytesLeftInRun` is huge, so `N = 0x2000`, and it reads at LCN `0x1de5`.
4. LCN `0x1de5` and `0x1de6`, which do not belong to the file, are read and happen to hold `DEADBEEF` and `BADC0FFE`.
5. The cursor is left at `0x1de7`, still outside the file, which means a third read would be wrong too...

#### Preconditions for triggering the bug

* The file has more than one run.
* A read ends exactly at the end of a run, in a request that doesn’t carry on into the next run.
* The next read resumes from that parked cursor.

This is why this bug is highly dependent on the cluster size, the read buffer size, and how Windows elects to write
the files (where continuity, or lack thereof, can be pretty random).

There is also a similar defect in SetPosition(), when seeking, which I will not detail.

#### What the AMI driver should be doing

In the end-of-run block, decode the next run **before** testing `*Len`. When there is no next run (true `EOF`),
it's okayleave the cursor as it is, since the parent call already rejects reads at or past `EOF`.

```C
   if (N == BytesLeftInRun || File->CurLcn == RunStartLcn + RunLen) {
     if (*Run != 0 && DecodeRun(&Run, &RunLen, &RunLcn, TRUE, Run) >= 0) {
       File->CurLcn = RunLcn;  File->CurRunLen = RunLen;  RunStartLcn = RunLcn;
     } else if (*Len != 0) { Status = EFI_SUCCESS; break; }
   }
```

Or, it should have derived the run from the VCN (`Position / BytesPerCluster`) on every read and seek, which is
more robust and walked the runs by cumulative length, not by LCN containment, as well as returning an error
instead of reading when no run matches.

There are also additional minor bugs that Claude found in the seek code, where the `<` in
`(advance while runEndVcn <= targetVcn)` should be changed to to `<=`, as well as in the resident-read length
arithmetic, the sparse-run sign extension in the decoder, and the 128-byte runlist copy.

### Working around the bug

Since AMI don't seem to care about bug reports, and manufacturers aren't going to issue UEFI firmware updates
to fix the AMI screwup, the only option you have, really, is to avoid writing files with non-continuous data.

On Windows, this **should** be achievable by issuing

```C
	SetFilePointerEx(hFileHandle, (LARGE_INTEGER) { .QuadPart = llFileSize }, NULL, FILE_BEGIN);
	SetEndOfFile(hFileHandle);
	SetFilePointerEx(hFileHandle, (LARGE_INTEGER) { .QuadPart = 0ULL }, NULL, FILE_BEGIN);
```

On your file handle, but of course, this might only work if your target volume isn't so fragmented that
finding large enough continuous blocks is possible.

This is actually what 7-zip is doing by default [since version 17.0](https://sourceforge.net/p/sevenzip/feature-requests/981/)
and the reason why, if you use 7-zip or some other applications that use a similar trick, the bug will not
be triggered. The 4.16 release of Rufus also does the same, since it **finally** allows its users to use
the _runtime UEFI validation_ feature on NTFS volumes, which otherwise would report false positives because
of this utterly maddening bug.

## Supplementary Notes

### Creating your own NTFS volume to replicate the bug

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

## Replicating the bug using your own content

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

## Secure Boot threat assessment

Due to the widespread use of the AMI NTFS driver in systems from ASUS, Gigabyte and Intel, and because it should
be assumed that a malicious actor can produce an NTFS media that will trigger the bug in a Secure Boot enabled
environment, we must consider the possibility of exploitation of this bug as a means to defeat Secure Boot.

In the absolute, this bug could potentially be used to deduplicate existing data from a file at an incorrect
location which could mean running some improper code. However, since this would have to be existing Secure Boot
signed code (rather than arbitrary code) residing at very specific boundaries, and since this should only happen
if the file is read in small chunks whereas UEFI API calls such as `LoadImage()` are expected to always read a
file in one go, exploitation of this bug in term of code execution looks very difficult.

We therefore assert that the threat assessment of this bug, for Secure Boot, is very low.
