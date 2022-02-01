#============================================================================
# 7ZIP (liblzma) easy extraction functions wrapper
#-------------------------------------------------
#
# Author: Salavat Tulebaev (salavat-tulebaev@yandex.ru), 2010.
#
# Copyright 2010 Alexander Potemkin (dispatch.mailbox@gmail.com)
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#============================================================================


#include <stdio.h>

#include "7z.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "7zAlloc.h"

#ifndef USE_WINDOWS_FILE
/* for mkdir */
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif
#endif

#ifdef _WIN32
#define CHAR_PATH_SEPARATOR '\\'
#else
#define CHAR_PATH_SEPARATOR '/'
#endif

static ISzAlloc g_Alloc = { SzAlloc, SzFree };

/* Allocation dynamic memory block of the specified 'size' */
/*
 LZMA library uses it's own dynamic memory dispatcher. Memory blocks
 allocated at the first time are bigger then requested. And In case if
 during the new requests, the requested size is smaller the one
 allocated earlier, then a new memory actual allocation doesn't happen.
 */
static int Buf_EnsureSize(CBuf *dest, size_t size)
{
  if (dest->size >= size)
    return 1;
  Buf_Free(dest, &g_Alloc);
  return Buf_Create(dest, size, &g_Alloc);
}

/* Code supporting widechar strings on non-Windows systems */
#ifndef _WIN32

