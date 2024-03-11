/*
 * AMI UEFI NTFS driver bug test application
 *
 * This UEFI application is designed to demonstrate a major bug in the
 * AMI UEFI NTFS Driver (version 0x10000) whereas, if you issue successive
 * small reads on a file, the driver "resets" to reading the beginning of
 * the file, instead of continuing where it left of.
 *
 * Copyright © 2024 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Base.h>
#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/EfiShellInterface.h>
#include <Protocol/ShellParameters.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>

#define BUFFER_SIZE 0x1000

STATIC EFI_STATUS TestFsDriver(
	IN CONST EFI_FILE_HANDLE RootHandle,
	IN CHAR16* Path
)
{
	UINTN Size;
	UINT8 Buffer[2][BUFFER_SIZE * 2];
	EFI_STATUS Status;
	EFI_FILE_HANDLE File = NULL;
	EFI_FILE_INFO* Info = NULL;

	// Remove the 'FS#:\' drive prefix if it exists
	if (StrLen(Path) > 5 && Path[3] == L':' &&
		(Path[4] == L'\\' || Path[4] == L'/'))
		Path = &Path[5];

	// Open the file
	Status = RootHandle->Open(RootHandle, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (EFI_ERROR(Status)) {
		Print(L"Could not open %s: %r\n", Path, Status);
		goto out;
	}

	// Get the file size
	Size = SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16);
	Info = AllocateZeroPool(Size);
	if (Info == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		goto out;
	}
	Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, Info);
	if (EFI_ERROR(Status))
		goto out;

	// Only process files that are at least twice our buffer size
	if (Info->FileSize < BUFFER_SIZE * 2) {
		Status = EFI_NO_MAPPING;
		goto out;
	}

	// Now perform the test:
	// 1. Read the first buffer in one go
	Size = BUFFER_SIZE * 2;
	Status = File->Read(File, &Size, Buffer[0]);
	if (EFI_ERROR(Status))
		goto out;

	// 2. Reset the position to the start of the file
	File->SetPosition(File, 0ULL);

	// 3. Read the second buffer in 2 parts
	Size = BUFFER_SIZE;
	Status = File->Read(File, &Size, Buffer[1]);
	if (EFI_ERROR(Status))
		goto out;
	Size = BUFFER_SIZE;
	Status = File->Read(File, &Size, &Buffer[1][BUFFER_SIZE]);
	if (EFI_ERROR(Status))
		goto out;

	// 4. Compare the 2 buffers, which should be identical.
	//    If they aren't, then the file system driver has a bug!
	if (CompareMem(Buffer[0], Buffer[1], BUFFER_SIZE * 2) != 0) {
		Print(L"FS DRIVER BUG! %s\n", Path);
		Status = EFI_COMPROMISED_DATA;
	}

out:
	if (File != NULL)
		File->Close(File);
	return Status;
}

/*
 * Use the UEFI Shell interface to access argc/argv.
 */
STATIC EFI_STATUS GetShellArgs(
	CONST IN EFI_HANDLE BaseImageHandle,
	OUT UINTN* pArgc,
	OUT CHAR16** pArgv[]
)
{
	STATIC CONST EFI_GUID gShellInterfaceProtocolGuid = SHELL_INTERFACE_PROTOCOL_GUID;
	EFI_STATUS Status;
	EFI_SHELL_PARAMETERS_PROTOCOL* ShellParametersProtocol = NULL;
	EFI_SHELL_INTERFACE* ShellInterfaceProtocol = NULL;

	if (pArgc == NULL || pArgv == NULL)
		return EFI_INVALID_PARAMETER;

	// Try the Shell 2.0 interface
	Status = gBS->OpenProtocol(BaseImageHandle, (EFI_GUID*)&gEfiShellParametersProtocolGuid,
		(VOID**)&ShellParametersProtocol, BaseImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (!EFI_ERROR(Status)) {
		*pArgv = ShellParametersProtocol->Argv;
		*pArgc = ShellParametersProtocol->Argc;
		return EFI_SUCCESS;
	}

	// Try the Shell 1.0 interface
	Status = gBS->OpenProtocol(BaseImageHandle, (EFI_GUID*)&gShellInterfaceProtocolGuid,
		(VOID**)&ShellInterfaceProtocol, BaseImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (!EFI_ERROR(Status)) {
		*pArgv = ShellInterfaceProtocol->Argv;
		*pArgc = ShellInterfaceProtocol->Argc;
		return EFI_SUCCESS;
	}

	return EFI_UNSUPPORTED;
}

/*
 * Application entry-point
 */
EFI_STATUS EFIAPI Main(
	IN EFI_HANDLE BaseImageHandle,
	IN EFI_SYSTEM_TABLE* SystemTable
)
{
	UINTN Argc = 0;
	CHAR16** Argv = NULL;
	EFI_STATUS Status;
	EFI_FILE_HANDLE RootHandle;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Volume;
	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;

	// Get the path passed as parameter
	Status = GetShellArgs(BaseImageHandle, &Argc, &Argv);
	if (EFI_ERROR(Status)) {
		Print(L"ERROR: Could not read parameters\n");
		goto out;
	}
	if (Argc != 2) {
		Print(L"USAGE: %s <path>\n", Argv[0]);
		Status = EFI_INVALID_PARAMETER;
		goto out;
	}

	// Access the loaded image to open the current volume
	Status = gBS->OpenProtocol(BaseImageHandle, &gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage, BaseImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status))
		goto out;

	// Open the the root directory on the current volume
	Status = gBS->OpenProtocol(LoadedImage->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid, (VOID**)&Volume,
		BaseImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status))
		goto out;
	Status = Volume->OpenVolume(Volume, &RootHandle);
	if (EFI_ERROR(Status))
		goto out;

	// Run the test on the file provided as parameter
	Status = TestFsDriver(RootHandle, Argv[1]);

out:
	return Status;
}
