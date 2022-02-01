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

/* Âûäåëåíèå áëîêà äèíàìè÷åñêîé ïàìÿòè çàäàííîãî ðàçìåðà size */
/*
 Â áèáëèîòåêå LZMA èñïîëüçóåòñÿ ñâîé äèñïåò÷åð äèíàìè÷åñêîé ïàìÿòè.
 Ðàçìåðû áëîêîâ ïàìÿòè, âûäåëÿåìûõ âïåðâûå, íåñêîëüêî áîëüøå, ÷åì çàòðåáîâàíî. 
 È åñëè ïðè íîâîì îáðàùåíèè çàïðàøèâàåìûé ðàçìåð ìåíüøå çàðàíåå âûäåëåííîãî,
 òî íîâîãî âûäåëåíèÿ ïàìÿòè ôèçè÷åñêè íå ïðîèñõîäèò.
*/
static int Buf_EnsureSize(CBuf *dest, size_t size)
{
  if (dest->size >= size)
    return 1;
  Buf_Free(dest, &g_Alloc);
  return Buf_Create(dest, size, &g_Alloc);
}

/* Äàëåå èäåò êîä ïîääåðæêè widechar-ñòðîê äëÿ nonWindows ñèñòåì */
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

/* Ïðåîáðàçîâàíèå widechar-ñòðîê â îáû÷íûå ñòðîêè */
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

/* Ñîçäàíèå êàòàëîãà ñ widechar-èìåíåì name */
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

/* Îòêðûòü ôàéë ñ widechar-èìåíåì name */
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

/* Âûâåñòè íà êîíñîëü widechar-ñòðîêó s */
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

/* Ñðàâíèòü widechar-ñòðîêó utf ñ îáû÷íîé ñòðîêîé s */
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

/* Ïîêàçàòü ñîäåðæèìîå àðõèâà archiveFile */
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

  /* îòêðûòü ôàéë àðõèâà */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  printf("Contents of archive %s:\n\n", archiveFile);
 
  /* èíèöèàëèçèðóåì ïîòîê ñæàòûõ äàííûõ -- â äàííîì ñëó÷àå ÷èòàåì èç ôàéëà */
  FileInStream_CreateVTable(&archiveStream);
  /* óêàçûâàåì íà ñïîñîá äîñòóïà ê äàííûì */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* óñòàíàâëèâàåì ïîçèöèþ ÷òåíèÿ íà íóëü */
  LookToRead_Init(&lookStream);

  /* èíèöèàëèçèðóåì ñòðóêòóðó, ñâÿçàííóþ ñ àðõèâîì */
  SzArEx_Init(&db);
  /* îòêðûâàåì àðõèâ è çàïîëíÿåì ñòðóêòóðó db */
  res = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
  if (res == SZ_OK)
  {
    UInt32 i;

    /* öèêë ïî âñåì ôàéëàì â àðõèâå */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      const CSzFileItem *f = db.db.Files + i;
      size_t len;
      /* óçíàåì ðàçìåð áëîêà ïàìÿòè ïîä ñòðîêó ñ èìåíåì ôàéëà */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* åñëè íåäîñòàòî÷íî -- âûäåëÿåì áîëüøå ïàìÿòè */
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
      /* ïîëó÷àåì èìÿ ôàéëà ïî èíäåêñó */
      SzArEx_GetFileNameUtf16(&db, i, temp);
      /* âûâîäèì èìÿ ôàéëà */
      PrintString(temp);
      printf("\n");
    }
  }
  /* îñâîáîæäàåì çàäåéñòâîâàííóþ äëÿ ðàáîòû äèíàìè÷åñêóþ ïàìÿòü */
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, temp);
  /* çàêðûâàåì ôàéë àðõèâà */
  File_Close(&archiveStream.file);
  return res;
}


