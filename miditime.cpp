// See how long a round-trip MIDI message takes using various timers.

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
#include <boost/lexical_cast.hpp>

// #using <mscorlib.dll>
using namespace std;
using namespace boost;

ostream *errnlog = NULL;
ostream *outnlog = NULL;
ostream *justlog = NULL;

const int SLOWEST_REPEAT_RESPONSE = 40;
const int MAX_SLOW_RESPONSES = 5;
const int LONGEST_TEST = 3600000; // 1 hour 
const int MAX_ACCEPTABLE_DELTA = 4;

HMIDIOUT midiOut;
HMIDIIN midiIn;
const UINT MIDDLE_C_ON = 0x00403C90;
const UINT MIDDLE_C_OFF = 0x00003C90;

bool got_response = false;
int slow_responses = 0;

// TGT (timeGetTime) time is in milliseconds, like midi time
DWORD tgt_time_started = 0;
DWORD tgt_time_sent = 0;
DWORD tgt_time_rcvd;
INT32 tgt_delta = 0;
INT64 tgt_running_delta_total = 0;

// QPC (QueryPerformanceCounter) time is in "counts"
LARGE_INTEGER qpc_time_started;
LARGE_INTEGER qpc_time_sent;
LARGE_INTEGER qpc_time_rcvd;
LARGE_INTEGER qpc_counts_per_sec;
INT64 qpc_delta_in_ms = 0;
INT64 qpc_running_delta_total = 0;

INT32 notes_received = 0;

class teebuf: public std::streambuf {
  public:
    teebuf(std::streambuf* sbuf1, std::streambuf* sbuf2):
      m_sbuf1(sbuf1), m_sbuf2(sbuf2) {}
  private:
    int_type overflow(int_type c) {
      if (m_sbuf1->sputc(c) == traits_type::eof())
        return traits_type::eof();
      if (m_sbuf2->sputc(c) == traits_type::eof())
        return traits_type::eof();
      return traits_type::not_eof(c);
    }
    std::streambuf* m_sbuf1;
    std::streambuf* m_sbuf2;
  };

void midiTimeIt (DWORD timestamp) {
	INT64 timestamp_in_counts;
	INT64 qpc_delta_in_counts;

	got_response = true;

	// timestamp holds the number of milliseconds since midiInStart was called.
	// So tgt_time_started + timestamp tells us when it was received - 
	// IFF tgt is in sync with the MIDI driver.
	tgt_time_rcvd = tgt_time_started + timestamp;
	tgt_delta = tgt_time_rcvd - tgt_time_sent;
	
	// Ditto for QPC, but the math is more complex because QPC is in counts, not ms
	timestamp_in_counts = (timestamp * qpc_counts_per_sec.QuadPart) / 1000;
	qpc_time_rcvd.QuadPart = qpc_time_started.QuadPart + timestamp_in_counts;
	qpc_delta_in_counts = qpc_time_rcvd.QuadPart - qpc_time_sent.QuadPart;
	qpc_delta_in_ms = (qpc_delta_in_counts * 1000) / qpc_counts_per_sec.QuadPart;

	*justlog << "Round trip: " << tgt_delta << " ms (TGT), " <<
			qpc_delta_in_ms << " ms (QPC)";
	
	tgt_running_delta_total += tgt_delta;
	qpc_running_delta_total += qpc_delta_in_ms;
	notes_received++;	
	if ((tgt_delta > SLOWEST_REPEAT_RESPONSE) ||
		(qpc_delta_in_ms > SLOWEST_REPEAT_RESPONSE)) {
			slow_responses++;
			*justlog << " Slow response #" << slow_responses << endl;
		}
	else {
		slow_responses = 0; // reset when we get a timely one 
		*justlog << endl;
	}
}

void midiData (HMIDIIN handle, DWORD data, DWORD timestamp) {
	switch (data) {
		case MIDDLE_C_ON:
			*justlog << "Data: " << data << " Timestamp: " << timestamp << endl;
			midiTimeIt (timestamp);
			break;
		case MIDDLE_C_OFF:
			;
			// Do nothing
			break;
		default:
			*errnlog << "Unexpected MIDI in: " << hex << data << dec << endl;
			break;
	}
}

void CALLBACK midiCallback(HMIDIIN handle, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) {
	switch (uMsg) {
		case MIM_OPEN:
			break;
		case MIM_CLOSE:
			break;
		case MIM_DATA:
			midiData(handle, dwParam1, dwParam2);
			break;
		case MIM_ERROR:
			*errnlog << "Unexpected MIDI data " << hex << dwParam1 << dec;
			break;
		case MIM_LONGDATA:
			*errnlog << "Unexpected sysex received." << endl;
			break;
		case MIM_LONGERROR:
			*errnlog << "Unexpected sysex error received." << endl;
			break;

		default:
			*errnlog << "Unexpected MIDI message type " << uMsg << " received.";
			break;
	}
	
	return;
}


