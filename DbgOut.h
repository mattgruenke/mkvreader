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
 */

#ifndef _DBG_OUT_H_
#define _DBG_OUT_H_

#ifndef STRICT
#define STRICT
#endif

#ifdef _DEBUG
//#define DEBUG_OUT_PUT
#include <tchar.h>
#include <stdarg.h>
#include <fstream>
#include <string>
#include <locale>
namespace win32
{
    typedef std::basic_string< _TCHAR > string;
    typedef std::basic_fstream< _TCHAR > fstream;
}
#define HPRINTF_MES_LENGTH 256
inline void hprintf( const _TCHAR * format, ... )
{
    _TCHAR buff[HPRINTF_MES_LENGTH];
    va_list vl;
    va_start(vl, format);
    _vsntprintf(buff, HPRINTF_MES_LENGTH, format, vl);
    va_end(vl);
    OutputDebugString(buff);
    //*
    win32::string basicstring(buff);
    win32::fstream f("foo_input_matroska.debug.txt", std::ios::out|std::ios::app);
    f << basicstring.c_str();
	f.close();
    //*/
}
#include <boost/timer.hpp>
#define TIMER boost::timer t
#define RETIMER t.restart()
#define _TIMER(x) hprintf(L"%S: %f sec.\n",x,t.elapsed())
#else
#define TIMER __noop
#define RETIMER __noop
#define _TIMER(x) __noop
#define hprintf __noop
#endif

#ifdef DEBUG_OUT_PUT
void WINAPI DbgStringOut(const TCHAR *pFormat,...);
#ifdef UNICODE
void WINAPI DbgLogInfo(const CHAR *pFormat,...)
#endif
#define NOTE(x) DbgStringOut(x)
#define NOTE1(x, y1) DbgStringOut(x, y1)
#define NOTE2(x, y1, y2) DbgStringOut(x, y1, y2)
#define NOTE3(x, y1, y2, y3) DbgStringOut(x, y1, y2, y3)
#define NOTE4(x, y1, y2, y3, y4) DbgStringOut(x, y1, y2, y3, y4)
#define NOTE5(x, y1, y2, y3, y4, y5) DbgStringOut(x, y1, y2, y3, y4, y5)
#define NOTE6(x, y1, y2, y3, y4, y5, y6) DbgStringOut(x, y1, y2, y3, y4, y5, y6)
#define NOTE10(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10) DbgStringOut(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10)
#define ODS(x) DbgStringOut(x)
#define ODS1(x, y1) DbgStringOut(x, y1)
#define ODS2(x, y1, y2) DbgStringOut(x, y1, y2)
#define ODS3(x, y1, y2, y3) DbgStringOut(x, y1, y2, y3)
#define ODS4(x, y1, y2, y3, y4) DbgStringOut(x, y1, y2, y3, y4)
#define ODS5(x, y1, y2, y3, y4, y5) DbgStringOut(x, y1, y2, y3, y4, y5)
#define ODS6(x, y1, y2, y3, y4, y5, y6) DbgStringOut(x, y1, y2, y3, y4, y5, y6)
#define ODS10(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10) DbgStringOut(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10)
#else
#define NOTE(x)
#define NOTE1(x, y1)
#define NOTE2(x, y1, y2)
#define NOTE3(x, y1, y2, y3)
#define NOTE4(x, y1, y2, y3, y4)
#define NOTE5(x, y1, y2, y3, y4, y5)
#define NOTE6(x, y1, y2, y3, y4, y5, y6)
#define NOTE10(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10)
#define ODS(x)
#define ODS1(x, y1)
#define ODS2(x, y1, y2)
#define ODS3(x, y1, y2, y3)
#define ODS4(x, y1, y2, y3, y4)
#define ODS5(x, y1, y2, y3, y4, y5)
#define ODS6(x, y1, y2, y3, y4, y5, y6)
#define ODS10(x, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10)
#endif


#endif // _DBG_OUT_H_
