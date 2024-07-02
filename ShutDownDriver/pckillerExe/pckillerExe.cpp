// pckillerExe.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <Windows.h>
#include <tchar.h>
#include <string>
#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

// Define IOCTL codes (must match those defined in the driver)
#define SHUT_DOWN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CANCEL_SHUTDOWN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMAIN_TIME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Function to send IOCTL request to driver
BOOL sendIOToDriver(DWORD controlCode, PVOID inputBuffer, DWORD inputBufferSize, PVOID outputBuffer, DWORD outputBufferSize, LPDWORD bytesReturned) {
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	BOOL status = FALSE;

	// Open handle to the device object
	hDevice = CreateFile(L"\\\\.\\pckiller", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
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

BOOL ConvertTimeToLargeInteger(const std::wstring& timeStr, LARGE_INTEGER *dest)
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
		// if command is  -shutdown
		if (command == L"-shutdown") {
			//shutdown the computer
			if (argc > 2) {
				std::wstring strTime = argv[2];
				//create a large integer
				LARGE_INTEGER time;
				if (!ConvertTimeToLargeInteger(strTime, &time)) {
					std::wcout << "Failed to convert time to LARGE_INTEGER" << std::endl;
					return 1;
				}

				std::wcout<< "Shutting down in " << time.QuadPart << " seconds" << std::endl;
				time.QuadPart = -10000000LL * time.QuadPart; // seconds in 100-nanosecond intervals
				std::wcout << "Shutting down in " << time.QuadPart << " seconds" << std::endl;

				// Send IOCTL_SET_SHUTDOWN_TIME request
				status = sendIOToDriver(SHUT_DOWN, &time, sizeof(LARGE_INTEGER), NULL, 0, &bytesReturned);
				if (status) {
					std::wcout <<  L"Set shutdown time IOCTL sent successfully" << std::endl;
				}
			}
			else
			{
				// not the right parameter
				std::wcout << "Please provide time in seconds" << std::endl;
				return 1;
			}
		}
		else if (command == L"-cancel") {
			status = sendIOToDriver(CANCEL_SHUTDOWN, NULL, 0, NULL, NULL, &bytesReturned);
			if (status) {
				std::wcout <<L"Cancel shut down was snent to IOCTL" << std::endl;
			}
		}
		else if (command == L"-remaining")
		{
			status = sendIOToDriver(REMAIN_TIME, NULL, 0, &remainingTime, sizeof(LARGE_INTEGER), &bytesReturned);
			if (status) {
				printf("Remaining time: %lld nanoseconds\n", remainingTime.QuadPart);
				//convert time to seconds and miliseconds format
				std::wcout << "Remaining time seconds: " << formatTime(remainingTime) << std::endl;
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