void show_mm_error(MMRESULT code, string description) {
	char lpMsgBuf[80];
	
	// I have a hunch that midiInGetErrorText and midiOutGetErrorText are the same function.  If not, 
	// oh well - it's just a test app.

	if (midiInGetErrorText (code, lpMsgBuf, 80) != MMSYSERR_NOERROR) {
		*errnlog << "MM Error # " << code << ". " << description << endl;
	}
	else {
		*errnlog << "MM Error # " << code << ": " << lpMsgBuf << ". " << description << endl;
	}
	return;
}

void show_error(string description) {
	DWORD code;
	LPVOID lpMsgBuf;

	code = GetLastError();

	if (! FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf, 0, NULL)) {
			*errnlog << "Unknown error #" << code << endl;
			return;
		}

		if (errnlog != NULL) {
			*errnlog << "Error # " << code << ": " << lpMsgBuf << 
				". " << description << endl;
		} else {
			cerr << "Error # " << code << ": " << lpMsgBuf <<
				". " << description << endl;
		}

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

bool qpc_in_bounds(void) {
	if (qpc_delta_in_ms < 0) 
		return (false);

	if ((qpc_delta_in_ms > SLOWEST_REPEAT_RESPONSE) &&
		(tgt_delta <= SLOWEST_REPEAT_RESPONSE) &&			// avoid false positive
		(slow_responses >= MAX_SLOW_RESPONSES))
		return (false);

	return (true);
}

bool tgt_in_bounds(void) {
	if (tgt_delta < 0) 
		return (false);

	if ((tgt_delta > SLOWEST_REPEAT_RESPONSE) &&
		(qpc_delta_in_ms <= SLOWEST_REPEAT_RESPONSE) &&			// avoid false positive
		(slow_responses >= MAX_SLOW_RESPONSES))
		return (false);

	return (true);
}

INT64 calc_delta() {
	INT64 tgt_duration_in_usec		= (INT64)(tgt_time_sent - tgt_time_started) * 1000I64;
	INT64 qpc_duration_in_counts	= qpc_time_sent.QuadPart - qpc_time_started.QuadPart; 
	INT64 qpc_duration_in_usec		= (qpc_duration_in_counts * 1000000I64)/qpc_counts_per_sec.QuadPart; 
	INT64 qpc_tgt_delta				= qpc_duration_in_usec - (INT64)tgt_duration_in_usec; 
	return (qpc_tgt_delta);
}


bool keep_going(void) {
	static bool apologized = false;
	
	if ((tgt_time_sent - tgt_time_started) < LONGEST_TEST) {
		return (true);
	}
	
	// Even if we're over LONGEST_TEST, if we are starting to see a drift, we should keep going!
	
	if (calc_delta() > MAX_ACCEPTABLE_DELTA) {
		if (apologized == false) {
			cout << endl << "My hour is up, but your clocks are drifting so slowly that " << endl <<
				"I need to continue testing to be sure which one is wrong.  Press " << endl <<
				"CTRL-BREAK to abort if you have better things to do; otherwise, I will " << endl <<
				"keep going until I have an answer." << endl;
			*justlog << "Slow drift.  Continuing." << endl;
			apologized = true;
		}
		return (true);
	}

	// Time's up and delta's fine
	return (false);
}

	
void choose_midi_devices(void) {
	MIDIOUTCAPS moc;
	MIDIINCAPS mic;
	MMRESULT mmcode;
	UINT nOutputs;
	UINT outputNum;
	UINT nInputs;
	UINT inputNum;
	UINT i;
	
	nOutputs = midiOutGetNumDevs();
	if (nOutputs == 0) {
		*errnlog << "There are no MIDI outputs on this machine." << endl;
		exit(1);
	}

	for (i = 0; i < nOutputs ; i++)
	{
	    mmcode = midiOutGetDevCaps(i, &moc, sizeof(MIDIOUTCAPS));
		if (mmcode != MMSYSERR_NOERROR) {
			show_mm_error(mmcode, "getting device caps for output #" + lexical_cast<string>(i));
			abort();
		}

		cout << "Output #" << i + 1 << ": " << moc.szPname << endl;
	}

	outputNum = 0;
	while ((outputNum < 1) || (outputNum > nOutputs)) {
        cout << endl << "Choose output (1 to " << nOutputs << "): ";
		cin >> outputNum;
	}

	mmcode = midiOutOpen(&midiOut, outputNum - 1, 0, 0, CALLBACK_NULL);
	if (mmcode != MMSYSERR_NOERROR) {
		show_mm_error(mmcode, "opening MIDI output #" + lexical_cast<string>(outputNum - 1));
		abort();
	}

	nInputs = midiInGetNumDevs();
	if (nInputs == 0) {
		*errnlog << "There are no MIDI inputs on this machine." << endl;
		exit(1);
	}
	
	cout << endl << endl;
	
	for (i = 0; i < nInputs ; i++)
	{
	    mmcode = midiInGetDevCaps(i, &mic, sizeof(MIDIINCAPS));
		if (mmcode != MMSYSERR_NOERROR) {
			show_mm_error(mmcode, "getting device caps for input " + lexical_cast<string>(i));
			abort();
		}
		cout << "Input #" << i + 1 << ": " << mic.szPname << endl;
	}

	inputNum = 0;
	while ((inputNum < 1) || (inputNum > nInputs)) {
        cout << endl << "Choose input (1 to " << nInputs << "): ";
		cin >> inputNum;
	}
	mmcode = midiInOpen(&midiIn, inputNum - 1, (DWORD)midiCallback, 0, CALLBACK_FUNCTION);
	if (mmcode != MMSYSERR_NOERROR) {
		show_mm_error(mmcode, "opening MIDI input #" + lexical_cast<string>(inputNum - 1));
		abort();
	}

	return;
}