/* Ðàñïàêîâàòü ôàéë fileName èç àðõèâà archiveFile */
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

  /* îòêðûòü ôàéë àðõèâà */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  /* èíèöèàëèçèðóåì ïîòîê ñæàòûõ äàííûõ -- â äàííîì ñëó÷àå ÷èòàåì èç ôàéëà */
  FileInStream_CreateVTable(&archiveStream);
  /* óêàçûâàåì íà ñïîñîá äîñòóïà ê äàííûì */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* óñòàíàâëèâàåì ïîçèöèþ ÷òåíèÿ íà íóëü */
  LookToRead_Init(&lookStream);

  /* èíèöèàëèçèðóåì ñòðóêòóðó, ñâÿçàííóþ ñ àðõèâîì */
  SzArEx_Init(&db);
  /* îòêðûâàåì àðõèâ è çàïîëíÿåì ñòðóêòóðó db */
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

    /* öèêë ïî âñåì ôàéëàì â àðõèâå */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      size_t offset = 0;
      size_t outSizeProcessed = 0;
      const CSzFileItem *f = db.db.Files + i;
      CSzFile outFile;
      size_t len, j, processedSize;
      const UInt16 *destPath = (const UInt16 *)name;

      /* åñëè ýòî êàòàëîã -- ïðîïóñêàåì */
      if (f->IsDir)
        continue;
      /* óçíàåì ðàçìåð áëîêà ïàìÿòè ïîä ñòðîêó ñ èìåíåì ôàéëà */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* åñëè íåäîñòàòî÷íî -- âûäåëÿåì áîëüøå ïàìÿòè */
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
      /* ïîëó÷àåì èìÿ ôàéëà ïî èíäåêñó */
      SzArEx_GetFileNameUtf16(&db, i, name);
      /* ôîðìèðóåì èìÿ ôàéëà áåç ïîäêàòàëîãîâ */
      for (j = 0; name[j] != 0; j++)
        if (name[j] == '/')
          destPath = name + j + 1;
      /* ñðàâíèâàåì èìÿ òåêóùåãî ôàéëà ñ çàäàííûì */
      if (CompareUtf16_String(destPath, fileName) != 0)
        continue;
      /* ðàñïàêîâûâàåì ôàéë âî âðåìåííûé áóôåð */
      res = SzArEx_Extract(&db, &lookStream.s, i,
          &blockIndex, &outBuffer, &outBufferSize,
          &offset, &outSizeProcessed,
          &allocImp, &allocTempImp);
      if (res != SZ_OK)
        //printf("nameSize = %d\n", nameSize);
        //printf("i=%d\n", i);
        printf("i: %d, outBufferSize: %ld, offset: %ld, outSizeProcessed: %ld\n", i, outBufferSize, offset, outSizeProcessed);
        break;
      /* îòêðûâàåì ôàéë äëÿ çàïèñè */
      if (OutFile_OpenUtf16(&outFile, destPath))
      {
        printf("\nERROR: can not open output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      processedSize = outSizeProcessed;
      /* çàïèñûâàåì ðàñïàêîâàííûé áóôåð â ôàéë */
      if (File_Write(&outFile, outBuffer + offset, &processedSize) != 0 || processedSize != outSizeProcessed)
      {
        printf("\nERROR: can not write output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      /* çàêðûâàåì ôàéë */
      if (File_Close(&outFile))
      {
        printf("\nERROR: can not close output file");
        res = SZ_ERROR_FAIL;
        break;
      }
      /* åñëè ïîä Windows, òî óñòàíàâëèâàåì àòðèáóòû ôàéëà */
      #ifdef USE_WINDOWS_FILE
      if (f->AttribDefined)
        SetFileAttributesW(destPath, f->Attrib);
      #endif

    }
    /* îñâîáîæäàåì çàäåéñòâîâàííóþ äëÿ ðàáîòû äèíàìè÷åñêóþ ïàìÿòü */
    IAlloc_Free(&allocImp, outBuffer);
  }
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, name);
  /* çàêðûâàåì ôàéë àðõèâà */
  File_Close(&archiveStream.file);
  
  return res;
}


/* Ðàñïàêîâàòü àðõèâ archiveFile
 ïðè fullPaths==1  ñ ñîõðàíåíèåì ñòðóêòóðû ïîäêàòàëîãîâ  */ 
