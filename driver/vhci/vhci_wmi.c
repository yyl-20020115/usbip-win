#include "vhci.h"

#include <wmistr.h>

#include "vhci_dev.h"
#include "usbip_vhci_api.h"
#include "globals.h"

static WMI_SET_DATAITEM_CALLBACK	vhci_SetWmiDataItem;
static WMI_SET_DATABLOCK_CALLBACK	vhci_SetWmiDataBlock;
static WMI_QUERY_DATABLOCK_CALLBACK	vhci_QueryWmiDataBlock;
static WMI_QUERY_REGINFO_CALLBACK	vhci_QueryWmiRegInfo;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, vhci_SetWmiDataItem)
#pragma alloc_text(PAGE, vhci_SetWmiDataBlock)
#pragma alloc_text(PAGE, vhci_QueryWmiDataBlock)
#pragma alloc_text(PAGE, vhci_QueryWmiRegInfo)
#endif

#define MOFRESOURCENAME L"USBIPVhciWMI"

#define NUMBER_OF_WMI_GUIDS                 1
#define WMI_USBIP_BUS_DRIVER_INFORMATION  0

static WMIGUIDREGINFO USBIPBusWmiGuidList[] = {
	{ &USBIP_BUS_WMI_STD_DATA_GUID, 1, 0 } // driver information
};

PAGEABLE NTSTATUS
vhci_system_control(__in  PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pusbip_vhub_dev_t	vhub;
	pdev_common_t		devcom;
	SYSCTL_IRP_DISPOSITION	disposition;
	PIO_STACK_LOCATION	stack;
	NTSTATUS		status;

	PAGED_CODE();

	DBGI(DBG_WMI, "vhci_system_control: Enter\n");

	stack = IoGetCurrentIrpStackLocation(Irp);

	devcom = (pdev_common_t)devobj->DeviceExtension;

	if (!devcom->is_vhub) {
		// The vpdo, just complete the request with the current status
		DBGI(DBG_WMI, "vpdo %s\n", dbg_wmi_minor(stack->MinorFunction));
		status = Irp->IoStatus.Status;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		return status;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	DBGI(DBG_WMI, "vhci: %s\n", dbg_wmi_minor(stack->MinorFunction));

	inc_io_vhub(vhub);

	if (vhub->common.DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		dec_io_vhub(vhub);
		return status;
	}

	status = WmiSystemControl(&vhub->WmiLibInfo, devobj, Irp, &disposition);
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
		status = IoCallDriver(vhub->NextLowerDriver, Irp);
		break;
	default:
		// We really should never get here, but if we do just forward....
		ASSERT(FALSE);
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(vhub->NextLowerDriver, Irp);
		break;
	}

	dec_io_vhub(vhub);

	return status;
}

// WMI System Call back functions
static NTSTATUS
vhci_SetWmiDataItem(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	pusbip_vhub_dev_t	vhub;
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	switch (GuidIndex) {
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

	status = WmiCompleteRequest(devobj, Irp, status, requiredSize, IO_NO_INCREMENT);

	return status;
}

static NTSTATUS
vhci_SetWmiDataBlock(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	pusbip_vhub_dev_t	vhub;
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

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

	status = WmiCompleteRequest(devobj, Irp, status, requiredSize, IO_NO_INCREMENT);

	return(status);
}

static NTSTATUS
vhci_QueryWmiDataBlock(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	pusbip_vhub_dev_t	vhub;
	ULONG		size = 0;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(InstanceCount);

	PAGED_CODE();

	// Only ever registers 1 instance per guid
	ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);

		if (OutBufferSize < size) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		*(PUSBIP_BUS_WMI_STD_DATA)Buffer = vhub->StdUSBIPBusData;
		*InstanceLengthArray = size;
		status = STATUS_SUCCESS;

		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(devobj, Irp, status, size, IO_NO_INCREMENT);

	return status;
}

static NTSTATUS
vhci_QueryWmiRegInfo(__in PDEVICE_OBJECT devobj, __out ULONG *RegFlags, __out PUNICODE_STRING InstanceName,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	pusbip_vhub_dev_t	vhub;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceName);

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = vhub->UnderlyingPDO;
	RtlInitUnicodeString(MofResourceName, MOFRESOURCENAME);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
reg_wmi(pusbip_vhub_dev_t vhub)
{
	NTSTATUS	status;

	PAGED_CODE();

	vhub->WmiLibInfo.GuidCount = sizeof(USBIPBusWmiGuidList) /
		sizeof(WMIGUIDREGINFO);
	ASSERT(NUMBER_OF_WMI_GUIDS == vhub->WmiLibInfo.GuidCount);
	vhub->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	vhub->WmiLibInfo.QueryWmiRegInfo = vhci_QueryWmiRegInfo;
	vhub->WmiLibInfo.QueryWmiDataBlock = vhci_QueryWmiDataBlock;
	vhub->WmiLibInfo.SetWmiDataBlock = vhci_SetWmiDataBlock;
	vhub->WmiLibInfo.SetWmiDataItem = vhci_SetWmiDataItem;
	vhub->WmiLibInfo.ExecuteWmiMethod = NULL;
	vhub->WmiLibInfo.WmiFunctionControl = NULL;

	// Register with WMI
	status = IoWMIRegistrationControl(vhub->common.Self, WMIREG_ACTION_REGISTER);

	// Initialize the Std device data structure
	vhub->StdUSBIPBusData.ErrorCount = 0;

	return status;
}

PAGEABLE NTSTATUS
dereg_wmi(pusbip_vhub_dev_t vhub)
{
	PAGED_CODE();

	return IoWMIRegistrationControl(vhub->common.Self, WMIREG_ACTION_DEREGISTER);
}