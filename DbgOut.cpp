/*
 *  Part of the foobar2000 Matroska plugin
 *
 *  Copyright (C) Jory Stone - 2003
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#include "DbgOut.h"

#ifdef DEBUG_OUT_PUT
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

static DWORD g_dwTimeOffset = timeGetTime();

void WINAPI DbgStringOut(const TCHAR *pFormat,...)
{    	
  TCHAR szInfo[2000];

  // Format the variable length parameter list
  va_list va;
  va_start(va, pFormat);

  lstrcpy(szInfo, TEXT("foo_input_matroska - "));
  wsprintf(szInfo + lstrlen(szInfo),
            TEXT("(tid %x) %8d : "),
            GetCurrentThreadId(), timeGetTime() - g_dwTimeOffset);

  _vstprintf(szInfo + lstrlen(szInfo), pFormat, va);
  lstrcat(szInfo, TEXT("\r\n"));
  OutputDebugString(szInfo);

  va_end(va);
};

#ifdef UNICODE
void WINAPI DbgLogInfo(const CHAR *pFormat,...)
{
    TCHAR szInfo[2000];

    // Format the variable length parameter list
    va_list va;
    va_start(va, pFormat);

    lstrcpy(szInfo, TEXT("foo_input_matroska - "));
    wsprintf(szInfo + lstrlen(szInfo),
             TEXT("(tid %x) %8d : "),
             GetCurrentThreadId(), timeGetTime() - g_dwTimeOffset);

    CHAR szInfoA[2000];
    WideCharToMultiByte(CP_ACP, 0, szInfo, -1, szInfoA, sizeof(szInfoA)/sizeof(CHAR), 0, 0);

    wvsprintfA(szInfoA + lstrlenA(szInfoA), pFormat, va);
    lstrcatA(szInfoA, "\r\n");

    WCHAR wszOutString[2000];
    MultiByteToWideChar(CP_ACP, 0, szInfoA, -1, wszOutString, sizeof(wszOutString)/sizeof(WCHAR));
    DbgOutString(wszOutString);

    va_end(va);
}
#endif
#endif

