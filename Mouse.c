#include "ntddk.h"
#include "stdarg.h"
#include "stdio.h"
#include "ntddkbd.h"
#include "Ntddmou.h"

PDEVICE_OBJECT  MouseDevice;
PDEVICE_OBJECT  MouseFilterDevice;
PFILE_OBJECT    MouseFileObject;


DRIVER_ADD_DEVICE AddDevice;
//NTSTATUS MouseInit(IN PDRIVER_OBJECT DriverObject);
VOID MouseUnload(IN PDRIVER_OBJECT DriverObject);
NTSTATUS MouseDispatchRead(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS MouseDispatchPnp(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS MouseDispatchGeneral(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);


NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT   DriverObject,
	IN PUNICODE_STRING  RegistryPath)
{
	DbgPrint("MafMouse: Entering DriverEntry()\n");

	DriverObject->DriverExtension->AddDevice = AddDevice;
	DriverObject->DriverUnload = MouseUnload;
#if 1
	DriverObject->MajorFunction[IRP_MJ_READ]
		= MouseDispatchRead;
	DriverObject->MajorFunction[IRP_MJ_PNP]
		= MouseDispatchPnp;
	DriverObject->MajorFunction[IRP_MJ_CREATE] =
		DriverObject->MajorFunction[IRP_MJ_CLOSE] =
		DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] =
		DriverObject->MajorFunction[IRP_MJ_CLEANUP] =
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]
		= MouseDispatchGeneral;

	//return MouseInit(DriverObject);
#endif
	return STATUS_SUCCESS;
}

NTSTATUS AddDevice(
	_In_ struct _DRIVER_OBJECT *DriverObject,
	_In_ struct _DEVICE_OBJECT *PhysicalDeviceObject
	)
{
	CCHAR           ntNameBuffer[64];
	STRING          ntNameString;
	UNICODE_STRING  ntUnicodeString;
	NTSTATUS        ntStatus;

	sprintf(ntNameBuffer, "\\Device\\PointerClass0");
	RtlInitAnsiString(&ntNameString, ntNameBuffer);
	RtlAnsiStringToUnicodeString(&ntUnicodeString, &ntNameString, TRUE);

	ntStatus = IoCreateDevice(
		DriverObject,
		0,
		NULL,
		FILE_DEVICE_MOUSE,
		0,
		FALSE,
		&MouseFilterDevice);

	if (!NT_SUCCESS(ntStatus)) {
		DbgPrint("MafMouse: failed to create device\n");
		RtlFreeUnicodeString(&ntUnicodeString);
		return STATUS_SUCCESS;
	}

	MouseFilterDevice->Flags |= DO_BUFFERED_IO;

#if 1
	ntStatus = IoGetDeviceObjectPointer(
		&ntUnicodeString,
		STANDARD_RIGHTS_ALL,
		&MouseFileObject,
		&MouseDevice);

	if (!NT_SUCCESS(ntStatus)) {
		DbgPrint("MafMouse: failed to get top device object\n");
		IoDeleteDevice(MouseFilterDevice);
		RtlFreeUnicodeString(&ntUnicodeString);
		return STATUS_SUCCESS;
	}
#endif

	MouseDevice = IoAttachDeviceToDeviceStack(
		MouseFilterDevice,
		PhysicalDeviceObject);
	if (MouseDevice == NULL){
		DbgPrint("MafMouse: failed to attach device object\n");
	}

	RtlFreeUnicodeString(&ntUnicodeString);

	DbgPrint("MafMouse: Successfully connected to mouse device\n");

	return STATUS_SUCCESS;
}


NTSTATUS MouseReadCompletionRoutine(
	IN PDEVICE_OBJECT   DeviceObject,
	IN PIRP             Irp,
	IN PVOID            Context)
{
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
	PMOUSE_INPUT_DATA MouseData;

	//DbgPrint("Handle ReadComplete\n");

	if (NT_SUCCESS(Irp->IoStatus.Status)) {
		MouseData = Irp->AssociatedIrp.SystemBuffer;

		if (MouseData->Flags & MOUSE_MOVE_ABSOLUTE) {
			DbgPrint("Absolute: Y = %ld\n", MouseData->LastY);
			//MouseData->LastX = 0xFFFF - MouseData->LastX;
			MouseData->LastY = 0xFFFF - MouseData->LastY;
		}
		else {
			DbgPrint("Relative: Y = %ld\n", MouseData->LastY);
			//MouseData->LastX = -MouseData->LastX;
			MouseData->LastY = -MouseData->LastY;
		}
	}

	if (Irp->PendingReturned) {
		IoMarkIrpPending(Irp);
	}

	return Irp->IoStatus.Status;
}


NTSTATUS MouseDispatchPnp(
	IN PDEVICE_OBJECT   DeviceObject,
	IN PIRP             Irp)
{
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

	DbgPrint("DispatchPNP: Mj=%x Mn=%x\n", stack->MajorFunction, stack->MinorFunction);

	switch (stack->MinorFunction){
	case IRP_MN_STOP_DEVICE:
		DbgPrint("Removing device");
		IoDetachDevice(MouseDevice);
		//ObDereferenceObject(MouseFileObject);
		IoDeleteDevice(MouseFilterDevice);
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	}
	
	IoSkipCurrentIrpStackLocation(Irp);
	NTSTATUS st = IoCallDriver(MouseDevice, Irp);
	DbgPrint("CallDriverStatus 0x%x\n", st);
	return st;

	//PIO_STACK_LOCATION nextIrpStack = IoGetNextIrpStackLocation(Irp);
	//Irp->IoStatus.Status = STATUS_SUCCESS;
	//Irp->IoStatus.Information = 0;
	/*
	if (DeviceObject == MouseFilterDevice) {
		*nextIrpStack = *stack;
		NTSTATUS st = IoCallDriver(MouseDevice, Irp);
		DbgPrint("CallDriverStatus 0x%x\n", st);
		if (st != STATUS_PENDING)
			;
		return st;
	}

	DbgPrint("FAIL1");
	return STATUS_SUCCESS;
	*/
}


NTSTATUS MouseDispatchRead(
	IN PDEVICE_OBJECT   DeviceObject,
	IN PIRP             Irp)
{
	PIO_STACK_LOCATION currentIrpStack = IoGetCurrentIrpStackLocation(Irp);
	PIO_STACK_LOCATION nextIrpStack = IoGetNextIrpStackLocation(Irp);

	DbgPrint("Dispatch read\n");

	*nextIrpStack = *currentIrpStack;
	IoSetCompletionRoutine(
		Irp,
		MouseReadCompletionRoutine,
		DeviceObject,
		TRUE,
		TRUE,
		TRUE);

	return IoCallDriver(MouseDevice, Irp);
}


NTSTATUS MouseDispatchGeneral(
	IN PDEVICE_OBJECT   DeviceObject,
	IN PIRP             Irp)
{
	PIO_STACK_LOCATION currentIrpStack = IoGetCurrentIrpStackLocation(Irp);
	PIO_STACK_LOCATION nextIrpStack = IoGetNextIrpStackLocation(Irp);

	DbgPrint("Dispatch general\n");

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	if (DeviceObject == MouseFilterDevice) {
		*nextIrpStack = *currentIrpStack;
		return IoCallDriver(MouseDevice, Irp);
	}

	return STATUS_SUCCESS;
}

VOID MouseUnload(
	IN PDRIVER_OBJECT DriverObject)
{
	DbgPrint("MafMouse: unload driver\n");
}