static Byte kUtf8Limits[5] = { 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

static Bool Utf16_To_Utf8(Byte *dest, size_t *destLen, const UInt16 *src, size_t srcLen)
{
  size_t destPos = 0, srcPos = 0;
  for (;;)
  {
    unsigned numAdds;
    UInt32 value;
    if (srcPos == srcLen)
    {
      *destLen = destPos;
      return True;
    }
    value = src[srcPos++];
    if (value < 0x80)
    {
      if (dest)
        dest[destPos] = (char)value;
      destPos++;
      continue;
    }
    if (value >= 0xD800 && value < 0xE000)
    {
      UInt32 c2;
      if (value >= 0xDC00 || srcPos == srcLen)
        break;
      c2 = src[srcPos++];
      if (c2 < 0xDC00 || c2 >= 0xE000)
        break;
      value = (((value - 0xD800) << 10) | (c2 - 0xDC00)) + 0x10000;
    }
    for (numAdds = 1; numAdds < 5; numAdds++)
      if (value < (((UInt32)1) << (numAdds * 5 + 6)))
        break;
    if (dest)
      dest[destPos] = (char)(kUtf8Limits[numAdds - 1] + (value >> (6 * numAdds)));
    destPos++;
    do
    {
      numAdds--;
      if (dest)
        dest[destPos] = (char)(0x80 + ((value >> (6 * numAdds)) & 0x3F));
      destPos++;
    }
    while (numAdds != 0);
  }
  *destLen = destPos;
  return False;
}

static SRes Utf16_To_Utf8Buf(CBuf *dest, const UInt16 *src, size_t srcLen)
{
  size_t destLen = 0;
  Bool res;
  Utf16_To_Utf8(NULL, &destLen, src, srcLen);
  destLen += 1;
  if (!Buf_EnsureSize(dest, destLen))
    return SZ_ERROR_MEM;
  res = Utf16_To_Utf8(dest->data, &destLen, src, srcLen);
  dest->data[destLen] = 0;
  return res ? SZ_OK : SZ_ERROR_FAIL;
}
#endif

/* convertion of the widechar strings to the regular */
static WRes Utf16_To_Char(CBuf *buf, const UInt16 *s, int fileMode)
{
  int len = 0;
  for (len = 0; s[len] != '\0'; len++);

  #ifdef _WIN32
  {
    int size = len * 3 + 100;
    if (!Buf_EnsureSize(buf, size))
      return SZ_ERROR_MEM;
    {
      char defaultChar = '_';
      BOOL defUsed;
      int numChars = WideCharToMultiByte(fileMode ? (AreFileApisANSI() ? CP_ACP : CP_OEMCP) : CP_OEMCP,
          0, s, len, (char *)buf->data, size, &defaultChar, &defUsed);
      if (numChars == 0 || numChars >= size)
        return SZ_ERROR_FAIL;
      buf->data[numChars] = 0;
      return SZ_OK;
    }
  }
  #else
  fileMode = fileMode;
  return Utf16_To_Utf8Buf(buf, s, len);
  #endif
}

/* creating directory with widechar 'name' */
static WRes MyCreateDir(const UInt16 *name)
{
  #ifdef USE_WINDOWS_FILE
  
  return CreateDirectoryW(name, NULL) ? 0 : GetLastError();
  
  #else

  CBuf buf;
  WRes res;
  Buf_Init(&buf);
  RINOK(Utf16_To_Char(&buf, name, 1));

  res =
  #ifdef _WIN32
  _mkdir((const char *)buf.data)
  #else
  mkdir((const char *)buf.data, 0777)
  #endif
  == 0 ? 0 : errno;
  Buf_Free(&buf, &g_Alloc);
  return res;
  
  #endif
}

/* Openining file with widechar 'name' */
static WRes OutFile_OpenUtf16(CSzFile *p, const UInt16 *name)
{
  #ifdef USE_WINDOWS_FILE
  return OutFile_OpenW(p, name);
  #else
  CBuf buf;
  WRes res;
  Buf_Init(&buf);
  RINOK(Utf16_To_Char(&buf, name, 1));
  res = OutFile_Open(p, (const char *)buf.data);
  Buf_Free(&buf, &g_Alloc);
  return res;
  #endif
}

/* Print widechar string 's' to the console */
static void PrintString(const UInt16 *s)
{
  CBuf buf;
  Buf_Init(&buf);
  if (Utf16_To_Char(&buf, s, 0) == 0)
  {
    printf("%s", buf.data);
    Buf_Free(&buf, &g_Alloc);
  }
}

/* widechar 'utf' string comparison with the regular 's' */
static int CompareUtf16_String(const UInt16 *utf, const char* s)
{
  CBuf buf;
  int res = 1;
  Buf_Init(&buf);
  if (Utf16_To_Char(&buf, utf, 0) == 0)
  {
    res = strcmp(buf.data, s);
    Buf_Free(&buf, &g_Alloc);
  }
  return res;
}

/* Print 'archiveFile' archive content */
SRes List7zFiles(char* archiveFile) {
  CFileInStream archiveStream;
  CLookToRead lookStream;
  CSzArEx db;
  SRes res;
  ISzAlloc allocImp;
  ISzAlloc allocTempImp;
  UInt16 *temp = NULL;
  size_t tempSize = 0;

  allocImp.Alloc = SzAlloc;
  allocImp.Free = SzFree;

  allocTempImp.Alloc = SzAllocTemp;
  allocTempImp.Free = SzFreeTemp;

  /* открыть файл архива */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  printf("Contents of archive %s:\n\n", archiveFile);
 
  /* Initializing compressed stream, which is reading from the file in that case */
  FileInStream_CreateVTable(&archiveStream);
  /* specifying data access method */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* zeroing reading pointer's position */
  LookToRead_Init(&lookStream);

  /* initializing archive's structure */
  SzArEx_Init(&db);
  /* opening archive & filling 'db' structure */
  res = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
  if (res == SZ_OK)
  {
    UInt32 i;

    /* running through files in archive */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      const CSzFileItem *f = db.db.Files + i;
      size_t len;
      /* memory block size storing file name string */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* allocate additional memory, if that was not enough */
      if (len > tempSize)
      {
        SzFree(NULL, temp);
        tempSize = len;
        temp = (UInt16 *)SzAlloc(NULL, tempSize * sizeof(temp[0]));
        if (temp == 0)
        {
          res = SZ_ERROR_MEM;
          break;
        }
      }
      /* getting file name by index */
      SzArEx_GetFileNameUtf16(&db, i, temp);
      /* printing file name */
      PrintString(temp);
      printf("\n");
    }
  }
  /* freeing memory allocated for the job earlier */
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, temp);
  /* closing file archive */
  File_Close(&archiveStream.file);
  return res;
}


