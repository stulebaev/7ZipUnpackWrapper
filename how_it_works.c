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

/*
How to build:

1. cd liblzma (latest liblzma snapshot, at the moment of writing that strings)

2. make

3. gcc ../how_it_works.c *.o -o../how_it_works

4. Ppmd.h, Ppmd7.c, Ppmd7.h, Ppmd7Dec.c files are not required if -D_7ZIP_PPMD_SUPPPORT option is not in place for 7zDec.c file compilation.

5. '-D_SZ_ALLOC_DEBUG' option for 7zAlloc.c file is an option to troubelshoot memory problems:
7zAlloc.o: 7zAlloc.c
 $(CC) $(CFLAGS) -D_SZ_ALLOC_DEBUG 7zAlloc.c

6. Files required from the library (with PPMD support):
7zAlloc.c 7zCrc.c 7zCrcOpt.c CpuArch.c 7zFile.c 7zStream.c 7zIn.c 7zBuf.c 7zDec.c LzmaDec.c Lzma2Dec.c Bra86.c Bcj2.c Ppmd7.c Ppmd7Dec.c
 
*/

#include <stdio.h>
#include "liblzma.h"

int main(void) {
  int res;

  CrcGenerateTable();

  /* Shows content of the archiveFile */
  //res = List7zFiles("Output.7z");
  //if (res != SZ_OK)
  //  goto error_occasion;

  /* Extract fileName from archiveFile */ 
  //res = Decode7zOneFile("Output.7z", "Ruta-67c1a60f1cf45ff01ac5b58b6d1baef1.html");
  //if (res != SZ_OK)
  //  goto error_occasion;

  /* Extract archiveFile, in case with fullPaths==1 - keep sub-directories structure. */   
  res = Decode7zFiles("Output.7z", 1);
  if (res != SZ_OK)
    goto error_occasion;
  
  return 0;

error_occasion:
  printf("\nERROR #%d", res);
  switch (res) {
    case SZ_ERROR_UNSUPPORTED:
      printf(": decoder doesn't support this archive");
      break;
    case SZ_ERROR_MEM:
      printf(": can not allocate memory");
      break;
    case SZ_ERROR_CRC:
      printf(": CRC error");
  }
  printf("\n");

  return 1;
}
