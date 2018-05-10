#include "busenum.h"

#include <usbdi.h>

#include "code2name.h"
#include "usbreq.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Bus_IoCtl)
#endif

extern NTSTATUS
submit_urb_req(PPDO_DEVICE_DATA pdodata, PIRP Irp);

static NTSTATUS
process_urb_select_config(PPDO_DEVICE_DATA pdodata, PURB urb)
{
	struct _URB_SELECT_CONFIGURATION	*urb_sel = &urb->UrbSelectConfiguration;
	USBD_INTERFACE_INFORMATION	*intf;
	unsigned int	i;
	unsigned int	offset = 0;

	if (pdodata->dev_config == NULL) {
		DBGW(DBG_IOCTL, "select config when have no get config\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (urb_sel->ConfigurationDescriptor == NULL) {
		DBGI(DBG_IOCTL, "Device unconfigured\n");
		return STATUS_SUCCESS;
	}
	if (!RtlEqualMemory(pdodata->dev_config, urb_sel->ConfigurationDescriptor, sizeof(*urb_sel->ConfigurationDescriptor))) {
		DBGW(DBG_IOCTL, "Warning, not the same config desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	/* it has no means */
	urb_sel->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678;
	intf = &urb_sel->Interface;
	for (i = 0; i < urb_sel->ConfigurationDescriptor->bNumInterfaces; i++) {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;
		unsigned int	j;

		if ((char *)intf + sizeof(*intf) - sizeof(intf->Pipes[0]) - (char *)urb_sel > urb_sel->Hdr.Length) {
			DBGW(DBG_IOCTL, "not all interface select\n");
			return STATUS_SUCCESS;
		}
		intf_desc = seek_to_one_intf_desc((PUSB_CONFIGURATION_DESCRIPTOR)pdodata->dev_config,
			&offset, intf->InterfaceNumber, intf->AlternateSetting);
		if (intf_desc == NULL) {
			DBGW(DBG_IOCTL, "no interface desc\n");
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		if (intf_desc->bNumEndpoints != intf->NumberOfPipes) {
			DBGW(DBG_IOCTL, "number of pipes is no same%d %d\n", intf_desc->bNumEndpoints, intf->NumberOfPipes);
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		if (intf->NumberOfPipes > 0) {
			if ((char *)intf + sizeof(*intf) + (intf->NumberOfPipes - 1) * sizeof(intf->Pipes[0]) - (char *)urb_sel
			    > urb_sel->Hdr.Length) {
				DBGW(DBG_IOCTL, "small for select config\n");
				return STATUS_INVALID_PARAMETER;
			}
		}
		if (intf->InterfaceNumber != i || intf->AlternateSetting != 0) {
			DBGW(DBG_IOCTL, "Warning, I don't expect this");
			return STATUS_INVALID_PARAMETER;
		}
		intf->Class = intf_desc->bInterfaceClass;
		intf->SubClass = intf_desc->bInterfaceSubClass;
		intf->Protocol = intf_desc->bInterfaceProtocol;
		/* it has no means */
		intf->InterfaceHandle = (USBD_INTERFACE_HANDLE)0x12345678;
		for (j = 0; j<intf->NumberOfPipes; j++) {
			PUSB_ENDPOINT_DESCRIPTOR	ep_desc;

			show_pipe(j, &intf->Pipes[j]);

			ep_desc = seek_to_next_desc(
				(PUSB_CONFIGURATION_DESCRIPTOR)pdodata->dev_config,
				&offset, USB_ENDPOINT_DESCRIPTOR_TYPE);

			if (ep_desc == NULL) {
				DBGW(DBG_IOCTL, "no ep desc\n");
				return STATUS_INVALID_DEVICE_REQUEST;
			}

			set_pipe(&intf->Pipes[j], ep_desc, pdodata->speed);
			show_pipe(j, &intf->Pipes[j]);
		}
		intf = (USBD_INTERFACE_INFORMATION *)((char *)intf + sizeof(*intf) + (intf->NumberOfPipes - 1) *
			sizeof(intf->Pipes[0]));
	}
	/* it seems we must return now */
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_reset_pipe(PPDO_DEVICE_DATA pdodata)
{
	UNREFERENCED_PARAMETER(pdodata);

	////TODO need to check
	DBGI(DBG_IOCTL, "reset_pipe:\n");
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_get_frame(PPDO_DEVICE_DATA pdodata, PURB urb)
{
	struct _URB_GET_CURRENT_FRAME_NUMBER	*urb_get = &urb->UrbGetCurrentFrameNumber;
	UNREFERENCED_PARAMETER(pdodata);

	urb_get->FrameNumber = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
process_irp_urb_req(PPDO_DEVICE_DATA pdodata, PIRP irp, PURB urb)
{
	if (urb == NULL) {
		ERROR(("process_irp_urb: null urb"));
		return STATUS_INVALID_PARAMETER;
	}

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_SELECT_CONFIGURATION:
		DBGI(DBG_IOCTL, "select configuration\n");
		return process_urb_select_config(pdodata, urb);
	case URB_FUNCTION_RESET_PIPE:
		return process_urb_reset_pipe(pdodata);
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		return process_urb_get_frame(pdodata, urb);
	case URB_FUNCTION_ISOCH_TRANSFER:
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
	case URB_FUNCTION_SELECT_INTERFACE:
		return submit_urb_req(pdodata, irp);
	default:
		DBGW(DBG_IOCTL, "Unknown function:%x %d\n", urb->UrbHeader.Function, urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}
}

NTSTATUS
Bus_Internal_IoCtl(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS		status;
	PPDO_DEVICE_DATA	pdoData;
	PCOMMON_DEVICE_DATA	commonData;
	ULONG			ioctl_code;

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Internal_Ioctl: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "ioctl code: %s\n", code2name(ioctl_code));

	if (commonData->IsFDO) {
		DBGW(DBG_IOCTL, "internal ioctl for fdo is not allowed\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	pdoData = (PPDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	if (!pdoData->Present) {
		DBGW(DBG_IOCTL, "device is not connected\n");
		Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_DEVICE_NOT_CONNECTED;
	}

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(pdoData, Irp, (PURB)irpStack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = STATUS_SUCCESS;
		*(unsigned long *)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urb_req(pdoData, Irp);
		break;
	default:
		DBGE(DBG_IOCTL, "unknown ioctl code: %x", ioctl_code);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Internal_Ioctl: Leave: %08x\n", status);
	return status;
}

NTSTATUS
Bus_IoCtl(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS			status;
	ULONG				inlen, outlen;
	ULONG				info = 0;
	PFDO_DEVICE_DATA	fdoData;
	PVOID				buffer;
	PCOMMON_DEVICE_DATA	commonData;
	ULONG				ioctl_code;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Ioctl: Enter\n");

	//
	// We only allow create/close requests for the FDO.
	// That is the bus itself.
	//
	if (!commonData->IsFDO) {
		DBGE(DBG_IOCTL, "ioctl for fdo is not allowed\n");

		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;
	DBGI(DBG_IOCTL, "ioctl code: %s\n", code2name(ioctl_code));

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//
	if (fdoData->common.DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	buffer = Irp->AssociatedIrp.SystemBuffer;
	inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

	status = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_USBVBUS_PLUGIN_HARDWARE:
		if (sizeof(ioctl_usbvbus_plugin) == inlen) {
			status = bus_plugin_dev((ioctl_usbvbus_plugin *)buffer, fdoData, irpStack->FileObject);
		}
		break;
	case IOCTL_USBVBUS_GET_PORTS_STATUS:
		if (sizeof(ioctl_usbvbus_get_ports_status) == outlen) {
			status = bus_get_ports_status((ioctl_usbvbus_get_ports_status *)buffer, fdoData, &info);
		}
		break;
	case IOCTL_USBVBUS_UNPLUG_HARDWARE:
		if (sizeof(ioctl_usbvbus_unplug) == inlen) {
			status = bus_unplug_dev(((ioctl_usbvbus_unplug *)buffer)->addr, fdoData);
		}
		break;
	case IOCTL_USBVBUS_EJECT_HARDWARE:
		if (inlen == sizeof(BUSENUM_EJECT_HARDWARE) && ((PBUSENUM_EJECT_HARDWARE)buffer)->Size == inlen) {
			status = Bus_EjectDevice((PBUSENUM_EJECT_HARDWARE)buffer, fdoData);
		}
		break;
	default:
		break;
	}

	Irp->IoStatus.Information = info;
END:
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	Bus_DecIoCount(fdoData);
	return status;
}
