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

#ifndef _LIBLZMA_H
#define _LIBLZMA_H

#define SZ_OK 0
#define SZ_ERROR_DATA 1
#define SZ_ERROR_MEM 2
#define SZ_ERROR_CRC 3
#define SZ_ERROR_UNSUPPORTED 4
#define SZ_ERROR_PARAM 5
#define SZ_ERROR_INPUT_EOF 6
#define SZ_ERROR_OUTPUT_EOF 7
#define SZ_ERROR_READ 8
#define SZ_ERROR_WRITE 9
#define SZ_ERROR_PROGRESS 10
#define SZ_ERROR_FAIL 11
#define SZ_ERROR_THREAD 12
#define SZ_ERROR_ARCHIVE 16
#define SZ_ERROR_NO_ARCHIVE 17

/* Call CrcGenerateTable one time before other CRC functions */
void CrcGenerateTable(void);

int List7zFiles(char* archiveFile);
int Decode7zOneFile(char* archiveFile, char* fileName);
int Decode7zFiles(char* archiveFile, int fullPaths);

#endif
