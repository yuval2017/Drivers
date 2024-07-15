

#include <iostream>
#include <Windows.h>
#include <tchar.h>
#include <string>
#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

#define ADD_PROCESS_BLACKLIST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ADD_PROCESS_WHITELIST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KILL_PROCESS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_LISTS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BLOCK_FILENAME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DUMB_BYTES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _DUMB_BYTES_STRUCT {
	ULONG processID; // process ID
	ULONG n;       // number of bytes to dump
} DUMB_BYTES_STRUCT, * PDUMB_BYTES_STRUCT;

typedef struct _UNICODE_STRING {
	USHORT Length;       // The length, in bytes, of the string pointed to by Buffer.
	USHORT MaximumLength; // The maximum length, in bytes, of the string that can be contained in the buffer.
	PWSTR Buffer;        // Pointer to the string buffer.
} UNICODE_STRING, * PUNICODE_STRING;



// function that check if driver if running
BOOL isDriverRunning() {
	HANDLE hDevice = CreateFile(L"\\\\.\\sugiotAv", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	CloseHandle(hDevice);
	return TRUE;
}
//function that create driver SugionAv.sys must be in the same folder and then run it
BOOL createDriver() {
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
	BOOL status = FALSE;
	// Open a handle to the SCM database
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (schSCManager == NULL) {
		printf("Failed to open SCM database! Error: %d\n", GetLastError());
		return FALSE;
	}
	//check if SugiotAv.sys is in the same folder
	if (GetFileAttributes(L".\\SugiotAv.sys") == INVALID_FILE_ATTRIBUTES) {
		printf("Driver file not found! Make sure SugiotAv.sys is in the same folder as the executable\n");
		CloseServiceHandle(schSCManager);
		return FALSE;
	}

	// Create a service for the driver
	schService = CreateService(schSCManager,
		L"SugiotAv", 
		L"SugiotAv", 
		SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, 
		L".\\SugiotAv.sys", 
		NULL, NULL, NULL, NULL, NULL);
	if (schService == NULL) {
		printf("Failed to create service! Error: %d\n", GetLastError());
		CloseServiceHandle(schSCManager);
		return FALSE;
	}

	// Start the driver service
	status = StartService(schService, 0, NULL);
	if (!status) {
		printf("Failed to start service! Error: %d\n", GetLastError());
	}

	// Close service and SCM handles
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);

	return status;
}

// Function to send IOCTL request to driver
BOOL sendIOToDriver(DWORD controlCode, PVOID inputBuffer, DWORD inputBufferSize, PVOID outputBuffer, DWORD outputBufferSize, LPDWORD bytesReturned) {
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	BOOL status = FALSE;

	// Open handle to the device object
	hDevice = CreateFile(L"\\\\.\\sugiotAv", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		printf("Failed to open device! Error: %d\n", GetLastError());
		return FALSE;
	}

	// Send IOCTL request to driver
	status = DeviceIoControl(hDevice, controlCode, inputBuffer, inputBufferSize, outputBuffer, outputBufferSize, bytesReturned, NULL);
	if (!status) {
		printf("IOCTL request failed! Error: %d\n", GetLastError());
	}

	// Close device handle
	CloseHandle(hDevice);

	return status;
}

BOOL ConvertTimeToLargeInteger(const std::wstring& timeStr, LARGE_INTEGER* dest)
{
	// Example: Convert time string to a LARGE_INTEGER
	try {
		// Assuming timeStr represents a valid time in some format that can be converted to an integer
		int timeInt = std::stoi(timeStr);
		LARGE_INTEGER largeInt;
		largeInt.QuadPart = timeInt; // Assign the integer value to LARGE_INTEGER's QuadPart
		*dest = largeInt;
		return TRUE;
	}
	catch (const std::exception& e) {
		std::cerr << "Error converting time string to LARGE_INTEGER: " << e.what() << std::endl;
		// Return an invalid LARGE_INTEGER if conversion fails
		LARGE_INTEGER invalidLargeInt = { 0 };
		return FALSE;
	}
}


