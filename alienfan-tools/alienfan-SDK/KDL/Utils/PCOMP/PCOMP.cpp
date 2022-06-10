/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2020 - 2021
*
*  TITLE:       MAIN.CPP
*
*  VERSION:     1.00
*
*  DATE:        18 Apr 2021
*
*  PCOMP - KDU's Provider Compressor.
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/

#include <Windows.h>
#include <msdelta.h>
#include <strsafe.h>

#pragma comment(lib, "msdelta.lib")

#ifdef __cplusplus
extern "C" {
#include "../../Shared/ntos/ntos.h"
#include "../../Shared/minirtl/minirtl.h"
#include "../../Shared/minirtl/cmdline.h"
#include "../../Shared/minirtl/_filename.h"
#include "../../Hamakaze/key.h"
}
#endif

//#define PROVIDER_RES_KEY_DEFAULT        ' uwu'

#define supHeapAlloc(Size) RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, Size)
#define supHeapFree(Ptr) RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, Ptr)

/*
* EncodeBuffer
*
* Purpose:
*
* Decrypt/Encrypt given buffer.
*
*/
VOID EncodeBuffer(
    _In_ PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG Key
)
{
    ULONG k, c;
    PUCHAR ptr;

    if ((Buffer == NULL) || (BufferSize == 0))
        return;

    k = Key;
    c = BufferSize;
    ptr = (PUCHAR)Buffer;

    do {
        *ptr ^= k;
        k = _rotl(k, 1);
        ptr++;
        --c;
    } while (c != 0);
}