/* Extract 'fileName' from 'archiveFile' */
SRes Decode7zOneFile(char* archiveFile, char* fileName) {
  CFileInStream archiveStream;
  CLookToRead lookStream;
  CSzArEx db;
  SRes res;
  ISzAlloc allocImp;
  ISzAlloc allocTempImp;
  UInt16 *name = NULL;
  size_t nameSize = 0;

  allocImp.Alloc = SzAlloc;
  allocImp.Free = SzFree;

  allocTempImp.Alloc = SzAllocTemp;
  allocTempImp.Free = SzFreeTemp;

  /* opening archive file */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  /* initializing compressed stream - reading from the file in that case */
  FileInStream_CreateVTable(&archiveStream);
  /* specifying data access method */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* reseting reading pointer's position */
  LookToRead_Init(&lookStream);

  /* initializing archive structure */
  SzArEx_Init(&db);
  /* opening archive & filling 'db' structure */
  res = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
  if (res == SZ_OK)
  {
    UInt32 i;
    /*
    if you need cache, use these 3 variables.
    if you use external function, you can make these variable as static.
    */
    UInt32 blockIndex = 0xFFFFFFFF; /* it can have any value before first call (if outBuffer = 0) */
    Byte *outBuffer = 0; /* it must be 0 before first call for each new archive. */
    size_t outBufferSize = 0;  /* it can have any value before first call (if outBuffer = 0) */

    /* running through all of the files in archive */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      size_t offset = 0;
      size_t outSizeProcessed = 0;
      const CSzFileItem *f = db.db.Files + i;
      CSzFile outFile;
      size_t len, j, processedSize;
      UInt16 *destPath;

      /* skipping directories */
      if (f->IsDir)
        continue;
      /* memory block size storing file name string */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* allocate additional memory, if that was not enough */
      if (len > nameSize)
      {
        SzFree(NULL, name);
        nameSize = len;
        name = (UInt16 *)SzAlloc(NULL, nameSize * sizeof(name[0]));
        if (name == 0)
        {
          res = SZ_ERROR_MEM;
          break;
        }
      }
      /* getting file name by index */
      SzArEx_GetFileNameUtf16(&db, i, name);
      destPath = name;
      /* generating file name without sub-directories */
      for (j = 0; name[j] != 0; j++)
        if (name[j] == '/')
          destPath = name + j + 1;
      /* compare current file name with required */
      if (CompareUtf16_String(destPath, fileName) != 0)
        continue;
      /* unpacking to the temporary buffer */
      res = SzArEx_Extract(&db, &lookStream.s, i,
          &blockIndex, &outBuffer, &outBufferSize,
          &offset, &outSizeProcessed,
          &allocImp, &allocTempImp);
      if (res != SZ_OK)
        break;
      /* opening for writing */
      if (OutFile_OpenUtf16(&outFile, destPath))
      {
        printf("\nERROR: can not open output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      processedSize = outSizeProcessed;
      /* writing temporary (unpacked file) buffer to the file */
      if (File_Write(&outFile, outBuffer + offset, &processedSize) != 0 || processedSize != outSizeProcessed)
      {
        printf("\nERROR: can not write output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      /* closing file handler */
      if (File_Close(&outFile))
      {
        printf("\nERROR: can not close output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      /* setting up file's attributes (Windows only) */
      #ifdef USE_WINDOWS_FILE
      if (f->AttribDefined)
        SetFileAttributesW(destPath, f->Attrib);
      #endif

    }
    /* freeing memory allocated for the job earlier */
    IAlloc_Free(&allocImp, outBuffer);
  }
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, name);
  /* closing file archive */
  File_Close(&archiveStream.file);
  return res;
}


/* Extract archive 'archiveFile'
  if used with 'fullPaths==1' - it will keep directories structure */ 
SRes Decode7zFiles(char* archiveFile, int fullPaths) {
  CFileInStream archiveStream;
  CLookToRead lookStream;
  CSzArEx db;
  SRes res;
  ISzAlloc allocImp;
  ISzAlloc allocTempImp;
  UInt16 *name = NULL;
  size_t nameSize = 0;

  allocImp.Alloc = SzAlloc;
  allocImp.Free = SzFree;

  allocTempImp.Alloc = SzAllocTemp;
  allocTempImp.Free = SzFreeTemp;

  /* opening archive file */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  /* initializing compressed stream - reading from the file in that case */
  FileInStream_CreateVTable(&archiveStream);
  /* specifying data access method */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* reseting reading pointer's position */
  LookToRead_Init(&lookStream);

  /* initializing archive structure */
  SzArEx_Init(&db);
  /* opening archive & filling 'db' structure */
  res = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
  if (res == SZ_OK)
  {
    UInt32 i;
    /*
    if you need cache, use these 3 variables.
    if you use external function, you can make these variable as static.
    */
    UInt32 blockIndex = 0xFFFFFFFF; /* it can have any value before first call (if outBuffer = 0) */
    Byte *outBuffer = 0; /* it must be 0 before first call for each new archive. */
    size_t outBufferSize = 0;  /* it can have any value before first call (if outBuffer = 0) */

    /* running through all of the files in archive */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      size_t offset = 0;
      size_t outSizeProcessed = 0;
      const CSzFileItem *f = db.db.Files + i;
      size_t len;

      /* skipping, in case if that is the catalog and directories structure is not required */
      if (f->IsDir && !fullPaths)
        continue;
      /* memory block size storing file name string */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* allocate additional memory, if that was not enough */
      if (len > nameSize)
      {
        SzFree(NULL, name);
        nameSize = len;
        name = (UInt16 *)SzAlloc(NULL, nameSize * sizeof(name[0]));
        if (name == 0)
        {
          res = SZ_ERROR_MEM;
          break;
        }
      }
      /* getting file name by index */
      SzArEx_GetFileNameUtf16(&db, i, name);
      /* in case if it is not a directory, unpacking file to the buffer */
      if (!(f->IsDir))
      {
        res = SzArEx_Extract(&db, &lookStream.s, i,
            &blockIndex, &outBuffer, &outBufferSize,
            &offset, &outSizeProcessed,
            &allocImp, &allocTempImp);
        if (res != SZ_OK)
          break;
      }

      {
        CSzFile outFile;
        size_t processedSize, j;
        UInt16 *destPath = name;
        /* generating file name with sub-directories */
        for (j = 0; name[j] != 0; j++)
          if (name[j] == '/')
          {
            if (fullPaths)
            {
              name[j] = 0;
              MyCreateDir(name);
              name[j] = CHAR_PATH_SEPARATOR;
            }
            else
              destPath = name + j + 1;
          }
        /* in case that is a directory, creating it */
        if (f->IsDir)
        {
          MyCreateDir(destPath);
          continue;
        }

        else if (OutFile_OpenUtf16(&outFile, destPath))
        {
          printf("\nERROR: can not open output file");
          res = SZ_ERROR_FAIL;
          break;
        }
        processedSize = outSizeProcessed;
        /* writing temporary (unpacked file) buffer to the file */
        if (File_Write(&outFile, outBuffer + offset, &processedSize) != 0 || processedSize != outSizeProcessed)
        {
          printf("\nERROR: can not write output file");
          res = SZ_ERROR_FAIL;
          break;
        }
         /* closing file handler */
        if (File_Close(&outFile))
        {
          printf("\nERROR: can not close output file");
          res = SZ_ERROR_FAIL;
          break;
        }
        /* setting up file's attributes (Windows only) */
        #ifdef USE_WINDOWS_FILE
        if (f->AttribDefined)
          SetFileAttributesW(destPath, f->Attrib);
        #endif
      }

    }
    /* freeing memory allocated for the job earlier */
    IAlloc_Free(&allocImp, outBuffer);
  }
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, name);
  /* closing file archive */
  File_Close(&archiveStream.file);
  return res;
}
