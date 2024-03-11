/*
 * AmiNtfsBug - Fill an NTFS drive to trigger the AMI UEFI NTFS driver bug
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "msapi_utf8.h"

// BUFFER_SIZE should be smaller than the NTFS cluster size to trigger the bug
#define BUFFER_SIZE							0x1000

// Convenience function calls
static __inline bool file_exists(const char* path)
{
	struct _stat64 st;
	return (_stat64U(path, &st) == 0) && (((st.st_mode) & _S_IFMT) == _S_IFREG);
}

static __inline bool dir_exists(const char* path)
{
	struct _stat64 st;
	return (_stat64U(path, &st) == 0) && (((st.st_mode) & _S_IFMT) == _S_IFDIR);
}

static __inline bool create_dirs(char* path)
{
	bool r;
	char* c = strrchr(path, '\\');
	*c = '\0';
	r = _mkdirExU(path);
	*c = '\\';
	return (r == 0);
}

// UTF-8 main()
int main_utf8(int argc, char** argv)
{
	static const char* required_file[] = { "list.txt", "runme.nsh", "AmiNtfsBug.efi", "shellx64.efi" };
	char src[MAX_PATH], dst[MAX_PATH] = "#:\\";

	fprintf(stderr, "AmiNtfsBug © 2024 Pete Batard <pete@akeo.ie>\n\n");
	fprintf(stderr, "This program is free software; you can redistribute it and/or modify it under \n");
	fprintf(stderr, "the terms of the GNU General Public License as published by the Free Software \n");
	fprintf(stderr, "Foundation; either version 3 of the License or any later version.\n\n");
	fprintf(stderr, "Official project and latest downloads at: https://github.com/pbatard/AmiNtfsBug\n\n");

	// Validate parameters
	if (argc != 2 || strlen(argv[1]) != 2 || argv[1][1] != ':') {
		fprintf(stderr, "Usage: AmiNtfsBug <Drive>\n");
		fprintf(stderr, "Where <Drive> is a removable Windows NTFS drive (such as F:)\n");
		return 1;
	}

	// Prevent copying the files onto the C: drive
	if (argv[1][0] == 'c' || argv[1][0] == 'C') {
		fprintf(stderr, "ERROR: Destination drive cannot be C:\n");
		return 1;
	}

	// Check that we have all the prerequisite files
	for (int i = 0; i < ARRAYSIZE(required_file); i++) {
		if (!file_exists(required_file[i])) {
			fprintf(stderr, "ERROR: '%s' must exist in the current directory\n", required_file[i]);
			return 1;
		}
	}

	// Keep a copy of the source directory path
	if (_getcwdU(src, sizeof(src)) == NULL) {
		fprintf(stderr, "ERROR: Could not get source dir\n");
		return 1;
	}
	if (src[strlen(src) - 1] != '\\')
		strcat_s(src, sizeof(src), "\\");

	// Set up the destination drive and validate access
	dst[0] = argv[1][0];
	if (!dir_exists(dst)) {
		fprintf(stderr, "ERROR: Could not access destination drive\n");
		return 1;
	}

	// Create a buffer filled 0x00, 0x11, 0x22... values
	uint8_t* buf = malloc(16 * BUFFER_SIZE);
	if (buf == NULL)
		return 1;
	for (int i = 0; i < 16; i++)
		memset(&buf[i * BUFFER_SIZE], i * 0x11, BUFFER_SIZE);

	// Open the source list file
	FILE* list = fopenU(required_file[0], "r");
	if (list == NULL) {
		fprintf(stderr, "ERROR: Can't open '%s'\n", required_file[0]);
		free(buf);
		return 1;
	}

	// Parse the list to create files with the same location and size
	// on the target drive, filled with data from our buffer.
	uint64_t size;
	int offset = 0;
	while (fscanf_s(list, "%lld %s", &size, &dst[3], (unsigned int)sizeof(dst) - 3) != EOF) {
		// Create the subdirectories
		if (!create_dirs(dst)) {
			fprintf(stderr, "ERROR: Can't create subdirectories for '%s'\n", dst);
			free(buf);
			return 1;
		}

		// Create the file
		FILE* file = fopenU(dst, "wb");
		if (file == NULL) {
			fprintf(stderr, "ERROR: Can't write '%s'\n", dst);
			free(buf);
			return 1;
		}

		// Write the file in BUFFER_SIZE chunks of alternating data
		printf("CREATING: %s (%lld bytes)\n", dst, size);
		for (int64_t rem = size; rem > 0; rem -= BUFFER_SIZE) {
			if (fwrite(&buf[(offset++ % 16) * BUFFER_SIZE], 1,
				min(BUFFER_SIZE, rem), file) != min(BUFFER_SIZE, rem)) {
				fprintf(stderr, "ERROR: Can't write '%s'\n", dst);
				free(buf);
				return 1;
			}
		}
		fclose(file);
	}
	free(buf);
	fclose(list);

	// Copy the prerequisite files
	strcpy_s(&dst[3], sizeof(dst) - 3, "efi\\boot");
	create_dirs(dst);
	size_t src_len = strlen(src);
	for (int i = 1; i < ARRAYSIZE(required_file); i++) {
		strcpy_s(&src[src_len], sizeof(src) - src_len, required_file[i]);
		strcpy_s(&dst[3], sizeof(dst) - 3, (i == 3) ? "efi\\boot\\bootx64.efi" : required_file[i]);
		printf("COPYING: %s\n", dst);
		if (!CopyFileU(src, dst, FALSE)) {
			fprintf(stderr, "ERROR: Can't copy '%s'\n", dst);
			return 1;
		}
	}

	return 0;
}

// UTF-16 main()
int wmain(int argc, wchar_t** argv16)
{
	SetConsoleOutputCP(CP_UTF8);
	char** argv = calloc(argc, sizeof(char*));
	if (argv == NULL)
		return -1;
	for (int i = 0; i < argc; i++)
		argv[i] = wchar_to_utf8(argv16[i]);
	int r = main_utf8(argc, argv);
	for (int i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
	return r;
}
