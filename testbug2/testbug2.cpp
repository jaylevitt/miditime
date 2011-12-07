// testbug2.cpp : Defines the entry point for the console application.

#include "stdafx.h"
#include <comdef.h>
#include <conio.h>
#include <ctype.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>
#include "wbemidl.h"

#include <time.h>
#include <sys/timeb.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <streambuf>

int _tmain(int argc, _TCHAR* argv[])
{
using namespace std;

	// TGT (timeGetTime) time is in milliseconds, like midi time
DWORD tgt_time_started = 0;
DWORD tgt_time_sent = 0;

// QPC (QueryPerformanceCounter) time is in "counts"
LARGE_INTEGER qpc_time_started;
LARGE_INTEGER qpc_time_sent;
LARGE_INTEGER qpc_counts_per_sec;

tgt_time_started = 360840;
tgt_time_sent = 19616273;
qpc_time_started.QuadPart = 1291078205;
qpc_time_sent.QuadPart = 70216778171;
qpc_counts_per_sec.QuadPart = 3579545;

	INT64 tgt_duration_in_usec		= (INT64)(tgt_time_sent - tgt_time_started) * 1000I64;
    INT64 qpc_duration_in_counts	= qpc_time_sent.QuadPart - qpc_time_started.QuadPart; 
	INT64 qpc_duration_in_usec		= (qpc_duration_in_counts * 1000000I64)/qpc_counts_per_sec.QuadPart; 
	INT64 qpc_tgt_delta				= qpc_duration_in_usec - (INT64)tgt_duration_in_usec; 

	cout << qpc_tgt_delta;

	return 0;
}