std::wstring formatTime(const LARGE_INTEGER& nanoseconds) {
	// Convert nanoseconds to milliseconds
	long long milliseconds = nanoseconds.QuadPart / 1000000LL;

	// Extract seconds and remaining milliseconds
	long long seconds = milliseconds / 10LL;
	milliseconds = milliseconds % 100LL;

	// Create a wstringstream to format the output

	std::wstringstream wss;
	wss << seconds << L":" << std::setfill(L'0') << std::setw(2) << milliseconds;

	return wss.str();
}
// Function to initialize UNICODE_STRING
void InitUnicodeString(PUNICODE_STRING unicodeString, const std::wstring& sourceString) {
	unicodeString->Length = static_cast<USHORT>(sourceString.length() * sizeof(wchar_t));
	unicodeString->MaximumLength = unicodeString->Length + sizeof(wchar_t); // Allowing space for a null terminator
	unicodeString->Buffer = const_cast<PWSTR>(sourceString.c_str());
}
void InitDumbStruct(PDUMB_BYTES_STRUCT dumbStruct, const std::wstring& processID, const std::wstring& n) {
	// Convert process ID and n to ULONG
	ULONG processIDInt = std::stoul(processID);
	ULONG nInt = std::stoul(n);

	// Initialize the DUMB_BYTES_STRUCT
	dumbStruct->processID = processIDInt;
	dumbStruct->n = nInt;
}
// 64 bit main with args main(int argc, wchar_t* argv[])
int wmain(int argc, wchar_t* argv[])
{
	BOOL status;
	DWORD bytesReturned;
	LARGE_INTEGER remainingTime;


	//cehck if is higer then one and the first char is '-'
	if (argc > 1 || argv[1][0] != L'-')
	{
		std::wstring command = argv[1];
		std::wcout << command << std::endl;
		if (command == L"blacklist") {

			if (argc > 2) {
				std::wstring process = argv[2];

				// Create a UNICODE_STRING
				UNICODE_STRING uniString;
				InitUnicodeString(&uniString, process);
				//send to driver
				status = sendIOToDriver(ADD_PROCESS_BLACKLIST, (PVOID)&uniString, sizeof(UNICODE_STRING), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Process added to whitelist" << std::endl;
				}
			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide process" << std::endl;
				return 1;
			}
		}
		else if (command == L"whitelist") {
			if (argc > 2) {
				std::wstring process = argv[2];

				// Create a UNICODE_STRING
				UNICODE_STRING uniString;
				InitUnicodeString(&uniString, process);
				//send to driver
				status = sendIOToDriver(ADD_PROCESS_WHITELIST, (PVOID)&uniString, sizeof(UNICODE_STRING), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Process added to whitelist" << std::endl;
				}
			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide process" << std::endl;
				return 1;
			}
		}
		else if (command == L"block") {
			if (argc > 2) {
				std::wstring fileName = argv[2];
				//convert to ULONG
				//send BLOCK to control with unicode
				status = sendIOToDriver(BLOCK_FILENAME, (PVOID)fileName.c_str(), fileName.size() * sizeof(wchar_t), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Process killed" << std::endl;
				}
			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide process" << std::endl;
				return 1;
			}
		}
		else if (command == L"-kill") {
			//shutdown the computer
			if (argc > 2) {
				std::wstring processID = argv[2];
				//convert to ULONG
				ULONG processIDInt = std::stoul(processID);
				//send BLAKC_LIST to control with unicode
				status = sendIOToDriver(KILL_PROCESS, (PVOID)&processIDInt, sizeof(ULONG), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Process killed" << std::endl;
				}
			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide process" << std::endl;
				return 1;
			}
		}
		else if (command == L"dumb") {
			if (argc > 2) {
				std::wstring processID = argv[2];
				std::wstring NumOfBytes = argv[3];
				std::wstring filePath = argv[4];
				DUMB_BYTES_STRUCT MyStruct;

				//convert to ULONG
				ULONG NumOfBytesL = std::stoul(NumOfBytes);
				ULONG processIDInt = std::stoul(processID);

				//Init struct
				InitDumbStruct(&MyStruct, processID, NumOfBytes);

				//send BLAKC_LIST to control with unicode
				status = sendIOToDriver(DUMB_BYTES, (PVOID)&MyStruct, sizeof(DUMB_BYTES_STRUCT), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Process killed" << std::endl;
				}
				//print the value that was returned in bytes value

			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide process" << std::endl;
				return 1;
			}

		}
		else if (command == L"-init") {
			if (isDriverRunning())
			{
				//send clear to control
				status = sendIOToDriver(CLEAR_LISTS, NULL, 0, NULL, 0, &bytesReturned);
				if (status) {
					std::wcout << "Driver Was cleared while running" << std::endl;
				}
				return 1;
			}
			else {
				//create driver
				if (createDriver()) {
					std::wcout << "Driver created successfully" << std::endl;
				}
				else {
					std::wcout << "Failed to create driver" << std::endl;
				}
			}
		}
		else {
			std::wcout << "Please provide a valid command" << std::endl;
			return 1;
		}
		return 1;
	}
	else {
		std::wcout << "Please proveide args" << std::endl;
		return 1;
	}

	return 0;
}



// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