void show_intro(void) {
	cout << endl << 
		"MIDITime 1.2 - 5/15/04" << endl << endl <<
		"   Windows provides two unrelated high-resolution clocks, and every MIDI" << endl <<
		"interface uses one of these clocks for timestamping incoming MIDI data." << endl <<
		"These two clocks drift apart on most, if not all, systems.  To my" << endl <<
		"knowledge, most PC sequencers look at only one of these clocks, which is why" << endl <<
		"some interfaces seem to work better with some software.  As of May 2004, " << endl <<
		"Nuendo 2.20 and Cubase SX 2.20 allow you to choose." << endl << endl << 
		"   This software will test your MIDI interface to see which clock it uses." << endl <<
		"You will need to connect a standard MIDI cable from an output port on your" << endl <<
		"interface to an input port on the same interface." << endl << endl <<
		"Press any key...";

	getch();

	cout << endl << endl << 
		"   Neither MIDITime nor I are provided by, sponsored by, approved by, or " << endl <<
		"affiliated with Pinnacle Systems or Steinberg, the makers of Nuendo and " << endl <<
		"Cubase.  It is my own grotesque creation.  However, I'm not responsible for " << endl <<
		"any problems it causes you or anyone else either.  There is no warranty.  " << endl <<
		"Nuendo and Cubase are registered trademarks of Steinberg Media" << endl <<
		"Technologies GmbH." <<  endl << endl << 
		"Please send all MIDITime comments, questions, bugs and suggestions" << endl <<
		"to miditime@shopwatch.org." << endl << endl <<
		"The latest version of the FAQ can be found at " << endl << 
		"http://www.jay.fm/miditime" << endl << endl <<
		"Copyright 2004 Jay Levitt.  " << endl << endl << 
		"Press any key...";

	getch();

	cout << endl << endl << 
		"Please tell MIDITime which MIDI ports you are using for the loopback test." << endl << endl;
}

void show_test_starting(void) {
	cout << endl << endl << 
		"MIDITime will now send an exciting melody through your loopback cable, " << endl <<
		"consisting solely of Middle C.  (Aren't you glad you aren't the cable?)" << endl <<
		"This will probably take just a few minutes, but in rare cases it may take" << endl <<
		"up to an hour.  You can use your machine normally during this time, but" << endl <<
		"if another program tries to open the MIDI port we're using, you may" << endl <<
		"get an error from that program, and if it sends MIDI data to the port, " << endl <<
		"MIDITime will get confused." << endl;
}

void report_midi_interface(void) {
	MIDIINCAPS mic;
	MMRESULT mmcode;

	mmcode = midiInGetDevCaps((UINT_PTR)midiIn, &mic, sizeof(mic));
	if (mmcode == MMSYSERR_NOERROR) {
		*justlog <<
			"Device:         " << mic.szPname << endl <<
			"Driver:         " << hex << setw(4) << setfill('0') << mic.vDriverVersion << 
			dec << setfill(' ') << endl;
	}
}


