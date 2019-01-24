#include "vhci.h"

#include <wmistr.h>

#include "device.h"
#include "usbip_vhci_api.h"
#include "globals.h"

static WMI_SET_DATAITEM_CALLBACK Bus_SetWmiDataItem;
static WMI_SET_DATABLOCK_CALLBACK Bus_SetWmiDataBlock;
static WMI_QUERY_DATABLOCK_CALLBACK Bus_QueryWmiDataBlock;
static WMI_QUERY_REGINFO_CALLBACK Bus_QueryWmiRegInfo;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Bus_SetWmiDataItem)
#pragma alloc_text(PAGE, Bus_SetWmiDataBlock)
#pragma alloc_text(PAGE, Bus_QueryWmiDataBlock)
#pragma alloc_text(PAGE, Bus_QueryWmiRegInfo)
#endif

#define MOFRESOURCENAME L"USBIPVhciWMI"

#define NUMBER_OF_WMI_GUIDS                 1
#define WMI_USBIP_BUS_DRIVER_INFORMATION  0

static WMIGUIDREGINFO USBIPBusWmiGuidList[] = {
	{ &USBIP_BUS_WMI_STD_DATA_GUID, 1, 0 } // driver information
};

PAGEABLE NTSTATUS
Bus_SystemControl(__in  PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PFDO_DEVICE_DATA	fdoData;
	PCOMMON_DEVICE_DATA	commonData;
	SYSCTL_IRP_DISPOSITION	disposition;
	PIO_STACK_LOCATION	stack;
	NTSTATUS		status;

	PAGED_CODE();

	DBGI(DBG_WMI, "Bus SystemControl\r\n");

	stack = IoGetCurrentIrpStackLocation(Irp);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	if (!commonData->IsFDO) {
		// The PDO, just complete the request with the current status
		DBGI(DBG_WMI, "PDO %s\n", dbg_wmi_minor(stack->MinorFunction));
		status = Irp->IoStatus.Status;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_WMI, "FDO: %s\n", dbg_wmi_minor(stack->MinorFunction));

	Bus_IncIoCount (fdoData);

	if (fdoData->common.DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		Bus_DecIoCount (fdoData);
		return status;
	}

	status = WmiSystemControl(&fdoData->WmiLibInfo, DeviceObject, Irp, &disposition);
	switch(disposition) {
	case IrpProcessed:
		// This irp has been processed and may be completed or pending.
		break;
	case IrpNotCompleted:
		// This irp has not been completed, but has been fully processed.
		// we will complete it now
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		break;
	case IrpForward:
	case IrpNotWmi:
		// This irp is either not a WMI irp or is a WMI irp targetted
		// at a device lower in the stack.
		IoSkipCurrentIrpStackLocation (Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
		break;
	default:
		// We really should never get here, but if we do just forward....
		ASSERT(FALSE);
		IoSkipCurrentIrpStackLocation (Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
		break;
	}

	Bus_DecIoCount (fdoData);

	return(status);
}

// WMI System Call back functions
static NTSTATUS
Bus_SetWmiDataItem(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

	switch(GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		if (DataItemId == 2) {
			requiredSize = sizeof(ULONG);

			if (BufferSize < requiredSize) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			status = STATUS_SUCCESS;
		}
		else {
			status = STATUS_WMI_READ_ONLY;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);

	return status;
}

static NTSTATUS
Bus_SetWmiDataBlock(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

	switch(GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		requiredSize = sizeof(USBIP_BUS_WMI_STD_DATA);

		if (BufferSize < requiredSize) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		status = STATUS_SUCCESS;
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
		break;
	}

	status = WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);

	return(status);
}

static NTSTATUS
Bus_QueryWmiDataBlock(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
	ULONG		size = 0;
	NTSTATUS	status;

	PAGED_CODE();

	// Only ever registers 1 instance per guid
	ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);

		if (OutBufferSize < size) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		*(PUSBIP_BUS_WMI_STD_DATA)Buffer = fdoData->StdUSBIPBusData;
		*InstanceLengthArray = size;
		status = STATUS_SUCCESS;

		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(DeviceObject, Irp, status, size, IO_NO_INCREMENT);

	return status;
}

static NTSTATUS
Bus_QueryWmiRegInfo(__in PDEVICE_OBJECT DeviceObject, __out ULONG *RegFlags, __out PUNICODE_STRING InstanceName,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	PFDO_DEVICE_DATA fdoData;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceName);

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = fdoData->UnderlyingPDO;
	RtlInitUnicodeString(MofResourceName, MOFRESOURCENAME);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
Bus_WmiRegistration(PFDO_DEVICE_DATA FdoData)
{
	NTSTATUS status;

	PAGED_CODE();

	FdoData->WmiLibInfo.GuidCount = sizeof(USBIPBusWmiGuidList) /
		sizeof(WMIGUIDREGINFO);
	ASSERT(NUMBER_OF_WMI_GUIDS == FdoData->WmiLibInfo.GuidCount);
	FdoData->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	FdoData->WmiLibInfo.QueryWmiRegInfo = Bus_QueryWmiRegInfo;
	FdoData->WmiLibInfo.QueryWmiDataBlock = Bus_QueryWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataBlock = Bus_SetWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataItem = Bus_SetWmiDataItem;
	FdoData->WmiLibInfo.ExecuteWmiMethod = NULL;
	FdoData->WmiLibInfo.WmiFunctionControl = NULL;

	// Register with WMI
	status = IoWMIRegistrationControl(FdoData->common.Self, WMIREG_ACTION_REGISTER);

	// Initialize the Std device data structure
	FdoData->StdUSBIPBusData.ErrorCount = 0;

	return status;
}

PAGEABLE NTSTATUS
Bus_WmiDeRegistration(PFDO_DEVICE_DATA FdoData)
{
	PAGED_CODE();

	return IoWMIRegistrationControl(FdoData->common.Self, WMIREG_ACTION_DEREGISTER);
}