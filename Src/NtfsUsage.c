
#include <stdio.h>

#define _WIN32_WINNT 0x500

#include <windows.h>
#include <winioctl.h>

typedef struct _NTFS_EXTENDED_INFO
{
	ULONGLONG	FileSize;
	ULONGLONG	NumOfClusters;
	ULONGLONG	StorageSize;

} NTFS_EXTENDED_INFO;


void GetNtfsExtInfo(WIN32_FIND_DATA *		FindData, 
	   				NTFS_EXTENDED_INFO *	ExtendedInfo)
{
	static ULONG BytesPerCluster = 0;



	if (BytesPerCluster == 0) 
	{
		DWORD SectorsPerCluster;
		DWORD BytesPerSector;
		DWORD NumberOfFreeClusters;
		DWORD TotalNumberOfClusters;

		/*	Note: Can also retrieve this information via a call to 
			DeviceIoControl with an IO code of FSCTL_GET_NTFS_VOLUME_DATA.
		*/
		if (GetDiskFreeSpace(NULL, 
			                 &SectorsPerCluster, 
							 &BytesPerSector, 
							 &NumberOfFreeClusters, 
							 &TotalNumberOfClusters) == TRUE)
			BytesPerCluster = BytesPerSector * SectorsPerCluster;

		/*
		printf("\nBytesPerCluster=%lu\n", BytesPerCluster);
		*/
	}


	/* Get File Size */
	if (FindData->nFileSizeHigh == 0)
		ExtendedInfo->FileSize = (ULONGLONG) (FindData->nFileSizeLow);
	else
		ExtendedInfo->FileSize = (ULONGLONG) ((FindData->nFileSizeHigh * MAXDWORD) + FindData->nFileSizeLow);

	/* Get Number of Clusters */
	{
	HANDLE	hFile;
	DWORD	AccessMode = FILE_READ_ATTRIBUTES;
	
	ExtendedInfo->NumOfClusters = 0;

	if (FindData->dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED)
		AccessMode = AccessMode | FILE_READ_DATA; 
		
		hFile = CreateFile(FindData->cFileName, 
			               GENERIC_READ, 
						   FILE_SHARE_READ | FILE_SHARE_WRITE, 
						   NULL, 
						   OPEN_EXISTING, 
						   FILE_ATTRIBUTE_NORMAL  | FILE_FLAG_OPEN_NO_RECALL,
						   NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			STARTING_VCN_INPUT_BUFFER	StartVcn;
			DWORD						BytesRet;
			BYTE *						Buffer;
			DWORD						BufSize;

			memset(&StartVcn, 0, sizeof(StartVcn));
			
			BufSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + ((2 * sizeof(LARGE_INTEGER)) * 512);
			Buffer = (BYTE *) malloc(BufSize);

			printf("\nBufSize=%lu, RPTRSSize=%lu\n", BufSize, sizeof(RETRIEVAL_POINTERS_BUFFER));

			do
			{
				if (DeviceIoControl(hFile, 
									FSCTL_GET_RETRIEVAL_POINTERS, 
									&StartVcn,
									sizeof(StartVcn),
									Buffer,
									BufSize,
									&BytesRet,
									NULL))
				{
					ExtendedInfo->NumOfClusters = ((RETRIEVAL_POINTERS_BUFFER *) Buffer)->ExtentCount;
				}
				else
				{
					printf("\nDeviceIoControl failed.\n");
					switch (GetLastError())
					{
						case ERROR_INVALID_PARAMETER:
							printf("\n ERROR_INVALID_PARAMETER \n");
							break;

						case ERROR_INSUFFICIENT_BUFFER:
							printf("\n ERROR_INSUFFICIENT_BUFFER \n");
							break;

						case ERROR_NOT_READY:
							printf("\n ERROR_NOT_READY \n");
							break;

						case ERROR_INVALID_USER_BUFFER:
							printf("\n ERROR_INVALID_USER_BUFFER \n");
							break;

						case ERROR_HANDLE_EOF:
							printf("\n ERROR_HANDLE_EOF \n");
							break;

						case NO_ERROR:
							printf("\n NO_ERROR \n");
							break;

						case ERROR_MORE_DATA:
							printf("\n ERROR_MORE_DATA \n");
							break;

						default:
							printf("\n Unknown error %ld: \n", GetLastError());
							break;
					}
				}
			} while (GetLastError() == ERROR_MORE_DATA);


			CloseHandle(hFile);
		}
		else
		{
			printf("\nFileOpen failed.\n");
		}
	/*
	ExtendedInfo->NumOfClusters = ExtendedInfo->FileSize / BytesPerCluster;
	*/
	}

	/* Get Storage Size */
	ExtendedInfo->StorageSize = ExtendedInfo->NumOfClusters * BytesPerCluster;
}


void main(int argc, char * argv[])
{
	HANDLE				hFind;
	WIN32_FIND_DATA		FindData;
	NTFS_EXTENDED_INFO	NtfsExtInfo;
	ULONGLONG			TotalFiles;
	ULONGLONG			TotalBytes;
	TCHAR				FileSysName[32];
	

	/* Make sure a search spec was entered */
	if (argc < 2) 
	{
		printf("Usage is: NtfsUsage <search spec>\n\n");
		return;
	}

	/* Make sure this is an NTFS volume */
	if (!GetVolumeInformation(NULL, NULL, 0, NULL, NULL, NULL, FileSysName, 32))
	{
		printf("\nUnable to get volume information.\n");
		return;
	}
	else
	{
		if (strcmp(FileSysName, "NTFS") != 0)
		{
			printf("\nThe filesystem must be NTFS. It is recognized as '%s'.\n", FileSysName);
			return;
		}
	}


	hFind = FindFirstFile(argv[1], &FindData);
	if (hFind == INVALID_HANDLE_VALUE) 
	{
		printf("Unable to find first file.\n\n");
		return;
	}

	TotalFiles = 0L;
	TotalBytes = 0L;
	

	while (TRUE)
	{
		if (FindNextFile(hFind, &FindData) == FALSE) 
			break;

		printf("\t'%s'\n", FindData.cFileName);

		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			printf("\t\tSkipped: Directory\n");
			continue;
		}

		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
		{
			printf("\t\tSkipped: Hidden File\n");
			continue;
		}

		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
		{
			printf("\t\tSkipped: System File\n");
			continue;
		}

		/*
		if (InSkipList(FindData.cFileName))
		{
			printf("\t\tSkipped: In Skip List\n");
			continue;
		}
		*/

		GetNtfsExtInfo(&FindData, &NtfsExtInfo);
		printf("\t\tFileSize:     %I64u \n", NtfsExtInfo.FileSize);
		/*
		printf("\t\tNumOfClusters: %I64u \n", NtfsExtInfo.NumOfClusters);
		printf("\t\tStorageSize:   %I64u \n", NtfsExtInfo.StorageSize);
		*/

		TotalFiles++;
		TotalBytes = TotalBytes + NtfsExtInfo.FileSize;
	}
	FindClose(hFind);

	printf("\nResults:\n");
	printf("\n\tTotal Files: %I64u\n", TotalFiles);
	printf("\tTotal Bytes: %I64u\n", TotalBytes);
}