void report_cpu_speed(void) {
	HKEY hKey;
	DWORD speed;
	DWORD bufsize = sizeof(speed);
	char processor[1024];
	DWORD procsize = sizeof(processor);

	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, 
		KEY_READ, &hKey)) {
			if (ERROR_SUCCESS == RegQueryValueEx(hKey, "~MHZ", 0, 0, (LPBYTE)&speed, &bufsize)) {
				*justlog << "CPU speed:     " << speed << endl;
			}
			if (ERROR_SUCCESS == RegQueryValueEx(hKey, "ProcessorNameString", 0, 0, (LPBYTE)&processor, &procsize)) {
				*justlog << "CPU type:      " << processor << endl;
			}
			RegCloseKey(hKey);
		}
}


void report_results(void) {
	bool bug = false;
	
	cout << endl << endl;
	if (got_response == false) {
		cout << "MIDITime never got a response from your MIDI interface.  Check that " << endl <<
			"your loopback cable is connected to the ports you specified.  If this " << endl << 
			"continues, please contact miditime@shopwatch.org to report this bug." << endl;
		bug = true;
		*justlog << "Result:         No response!" << endl;
	}

	else if (!tgt_in_bounds() && !qpc_in_bounds()) {
		cout << "Both clocks reported unlikely round-trip times. " <<
			"Please contact miditime@shopwatch.org to report this bug." << endl;
		*justlog << "Result:         Out of bounds!" << endl;
		bug = true;		
	}	

	else if (!tgt_in_bounds() && qpc_in_bounds()) {
		cout << "Your interface takes about " <<
			setprecision(4) << float(qpc_running_delta_total) / float(notes_received) << 
			" ms to process " << endl <<
			"each note.  " << endl << endl << 
			"For best results in Nuendo or Cubase, you will need to set the 'Use " << endl <<
			"System Timestamp' option under Devices | Device Setup | DirectMusic." << endl <<
			"If your MIDI interface doesn't provide its own DirectMusic driver, you" << endl <<
			"will *have* to use the emulated port that Windows creates; the" << endl <<
			"Windows MIDI port will give you incorrect timing." << endl << endl;
		*justlog << "Result:         QPC." << endl;
	}

	else if (tgt_in_bounds() && !qpc_in_bounds()) {
		cout << "Your interface takes about " << 
			setprecision(4) << float(tgt_running_delta_total) / float(notes_received) << 
			" ms to process " << endl <<
			"each note.  " << endl << 
			"For best results in Nuendo or Cubase, you should NOT set the 'Use " << endl <<
			"System Timestamp' option under Devices | Device Setup | DirectMusic." << endl <<
			"If your interface provides its own DirectMusic driver, you can use" << endl <<
			"that port; if not, you should probably use the Windows MIDI port." << endl <<
			"You should probably avoid the emulated DirectMusic port." << endl << endl;

		*justlog << "Result:         TGT." << endl;
	}
	
	else if (tgt_time_sent - tgt_time_started >= LONGEST_TEST) {
		cout << "Amazing!  After an hour, both your clocks stayed in sync!" << endl <<
			"This is both impressive and unusual.  ANY MIDI interface will work with " << endl << 
			"your system.  Your particular interface took about " <<
			setprecision(4) << float(tgt_running_delta_total / notes_received) <<
			" ms to process each note.  Please tell me what type" << endl <<
			"of PC or motherboard you have!" << endl;
		*justlog << "Result:         In sync!" << endl;
	}

	else {	
		cout << "Logic error!  Please report this bug to miditime@shopwatch.org." << endl;
		*justlog << "Result:         Logic error!" << endl;
		bug = true;
	}

	INT64 qpc_tgt_delta = calc_delta();

	cout << endl << "By the way, after " <<	
		(tgt_time_sent - tgt_time_started)/1000 << " seconds, your clocks had drifted apart by" << endl << 
		_abs64(qpc_tgt_delta) << " microseconds.  A man with one watch knows what time it is; a man" << endl <<
		"with two watches is never sure." << endl << endl;		

	cout << "Press any key...";
	getch();
	cout << endl << endl;

	cout << "If you get a chance, please e-mail the file 'MIDITime.log', found in" << endl <<
		"your 'My Documents' folder, to miditime@shopwatch.org.  Also let me know" << endl <<
		"the brand and model of motherboard or PC, the chipset if you know it," << endl <<
		"the brand and model of your MIDI interface, and whether it is connected" << endl << 
		"via serial, parallel, or USB.  Thanks!" << endl << endl <<
		"The latest version of my PC MIDI timing FAQ is located at:" << endl << 
		"http://www.jay.fm/miditime" << endl << endl;

	*justlog << endl << 
		"---------" << endl <<
		"TGT start:      " << setw(7) << tgt_time_started <<   "   " <<
		"QPC start:      " << qpc_time_started.QuadPart << endl <<
		"TGT sent:       " << setw(7) << tgt_time_sent << "   " <<
		"QPC sent:       " << qpc_time_sent.QuadPart << endl <<
		"TGT in bounds:  " << setw(7) << tgt_in_bounds() << "   " << 
		"QPC in bounds:  " << setw(7) << qpc_in_bounds() << endl << 
		"TGT delta:      " << setw(7) << tgt_delta << "   " << 
		"QPC delta:      " << setw(7) << qpc_delta_in_ms << endl <<
		"TGT avg:        " << setw(7) << setprecision(4) << 
		float(tgt_running_delta_total) / float(notes_received) << "   " << 
		"QPC avg:        " << setw(7) << float(qpc_running_delta_total) / float(notes_received) << endl <<
		"Slow responses: " << setw(7) << slow_responses << "   " << 
		"QPC frequency:  " << qpc_counts_per_sec.QuadPart << endl <<
		"Run:            " << setw(7) << (tgt_time_sent - tgt_time_started)/1000 << "   " << 
		"Drift:          " << setw(7) << qpc_tgt_delta << endl;

	report_midi_interface();
	report_cpu_speed();

	*justlog << "---------" << endl;
}

