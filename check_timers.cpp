// Timer tests

#include "stdafx.h"
#include <windows.h>
#include <time.h>
#include <sys/timeb.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

using namespace std;

#pragma warning(disable:4100) // disable unreferenced formal parameter (argc,argv)

void show_error(string description) {
	DWORD code;
	LPVOID lpMsgBuf;

	code = GetLastError();

	if (! FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | 
			FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf, 0, NULL)) {
				cerr << "Unknown error #" << code << endl;
				return;
			}
		
	cerr << "Error # " << code << ": " << lpMsgBuf << 
		". " << description << endl;
	return;

}


string format_time (struct tm *time) {
	ostringstream sTime;

	sTime << setfill('0') << 
		setw(2) << time->tm_hour << ":" <<
		setw(2) << time->tm_min << ":" <<
		setw(2) << time->tm_sec;
	return (sTime.str());
}


int main (int argc, char **argv) {
    // Three types of time here:
	// 1. time.h (display only)
    struct _timeb tbNow;
    struct tm *tmNow;

	// 2. timeGetTime() 
    DWORD tgt_initial;
	DWORD tgt_baseline;
    DWORD tgt_current;
	INT32 function_slop_msec;
	INT32 tgt_duration_usec;
    INT32 tgt_cum_duration_usec;
	const UINT TGT_RESOLUTION = 1;
	MMRESULT tgt_err;

	// 3. QueryPerformanceCounter()
	LARGE_INTEGER qpc_frequency;
    LARGE_INTEGER qpc_initial;
	LARGE_INTEGER qpc_baseline;
	LARGE_INTEGER qpc_current;
    INT64 qpc_duration_counts;
    INT64 qpc_cum_duration_counts;
	INT64 qpc_duration_usec;
	INT64 qpc_cum_duration_usec;

	INT64 qpc_tgt_delta;
	INT64 qpc_tgt_cum_delta;
   
	// Ask windows for 1ms resolution
	tgt_err = timeBeginPeriod(TGT_RESOLUTION);
	if (tgt_err != TIMERR_NOERROR) {
		cerr << "timeBeginPeriod returned " << tgt_err << endl;
		abort();
	}

	// QPC is in "counts".  Let's find out what our count frequency is
	qpc_frequency.HighPart = 0;
    qpc_frequency.LowPart = 0;
	if (! QueryPerformanceFrequency(&qpc_frequency)) {
		show_error("during QueryPerformanceFrequency");
		abort();
	}
	
	// Initialize both of our timers 
    tgt_initial = timeGetTime();
	if (! QueryPerformanceCounter(&qpc_initial)) {
		show_error("during QueryPerformanceCounter");
		abort();
	}

	// If the time between those two functions is > 1 ms, don't blame Windows later
	function_slop_msec = timeGetTime() - tgt_initial;

	tgt_baseline = tgt_initial;
	qpc_baseline = qpc_initial;

	_ftime(&tbNow);
    tmNow = localtime(&tbNow.time);
	cout << endl << format_time(tmNow) << ": run started";
	if (function_slop_msec != 0) {
		cout << " (slop: " << function_slop_msec << " ms)";
	}
	
	for(;;) {
            Sleep(10000); // ~ 10 seconds

            _ftime(&tbNow);
            tmNow = localtime(&tbNow.time);

            // should be very small delta between calls
            tgt_current = timeGetTime();
			if (! QueryPerformanceCounter(&qpc_current)) {
				show_error ("during QueryPerformanceCounter");
				abort();
			}

            // calculate elapsed times (broken apart for debugger viewing)
            tgt_cum_duration_usec   = (tgt_current - tgt_initial) * 1000;
			tgt_duration_usec       = (tgt_current - tgt_baseline) * 1000;
			qpc_cum_duration_counts = qpc_current.QuadPart - qpc_initial.QuadPart; 
            qpc_duration_counts		= qpc_current.QuadPart - qpc_baseline.QuadPart; 
            qpc_cum_duration_usec	= (qpc_cum_duration_counts * 1000000I64)/qpc_frequency.QuadPart; 
            qpc_duration_usec		= (qpc_duration_counts * 1000000I64)/qpc_frequency.QuadPart; 

            qpc_tgt_delta = qpc_duration_usec - (INT64)tgt_duration_usec; 
			qpc_tgt_cum_delta = qpc_cum_duration_usec - (INT64)tgt_cum_duration_usec;

			// 1ms is minimum TGT resolution, so allow that much slop, plus however long it takes to 
			// actually check the timers

			if (_abs64(qpc_tgt_delta) < (function_slop_msec + 1) * 1000) {
				cout << ".";
			}
			else {
				cout << endl <<	format_time(tmNow) << ": delta " << 
					fixed << setprecision(3) << (float)qpc_tgt_delta/1000 <<
					" ms (cumulative " << (float)qpc_tgt_cum_delta/1000 << " ms)";			
					
				// Once we've found a delta, reset the counters so we only report new discrepancies 
				qpc_baseline = qpc_current;
				tgt_baseline = tgt_current;

			}
			
	}
 
	tgt_err = timeEndPeriod(TGT_RESOLUTION);
	if (tgt_err != TIMERR_NOERROR) {
	 cout << "timeEndPeriod returned " << tgt_err << endl;
 }

 return 0;
}
