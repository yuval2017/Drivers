#include <ntifs.h>
#include <ntddk.h>
#include <ntdef.h>
#include <wdm.h>

#define ADD_PROCESS_BLACKLIST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ADD_PROCESS_WHITELIST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KILL_PROCESS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_LISTS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BLOCK_FILENAME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DUMB_BYTES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
//struct for DUMB_BYTES
typedef struct _DUMB_BYTES_STRUCT {
	ULONG processID ; // process ID
	ULONG n;       // number of bytes to dump
} DUMB_BYTES_STRUCT, * PDUMB_BYTES_STRUCT;



// Global variable to store the time

//black list set dynamically size
#define MAX_PROCESS_NAME_LENGTH 256
#define MAX_LIST_SIZE 1000

//black and white list
UNICODE_STRING Blacklist[MAX_LIST_SIZE];
UNICODE_STRING Whitelist[MAX_LIST_SIZE];
UNICODE_STRING BlockList[MAX_LIST_SIZE];


//list sizes
ULONG BlacklistSize = 0;
ULONG WhitelistSize = 0;
ULONG BlockListSize = 0;



//defind type PsGetProcessSectionBaseAddress
typedef PVOID (*PSGETPROCESSSECTIONBASEADDRESS)(PEPROCESS Process);

PSGETPROCESSSECTIONBASEADDRESS PsGetProcessSectionBaseAddress;


BOOLEAN IsProcessInList(UNICODE_STRING* ProcessName, UNICODE_STRING* List, ULONG ListSize) {
	for (ULONG i = 0; i < ListSize; i++) {
		//print the currect process
		DbgPrint("Current process: %wZ\n", &List[i]);
		if (RtlEqualUnicodeString(ProcessName, &List[i], TRUE)) {
			return TRUE;
		}
	}
	return FALSE;
}
NTSTATUS AddProcessNameToList(UNICODE_STRING* List, ULONG* ListSize, UNICODE_STRING* ProcessName) {
	if (*ListSize >= MAX_LIST_SIZE) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Allocate memory for the new process name
	UNICODE_STRING* newEntry = &List[*ListSize];
	newEntry->Buffer = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, ProcessName->MaximumLength, 'List');
	if (newEntry->Buffer == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	newEntry->Length = 0;
	newEntry->MaximumLength = ProcessName->MaximumLength;

	// Copy the process name to the new entry
	RtlCopyUnicodeString(newEntry, ProcessName);
	(*ListSize)++;

	// Print that process was added to the list
	DbgPrint("Process %wZ added to the list\n", ProcessName);
	return STATUS_SUCCESS;
}

VOID PrintList(UNICODE_STRING* List, ULONG ListSize) {
	for (ULONG i = 0; i < ListSize; i++) {
		//print UNICODE_STRING
		DbgPrint("Process %wZ\n", &List[i]);
	}
}
//clear list
VOID ClearList(UNICODE_STRING* List, ULONG* ListSize) {
	for (ULONG i = 0; i < *ListSize; i++) {
		if (List[i].Buffer) {
			ExFreePool(List[i].Buffer);
			List[i].Buffer = NULL;
		}
	}
	*ListSize = 0;
}
//clear all lists
VOID ClearAllLists() {
	ClearList(Blacklist, &BlacklistSize);
	ClearList(Whitelist, &WhitelistSize);
	ClearList(BlockList, &BlockListSize);
}

VOID CheckCurrentIrqlAndAct(VOID) {
	KIRQL irql = KeGetCurrentIrql();
	DbgPrint("Current IRQL: %d\n", irql);
	if (irql == PASSIVE_LEVEL) {
		DbgPrint("IRQL is PASSIVE_LEVEL\n");
	}
	else if (irql == APC_LEVEL) {
		DbgPrint("IRQL is APC_LEVEL\n");
	}
	else if (irql == DISPATCH_LEVEL) {
		DbgPrint("IRQL is DISPATCH_LEVEL\n");
	}
	else if (irql == HIGH_LEVEL) {
		DbgPrint("IRQL is HIGH_LEVEL\n");
	}
	else {
		DbgPrint("IRQL is unknown\n");
	}
}