SRes Decode7zFiles(char* archiveFile, int fullPaths) {
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

  /* îòêðûòü ôàéë àðõèâà */
  if (InFile_Open(&archiveStream.file, archiveFile))
  {
    printf("\nERROR: can not open input file");
    return SZ_ERROR_FAIL;
  }

  /* èíèöèàëèçèðóåì ïîòîê ñæàòûõ äàííûõ -- â äàííîì ñëó÷àå ÷èòàåì èç ôàéëà */
  FileInStream_CreateVTable(&archiveStream);
  /* óêàçûâàåì íà ñïîñîá äîñòóïà ê äàííûì */
  LookToRead_CreateVTable(&lookStream, False);
  
  lookStream.realStream = &archiveStream.s;
  /* óñòàíàâëèâàåì ïîçèöèþ ÷òåíèÿ íà íóëü */
  LookToRead_Init(&lookStream);

  /* èíèöèàëèçèðóåì ñòðóêòóðó, ñâÿçàííóþ ñ àðõèâîì */
  SzArEx_Init(&db);
  /* îòêðûâàåì àðõèâ è çàïîëíÿåì ñòðóêòóðó db */
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

    /* öèêë ïî âñåì ôàéëàì â àðõèâå */
    for (i = 0; i < db.db.NumFiles; i++)
    {
      size_t offset = 0;
      size_t outSizeProcessed = 0;
      const CSzFileItem *f = db.db.Files + i;
      size_t len;

      /* åñëè ýòî êàòàëîã è íå íóæíî âîññîçäàâàòü ñòðóêòóðó ïîäêàòàëîãîâ
         -- ïðîïóñêàåì */
      if (f->IsDir && !fullPaths)
        continue;
      /* óçíàåì ðàçìåð áëîêà ïàìÿòè ïîä ñòðîêó ñ èìåíåì ôàéëà */
      len = SzArEx_GetFileNameUtf16(&db, i, NULL);
      /* åñëè íåäîñòàòî÷íî -- âûäåëÿåì áîëüøå ïàìÿòè */
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
      /* ïîëó÷àåì èìÿ ôàéëà ïî èíäåêñó */
      SzArEx_GetFileNameUtf16(&db, i, temp);
      /* åñëè ýòî íå êàòàëîã, òî ðàñïàêîâûâàåì ôàéë â áóôåð */
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
        UInt16 *name = (UInt16 *)temp;
        const UInt16 *destPath = (const UInt16 *)name;
        /* ôîðìèðóåì ïîëíîå èìÿ ôàéëà (ñ ó÷åòîì ïîäêàòàëîãîâ) */
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
        /* åñëè ýòî êàòàëîã, òî ñîçäàåì åãî */
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
        /* çàïèñûâàåì ðàñïàêîâàííûé áóôåð â ôàéë */
        if (File_Write(&outFile, outBuffer + offset, &processedSize) != 0 || processedSize != outSizeProcessed)
        {
          printf("\nERROR: can not write output file");
          res = SZ_ERROR_FAIL;
          break;
        }
        /* çàêðûâàåì ôàéë */
        if (File_Close(&outFile))
        {
          printf("\nERROR: can not close output file");
          res = SZ_ERROR_FAIL;
          break;
        }
        /* åñëè ïîä Windows, òî óñòàíàâëèâàåì àòðèáóòû ôàéëà */
        #ifdef USE_WINDOWS_FILE
        if (f->AttribDefined)
          SetFileAttributesW(destPath, f->Attrib);
        #endif
      }

    }
    /* îñâîáîæäàåì çàäåéñòâîâàííóþ äëÿ ðàáîòû äèíàìè÷åñêóþ ïàìÿòü */
    IAlloc_Free(&allocImp, outBuffer);
  }
  SzArEx_Free(&db, &allocImp);
  SzFree(NULL, temp);
  /* çàêðûâàåì ôàéë àðõèâà */
  File_Close(&archiveStream.file);
  return res;
}
