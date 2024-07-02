#include <ntddk.h>  // For kernel-mode development
#include <wdm.h>
#define SHUT_DOWN  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CANCEL_SHUTDOWN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMAIN_TIME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
// Global variable to store the time
// Define the SHUTDOWN_ACTION enumeration
typedef enum _SHUTDOWN_ACTION {
	ShutdownNoReboot,
	ShutdownReboot,
	ShutdownPowerOff
} SHUTDOWN_ACTION;

typedef NTSTATUS(*PNT_SHUTDOWN_SYSTEM)(SHUTDOWN_ACTION Action);
// Global variables
KTIMER g_ShutdownTimer;
KDPC g_ShutdownDpc;
LARGE_INTEGER g_ShutdownCallTime;
LARGE_INTEGER g_TimeToShutDown;
BOOLEAN g_TimerSet = FALSE;

void CheckCurrentIrqlAndAct(void) {
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
VOID MyShutdownSystem(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Context);

	DbgPrint("MyShutdownSystem called\n");
	CheckCurrentIrqlAndAct();

	
	UNICODE_STRING routineName = RTL_CONSTANT_STRING(L"NtShutdownSystem");
	PNT_SHUTDOWN_SYSTEM NtShutdownSystem = (PNT_SHUTDOWN_SYSTEM)MmGetSystemRoutineAddress(&routineName);
	
	if (NtShutdownSystem) {
		NTSTATUS status = NtShutdownSystem(ShutdownPowerOff);
		if (NT_SUCCESS(status)) {
			DbgPrint("System shutdown successfully initiated.\n");
		}
		else {
			DbgPrint("NtShutdownSystem failed with status: 0x%08X\n", status);
		}
	}
	else {
		DbgPrint("Failed to get NtShutdownSystem address\n");
	}
	
	g_TimerSet = FALSE;
	// Free the work item
	//IoFreeWorkItem(IoGetCurrentIrpStackLocation(DeviceObject)->Parameters.WorkItem.WorkItem);
}
void pckillerUnload( _In_ PDRIVER_OBJECT DriverObject )
{
	UNREFERENCED_PARAMETER( DriverObject );
	//unload the symbol link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\pckiller");
	//delete symbolic link
	IoDeleteSymbolicLink(&symLink);

	//delete device object
	IoDeleteDevice(DriverObject->DeviceObject);
	//Log debug message
	DbgPrint( "Driver Unloaded\n" );
}
NTSTATUS DriverCreateClose( _In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp )
{
	UNREFERENCED_PARAMETER( DeviceObject );
	//Log debug message
	DbgPrint( "DriverCreateClose Called\n" );
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest( Irp, IO_NO_INCREMENT );
	return STATUS_SUCCESS;
}
NTSTATUS DriverIoControl( _In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp )
{
	UNREFERENCED_PARAMETER( DeviceObject );
	//Log debug message
	DbgPrint( "DriverIoControl Called\n" );
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation( Irp );
	NTSTATUS status = STATUS_SUCCESS;
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case SHUT_DOWN:
	{
		if (Irp->AssociatedIrp.SystemBuffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(LARGE_INTEGER)) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		LARGE_INTEGER* shutdownTime = (LARGE_INTEGER*)Irp->AssociatedIrp.SystemBuffer;
		g_TimeToShutDown.QuadPart = shutdownTime->QuadPart;
		KeQuerySystemTime(&g_ShutdownCallTime);
		DbgPrint("shutdown call time: %lld\n", g_TimeToShutDown.QuadPart);

		KeSetTimer(&g_ShutdownTimer, *shutdownTime, &g_ShutdownDpc);
		g_TimerSet = TRUE;

		DbgPrint("Shutdown timer set for time: %lldu\n", shutdownTime->QuadPart);
		break;
	}
	case REMAIN_TIME:
	{
	if (g_TimerSet) {
				LARGE_INTEGER currentTime, remainingTime;
				KeQuerySystemTime(&currentTime);
				//print the current time
				DbgPrint("Current time: %llu\n", currentTime.QuadPart);
				//g_TimeToShutDown is minus value
				remainingTime.QuadPart = -1 * g_TimeToShutDown.QuadPart - (currentTime.QuadPart - g_ShutdownCallTime.QuadPart);
				if (remainingTime.QuadPart < 0) {
					remainingTime.QuadPart = 0;
				}
				RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &remainingTime, sizeof(LARGE_INTEGER));
				Irp->IoStatus.Information = sizeof(LARGE_INTEGER);
				DbgPrint("Remaining time: %llu\n", remainingTime.QuadPart);
			} else {
				status = STATUS_UNSUCCESSFUL;
				DbgPrint("No shutdown timer is set\n");
			}
		break;

	}
	case CANCEL_SHUTDOWN:
	{
		KeCancelTimer(&g_ShutdownTimer);
		g_TimerSet = FALSE;
		DbgPrint("Shutdown timer canceled\n");
		break;
	}
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			DbgPrint( "Invalid request\n" );
			break;
	}
	DbgPrint( "DriverIoControl Completed\n" );
	Irp->IoStatus.Status = status;
	IoCompleteRequest( Irp, IO_NO_INCREMENT );
	return status;
}
void TimerDpcRoutine(
	_In_ struct _KDPC* Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2
) {
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(DeferredContext);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	DbgPrint("TimerDpcRoutine called\n");
	CheckCurrentIrqlAndAct();

	// Retrieve the DeviceObject from DeferredContext
	PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)DeferredContext;
	// Allocate and queue the work item
	PIO_WORKITEM workItem = IoAllocateWorkItem(DeviceObject);
	if (workItem) {
		IoQueueWorkItem(workItem, MyShutdownSystem, CriticalWorkQueue, NULL);
	}
	else {
		DbgPrint("Failed to allocate work item\n");
	}
}

extern "C" NTSTATUS DriverEntry( _In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath )
{
	UNREFERENCED_PARAMETER( RegistryPath );
	PDEVICE_OBJECT DeviceObject = NULL;
	NTSTATUS status;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING( L"\\Device\\pckiller" );
	UNICODE_STRING symbolicLink = RTL_CONSTANT_STRING( L"\\??\\pckiller" );


	status = IoCreateDevice( DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject );
	if(!NT_SUCCESS( status ) )
	{
		DbgPrint( "Failed to create device object (0x%08X)\n", status );
		return status;
	}
	status = IoCreateSymbolicLink( &symbolicLink, &deviceName );
	if ( !NT_SUCCESS( status ) )
	{
		DbgPrint( "Failed to create symbolic link (0x%08X)\n", status );
		IoDeleteDevice(DeviceObject);
		return status;
	}
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoControl;
	DriverObject->DriverUnload = pckillerUnload;

	// Initialize the timer and DPC
	KeInitializeTimer(&g_ShutdownTimer);
	KeInitializeDpc(&g_ShutdownDpc, TimerDpcRoutine, DeviceObject);

	//Log debug message
	DbgPrint("Driver Loaded\n");
	CheckCurrentIrqlAndAct();

	return STATUS_SUCCESS;
}