// Function to get the executable name from the full path
VOID getExeNameFromPath(PCUNICODE_STRING FullPath, PUNICODE_STRING Dest) {
	USHORT i;

	// Allocate a buffer for the destination string
	WCHAR buffer[MAX_PROCESS_NAME_LENGTH];
	Dest->Buffer = buffer;
	Dest->Length = 0;
	Dest->MaximumLength = MAX_PROCESS_NAME_LENGTH;

	// Find the last backslash in the FullPath
	for (i = FullPath->Length / sizeof(WCHAR); i > 0; i--) {
		if (FullPath->Buffer[i - 1] == L'\\') {
			break;
		}
	}

	// Calculate the length of the executable name
	USHORT exeNameLength = FullPath->Length - i * sizeof(WCHAR);

	// Copy the executable name to the destination string
	RtlCopyMemory(Dest->Buffer, &FullPath->Buffer[i], exeNameLength);
	Dest->Length = exeNameLength;
	Dest->MaximumLength = exeNameLength + sizeof(WCHAR);

	// Null-terminate the destination string
	Dest->Buffer[exeNameLength / sizeof(WCHAR)] = L'\0';

	//print exe name
	DbgPrint("Exe name: %wZ\n", Dest);
}

NTSTATUS DumbNBitsToProcess(HANDLE ProcessId, ULONG n) {
	PEPROCESS process;
	NTSTATUS status;
	PVOID imageBase;

	// Get the process object for the given process ID
	status = PsLookupProcessByProcessId(ProcessId, &process);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to find process by ID, status: 0x%X\n", status);
		return status;
	}

	// Get the process image base
	imageBase = PsGetProcessSectionBaseAddress(process);
	if (imageBase == NULL) {
		DbgPrint("Failed to get process image base\n");
		ObDereferenceObject(process);
		return STATUS_UNSUCCESSFUL;
	}

	// Allocate memory to read the process memory
	PVOID buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, n, 'dmpB');
	if (buffer == NULL) {
		DbgPrint("Failed to allocate memory for buffer\n");
		ObDereferenceObject(process);
		return STATUS_INSUFFICIENT_RESOURCES;
	}


	// Attach to the target process's address space
	KAPC_STATE apcState;
	KeStackAttachProcess(process, &apcState);

	// Read the process memory
	__try {
		RtlCopyMemory(buffer, imageBase, n);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DbgPrint("Exception occurred while reading process memory\n");
		status = GetExceptionCode();
		KeUnstackDetachProcess(&apcState);
		ExFreePool(buffer);
		ObDereferenceObject(process);
		return status;
	}

	// Detach from the target process's address space
	KeUnstackDetachProcess(&apcState);

	// Print the bytes
	DbgPrint("Dump of %lu bytes from process ID %lu:\n", n, (ULONG)(ULONG_PTR)ProcessId);
	for (ULONG i = 0; i < n; i++) {
		DbgPrint("%02X ", ((PUCHAR)buffer)[i]);
		if ((i + 1) % 16 == 0) {
			DbgPrint("\n");
		}
	}
	DbgPrint("\n");

	// Free the allocated buffer
	ExFreePool(buffer);

	// Dereference the process object
	ObDereferenceObject(process);

	return STATUS_SUCCESS;
}
NTSTATUS KillProcess(HANDLE ProcessId) {
	PEPROCESS process;
	NTSTATUS status;

	// Get the process object for the given process ID
	status = PsLookupProcessByProcessId(ProcessId, &process);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to find process by ID, status: 0x%X\n", status);
		return status;
	}

	// Get a handle to the process
	HANDLE processHandle;
	status = ObOpenObjectByPointer(
		process,
		OBJ_KERNEL_HANDLE,
		NULL,
		DELETE,
		*PsProcessType,
		KernelMode,
		&processHandle
	);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to open process handle, status: 0x%X\n", status);
		ObDereferenceObject(process);
		return status;
	}

	// Terminate the process
	status = ZwTerminateProcess(processHandle, 0);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to terminate process, status: 0x%X\n", status);
	}

	// Close the process handle
	ZwClose(processHandle);

	// Dereference the process object
	ObDereferenceObject(process);

	return status;
}
VOID CreateProcessNotificationRoutineEx(
	_Inout_ PEPROCESS Process,
	_In_ HANDLE ProcessId,
	_Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
)
{
	UNREFERENCED_PARAMETER(Process);
	UNREFERENCED_PARAMETER(ProcessId);
	UNREFERENCED_PARAMETER(CreateInfo);
	if (CreateInfo) {
		UNICODE_STRING exeName;
		//getExeNameFromPath(CreateInfo->ImageFileName, &exeName);
		//DbgPrint("Process Created: %wZ \n", &exeName);
		// Find the last backslash in the FullPath
		USHORT i;

		for (i = CreateInfo->ImageFileName->Length / sizeof(WCHAR); i > 0; i--) {
			if (CreateInfo->ImageFileName->Buffer[i - 1] == L'\\') {
				break;
			}
		}
		//copy the exe name
		exeName.Buffer = &CreateInfo->ImageFileName->Buffer[i];
		exeName.Length = CreateInfo->ImageFileName->Length - i * sizeof(WCHAR);
		exeName.MaximumLength = CreateInfo->ImageFileName->Length - i * sizeof(WCHAR);

		// check if the proprocess is in the black list
		if (BlacklistSize > 0 && IsProcessInList(&exeName, Blacklist, BlacklistSize)) {
			// dont let process to be open
			CreateInfo->CreationStatus = STATUS_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY;
			DbgPrint("Process %wZ is in the black list\n", &exeName);
		}
		// check if the process is in the white list
		else if (WhitelistSize > 0 && !IsProcessInList(&exeName, Whitelist, WhitelistSize)) {
			// dont let process to be open
			CreateInfo->CreationStatus = STATUS_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY;
			DbgPrint("Process %wZ is not in the white list\n", &exeName);
		}
		else {
			// allow process to be open
			CreateInfo->CreationStatus = STATUS_SUCCESS;
			DbgPrint("Process Created: %wZ \n", &exeName);
		}
		//dbg process id
		//DbgPrint("Process created with ID: %d\n", ProcessId);
	}
	else {
		//DbgPrint("Process terminated ID: %d\n", ProcessId);
	}
}

void AvUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	//unload the symbol link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sugiotAv");
	//delete symbolic link
	IoDeleteSymbolicLink(&symLink);

	//delete device object
	IoDeleteDevice(DriverObject->DeviceObject);

	//clear all lists
	ClearAllLists();

	//Unregister the process notification routine
	PsSetCreateProcessNotifyRoutineEx(CreateProcessNotificationRoutineEx, TRUE);

	//Log debug message
	DbgPrint("Driver Unloaded\n");
}
NTSTATUS DriverCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	//Log debug message
	DbgPrint("DriverCreateClose Called\n");
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
NTSTATUS DriverIoControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	//Log debug message
	DbgPrint("DriverIoControl Called\n");
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case(ADD_PROCESS_WHITELIST):
	{
		DbgPrint("Send to white list\n");
		UNICODE_STRING* processName;

		// Validate parameters
		if (Irp->AssociatedIrp.SystemBuffer == NULL ||
			stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(UNICODE_STRING)) {
			status = STATUS_INVALID_PARAMETER;
			DbgPrint("Invalid parameter\n");
			break;
		}
		processName = (UNICODE_STRING*)Irp->AssociatedIrp.SystemBuffer;
		// Debug print the process name
		DbgPrint("Adding process to whitelist: %wZ\n", processName);

		status = AddProcessNameToList(Whitelist, &WhitelistSize, (UNICODE_STRING*)Irp->AssociatedIrp.SystemBuffer);
		break;
	}
	case(ADD_PROCESS_BLACKLIST):
	{
		DbgPrint("in BlackList\n");

		// Validate parameters
		if (Irp->AssociatedIrp.SystemBuffer == NULL ||
			stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(UNICODE_STRING)) {
			status = STATUS_INVALID_PARAMETER;
			DbgPrint("Invalid parameter\n");
			break;
		}
	
		// Debug print the process name
		DbgPrint("Adding process to blacklist: %wZ\n", (UNICODE_STRING*)Irp->AssociatedIrp.SystemBuffer);

	
		status = AddProcessNameToList(Blacklist, &BlacklistSize, (UNICODE_STRING*)Irp->AssociatedIrp.SystemBuffer);

		//print list
		PrintList(Blacklist, BlacklistSize);
		break;
	}
	case(KILL_PROCESS):
	{
		if (Irp->AssociatedIrp.SystemBuffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(ULONG)) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		//dbg thee gandle
		DbgPrint("Killing process with ID: %d\n", *(HANDLE*)Irp->AssociatedIrp.SystemBuffer);

		status = KillProcess(*(HANDLE*)Irp->AssociatedIrp.SystemBuffer);
		break;
	}
	case(DUMB_BYTES): 
	{
		//take the struct
		PDUMB_BYTES_STRUCT dumbBytesStruct;
		//check if the buffer is null
		if (Irp->AssociatedIrp.SystemBuffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(DUMB_BYTES_STRUCT)) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		//take the struct
		dumbBytesStruct = (PDUMB_BYTES_STRUCT)Irp->AssociatedIrp.SystemBuffer;
		//dbg the process id and the number of bytes
		DbgPrint("Dumb %lu bytes from process ID %lu\n", dumbBytesStruct->n, dumbBytesStruct->processID);
		status  = DumbNBitsToProcess((HANDLE)dumbBytesStruct->processID, dumbBytesStruct->n);
	}
	case(CLEAR_LISTS):
	{
		ClearAllLists();
		break;
	}
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		DbgPrint("Invalid request\n");
		break;
	}
	DbgPrint("DriverIoControl Completed\n");
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	PDEVICE_OBJECT DeviceObject = NULL;
	NTSTATUS status;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\sugiotAv");
	UNICODE_STRING symbolicLink = RTL_CONSTANT_STRING(L"\\??\\sugiotAv");


	status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Failed to create device object (0x%08X)\n", status);
		return status;
	}
	status = IoCreateSymbolicLink(&symbolicLink, &deviceName);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Failed to create symbolic link (0x%08X)\n", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}


	// Get the address of PsGetProcessSectionBaseAddress
	UNICODE_STRING PsGetProcessSectionBaseAddressName = RTL_CONSTANT_STRING(L"PsGetProcessSectionBaseAddress");
	PsGetProcessSectionBaseAddress = (PSGETPROCESSSECTIONBASEADDRESS)MmGetSystemRoutineAddress(&PsGetProcessSectionBaseAddressName);
	if (PsGetProcessSectionBaseAddress == NULL) {
		DbgPrint("Failed to get PsGetProcessSectionBaseAddress address\n");
		IoDeleteSymbolicLink(&symbolicLink);
		IoDeleteDevice(DeviceObject);
		return STATUS_UNSUCCESSFUL;
	}

	//Register the process notification routine
	status = PsSetCreateProcessNotifyRoutineEx(CreateProcessNotificationRoutineEx, FALSE);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to register process notification routine (0x%08X)\n", status);
		IoDeleteSymbolicLink(&symbolicLink);
		IoDeleteDevice(DeviceObject);
		return status;
	}
	
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoControl;
	DriverObject->DriverUnload = AvUnload;

	//Log debug message
	DbgPrint("Driver Loaded\n");
	CheckCurrentIrqlAndAct();

	return STATUS_SUCCESS;
}