/*
* supWriteBufferToFile
*
* Purpose:
*
* Create new file and write buffer to it.
*
*/
BOOL supWriteBufferToFile(
    _In_ LPWSTR lpFileName,
    _In_ PVOID Buffer,
    _In_ DWORD BufferSize
)
{
    HANDLE hFile;
    DWORD bytesIO;

    hFile = CreateFileW(lpFileName,
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    WriteFile(hFile, Buffer, BufferSize, &bytesIO, NULL);
    CloseHandle(hFile);

    return (bytesIO == BufferSize);
}

/*
* supReadFileToBuffer
*
* Purpose:
*
* Read file to buffer. Release memory when it no longer needed.
*
*/
PBYTE supReadFileToBuffer(
    _In_ LPWSTR lpFileName,
    _Inout_opt_ LPDWORD lpBufferSize
)
{
    NTSTATUS    status;
    HANDLE      hFile = NULL;
    PBYTE       Buffer = NULL;
    SIZE_T      sz = 0;

    UNICODE_STRING              usName;
    OBJECT_ATTRIBUTES           attr;
    IO_STATUS_BLOCK             iost;
    FILE_STANDARD_INFORMATION   fi;

    if (lpFileName == NULL)
        return NULL;

    usName.Buffer = NULL;

    do {

        if (!RtlDosPathNameToNtPathName_U(lpFileName, &usName, NULL, NULL))
            break;

        InitializeObjectAttributes(&attr, &usName, OBJ_CASE_INSENSITIVE, NULL, NULL);

        status = NtCreateFile(
            &hFile,
            FILE_READ_DATA | SYNCHRONIZE,
            &attr,
            &iost,
            NULL,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ,
            FILE_OPEN,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
        );

        if (!NT_SUCCESS(status)) {
            break;
        }

        RtlSecureZeroMemory(&fi, sizeof(fi));

        status = NtQueryInformationFile(
            hFile,
            &iost,
            &fi,
            sizeof(FILE_STANDARD_INFORMATION),
            FileStandardInformation);

        if (!NT_SUCCESS(status))
            break;

        sz = (SIZE_T)fi.EndOfFile.LowPart;

        Buffer = (PBYTE)supHeapAlloc(sz);
        if (Buffer) {

            status = NtReadFile(
                hFile,
                NULL,
                NULL,
                NULL,
                &iost,
                Buffer,
                fi.EndOfFile.LowPart,
                NULL,
                NULL);

            if (NT_SUCCESS(status)) {
                if (lpBufferSize)
                    *lpBufferSize = fi.EndOfFile.LowPart;
            }
            else {
                supHeapFree(Buffer);
                Buffer = NULL;
            }
        }

    } while (FALSE);

    if (hFile != NULL) {
        NtClose(hFile);
    }

    if (usName.Buffer)
        RtlFreeUnicodeString(&usName);

    return Buffer;
}

/*
* KDUCompressResource
*
* Purpose:
*
* Compress resource and write it to the disk into new file with same name and .bin extension.
*
*/
VOID KDUCompressResource(
    _In_ LPWSTR lpFileName,
    _In_ ULONG ulCompressKey
)
{
    DWORD fileSize = 0;
    PBYTE fileBuffer;

    DELTA_INPUT d_in, d_target, s_op, t_op, g_op;
    DELTA_OUTPUT d_out;

    printf_s("[+] Reading \"%wS\"\r\n", lpFileName);
    fileBuffer = supReadFileToBuffer(lpFileName, &fileSize);

    if (fileBuffer) {

        printf_s("[+] %lu bytes read\r\n", fileSize);

        PWSTR newFileName;
        SIZE_T sz = _strlen(lpFileName) + (2 * MAX_PATH);

        newFileName = (PWSTR)supHeapAlloc(sz);
        if (newFileName == NULL) {

            printf_s("[!] Could not allocate memory for filename, GetLastError %lu\r\n",
                GetLastError());

        }
        else {

            _filename_noext(newFileName, lpFileName);

            RtlSecureZeroMemory(&d_in, sizeof(DELTA_INPUT));
            d_target.lpcStart = fileBuffer;
            d_target.uSize = fileSize;
            d_target.Editable = FALSE;

            RtlSecureZeroMemory(&s_op, sizeof(DELTA_INPUT));
            RtlSecureZeroMemory(&t_op, sizeof(DELTA_INPUT));
            RtlSecureZeroMemory(&g_op, sizeof(DELTA_INPUT));

            if (CreateDeltaB(DELTA_FILE_TYPE_RAW,
                DELTA_FLAG_NONE,
                DELTA_FLAG_NONE,
                d_in,
                d_target,
                s_op,
                t_op,
                g_op,
                NULL,
                0,
                &d_out))
            {
                SIZE_T writeSize = d_out.uSize;
                PVOID dataBlob = supHeapAlloc(writeSize);
                if (dataBlob) {

                    RtlCopyMemory(dataBlob, d_out.lpStart, writeSize);
                    EncodeBuffer(dataBlob, (ULONG)writeSize, ulCompressKey);

                    _strcat(newFileName, L".bin");

                    printf_s("[+] Saving resource as \"%wS\" with new size %llu bytes\r\n",
                        newFileName,
                        writeSize);

                    if (!supWriteBufferToFile(newFileName,
                        dataBlob,
                        (DWORD)writeSize))
                    {

                        printf_s("[!] Error writing to file \"%wS\", GetLastError %lu\r\n",
                            newFileName,
                            GetLastError());

                    }

                    supHeapFree(dataBlob);
                }

                DeltaFree(d_out.lpStart);
            }
            else {

                printf_s("[!] Error compressing resource, GetLastError %lu\r\n", GetLastError());

            }

            supHeapFree(newFileName);
        }

        supHeapFree(fileBuffer);

    }
    else {

        printf_s("[!] Could not read input file \"%wS\"\r\n", lpFileName);

    }

}

USHORT supChkSum(
    ULONG PartialSum,
    PUSHORT Source,
    ULONG Length
)
{
    while (Length--) {
        PartialSum += *Source++;
        PartialSum = (PartialSum >> 16) + (PartialSum & 0xffff);
    }
    return (USHORT)(((PartialSum >> 16) + PartialSum) & 0xffff);
}

/*
* supVerifyMappedImageMatchesChecksum
*
* Purpose:
*
* Calculate PE file checksum and compare it with checksum in PE header.
*
*/
BOOLEAN supVerifyMappedImageMatchesChecksum(
    _In_ PVOID BaseAddress,
    _In_ ULONG FileLength,
    _Out_opt_ PULONG HeaderChecksum,
    _Out_opt_ PULONG CalculatedChecksum
)
{
    PUSHORT AdjustSum;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG HeaderSum;
    ULONG CheckSum;
    USHORT PartialSum;

    PartialSum = supChkSum(0, (PUSHORT)BaseAddress, (FileLength + 1) >> 1);

    NtHeaders = RtlImageNtHeader(BaseAddress);
    if (NtHeaders) {
        AdjustSum = (PUSHORT)(&NtHeaders->OptionalHeader.CheckSum);
        PartialSum -= (PartialSum < AdjustSum[0]);
        PartialSum -= AdjustSum[0];
        PartialSum -= (PartialSum < AdjustSum[1]);
        PartialSum -= AdjustSum[1];
        HeaderSum = NtHeaders->OptionalHeader.CheckSum;
    }
    else {
        HeaderSum = FileLength;
        PartialSum = 0;
    }

    CheckSum = (ULONG)PartialSum + FileLength;

    if (HeaderChecksum)
        *HeaderChecksum = HeaderSum;
    if (CalculatedChecksum)
        *CalculatedChecksum = CheckSum;

    return (CheckSum == HeaderSum);
}

PVOID KDUDecompressResource(
    _In_ LPWSTR lpFileName,
    //_Out_ PSIZE_T DecompressedSize,
    _In_ ULONG DecryptKey,
    _In_ BOOLEAN VerifyChecksum
)
{
    BOOLEAN bValidData = FALSE;
    DELTA_INPUT diDelta, diSource;
    DELTA_OUTPUT doOutput;
    PVOID resultPtr = NULL;// , dataBlob;
    DWORD fileSize = 0;
    PBYTE fileBuffer;

    ULONG headerSum = 0, calcSum = 0;

    //*DecompressedSize = 0;

    RtlSecureZeroMemory(&diSource, sizeof(DELTA_INPUT));
    RtlSecureZeroMemory(&diDelta, sizeof(DELTA_INPUT));
    RtlSecureZeroMemory(&doOutput, sizeof(DELTA_OUTPUT));

    printf_s("[+] Reading \"%wS\"\r\n", lpFileName);
    fileBuffer = supReadFileToBuffer(lpFileName, &fileSize);

    if (fileBuffer) {

        printf_s("[+] %lu bytes read\r\n", fileSize);

        PWSTR newFileName;
        SIZE_T sz = _strlen(lpFileName) + (2 * MAX_PATH);

        newFileName = (PWSTR)supHeapAlloc(sz);
        if (newFileName == NULL) {

            printf_s("[!] Could not allocate memory for filename, GetLastError %lu\r\n",
                GetLastError());

        }
        else {

            _filename_noext(newFileName, lpFileName);

            EncodeBuffer(fileBuffer, (ULONG)fileSize, DecryptKey);

            diDelta.Editable = FALSE;
            diDelta.lpcStart = fileBuffer;
            diDelta.uSize = fileSize;

            if (ApplyDeltaB(DELTA_FILE_TYPE_RAW, diSource, diDelta, &doOutput)) {

                SIZE_T newSize = doOutput.uSize;
                PVOID decomPtr = doOutput.lpStart;

                bValidData = supVerifyMappedImageMatchesChecksum(decomPtr,
                    (ULONG)newSize,
                    &headerSum,
                    &calcSum);

                if (VerifyChecksum) {

                    if (bValidData == FALSE) {
                        printf("[!] Error data checksum mismatch! Header sum 0x%lx, calculated sum 0x%lx\r\n",
                            headerSum,
                            calcSum);

                    }
                }
                else {

                    if (bValidData == FALSE) {
                        printf_s("[~] Data checksum mismatch, header sum 0x%lx, calculated sum 0x%lx, trying to continue\r\n",
                            headerSum, calcSum);
                    }

                    bValidData = TRUE; //ignore
                }

                if (bValidData) {
                    _strcat(newFileName, L".org");

                    printf_s("[+] Saving resource as \"%wS\" with new size %llu bytes\r\n",
                        newFileName,
                        newSize);

                    if (!supWriteBufferToFile(newFileName,
                        decomPtr,
                        (DWORD)newSize))
                    {

                        printf_s("[!] Error writing to file \"%wS\", GetLastError %lu\r\n",
                            newFileName,
                            GetLastError());

                    }

                    supHeapFree(decomPtr);
                    //resultPtr = (PVOID)supHeapAlloc(newSize);
                    //if (resultPtr) {
                    //    RtlCopyMemory(resultPtr, decomPtr, newSize);
                    //    *DecompressedSize = newSize;
                    //}
                }

                DeltaFree(doOutput.lpStart);
            }
        }
    }
    else {

        printf("[!] Error decompressing resource, GetLastError %lu\r\n", GetLastError());

    }

    return resultPtr;
}

/*
* main
*
* Purpose:
*
* Program entrypoint.
*
*/
int main()
{
    LPWSTR  keyParam = NULL, fNameParam = NULL, szCommand = NULL;
    LPWSTR* szArglist;
    INT     nArgs = 0;

    ULONG provKey = 0;

    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist) {

        if (nArgs > 2) {
            szCommand = szArglist[1];
            fNameParam = szArglist[2];
            keyParam = szArglist[3];

            if (keyParam) {
                provKey = _strtoul(keyParam);
            }

            if (provKey == 0) provKey = PROVIDER_RES_KEY;

            if (fNameParam) {
                if (szCommand[0] == 'c')
                    KDUCompressResource(fNameParam, provKey);
                if (szCommand[0] == 'u')
                    KDUDecompressResource(fNameParam, provKey, false);
                char newH[512];
                sprintf_s(newH, 512, "#define PROVIDER_RES_KEY %ld\r\n", provKey);
                supWriteBufferToFile((LPWSTR)L"../key.h", newH, (DWORD)strlen(newH));
                printf_s("[+] New encryption key is %ld", provKey);
            }
            else {

                printf_s("[!] Unrecognized parameter\r\n");

            }
        }
        else {

            printf_s("[?] KDU Provider De/Compressor, usage: pcomp c/u filename [key]\r\n[!] Input file not specified\r\n");

        }

        LocalFree(szArglist);
    }

    ExitProcess(0);

}