int _tmain() {
	const UINT TGT_RESOLUTION = 1;
	MMRESULT mmcode;
	bool first = true;

	INT32 last_dot = 0;
	const int DOT_INTERVAL = 10000; // 10 seconds

	const UINT NOTE_LENGTH = 500;
	const UINT CYCLE_TIME = 5000;

	TCHAR szPath[MAX_PATH];

	if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, szPath))) 
	{
		PathAppend(szPath, TEXT("MIDITime.log"));
	}
	else {
		show_error("Finding the My Documents folder");
		abort();
	}


	ofstream flog(szPath);
    teebuf sbuf(std::cout.rdbuf(), flog.rdbuf());
    ostream bothout(&sbuf);
	outnlog = &bothout;

	teebuf errbuf(std::cerr.rdbuf(), flog.rdbuf());
	ostream botherr(&errbuf);
	errnlog = &botherr;
	justlog = &flog;

	show_intro();
	choose_midi_devices();
	show_test_starting();

	// Ask windows for 1ms resolution
	mmcode = timeBeginPeriod(TGT_RESOLUTION);
	if (mmcode != TIMERR_NOERROR) {
		show_mm_error (mmcode, "timeBeginPeriod");
		abort();
	}

	// QPC is in "counts".  Let's find out what our count frequency is
	qpc_counts_per_sec.HighPart = 0;
    qpc_counts_per_sec.LowPart = 0;
	if (! QueryPerformanceFrequency(&qpc_counts_per_sec)) {
		show_error("during QueryPerformanceFrequency");
		abort();
	}
	
		*justlog << "QPC frequency: " << qpc_counts_per_sec.QuadPart << endl;
	

	// Initialize all three of our timers - TGT, QPC, and MIDI
    // (MIDI timestamps get reset to zero by midiInStart)
	
	tgt_time_started = timeGetTime();
	if (! QueryPerformanceCounter(&qpc_time_started)) {
		show_error("during QueryPerformanceCounter");
		abort();
	}
	mmcode = midiInStart(midiIn);
	if (mmcode != MMSYSERR_NOERROR) {
		show_mm_error (mmcode, "midiInStart");
		midiInClose(midiIn);
		midiOutClose(midiOut);
		abort();
	}

	while (first || (tgt_in_bounds() && qpc_in_bounds() && keep_going()))
	{
		first = false;
		tgt_time_sent = timeGetTime();
		if (! QueryPerformanceCounter(&qpc_time_sent)) {
			show_error("qpc time_sent");
			abort();
		}
	
		if (midiOutShortMsg(midiOut, MIDDLE_C_ON) != MMSYSERR_NOERROR) {
			show_error("sending middle C on");
			abort();
		}

		if ((tgt_time_sent - last_dot) > DOT_INTERVAL) {
			cout << '.';
			last_dot = tgt_time_sent;
		}
		
		Sleep (NOTE_LENGTH);

		if (midiOutShortMsg(midiOut, MIDDLE_C_OFF) != MMSYSERR_NOERROR) {
			show_error ("sending middle C off");
			abort();
		}
	
		Sleep (CYCLE_TIME);
	}

	report_results();
	cout << endl << "Press any key: " << endl;
	getch();

}
