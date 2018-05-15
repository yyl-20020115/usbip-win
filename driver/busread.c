#include "driver.h"

#include "usbip_proto.h"
#include "usbreq.h"

extern struct urb_req *
find_pending_urb_req(PPDO_DEVICE_DATA pdodata);

extern void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
	unsigned int direct, USBD_PIPE_HANDLE pipe, unsigned int flags, unsigned int len);

struct usbip_header *
get_usbip_hdr_from_read_irp(PIRP irp, ULONG len)
{
	PIO_STACK_LOCATION	irpstack;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	if (irpstack->Parameters.Read.Length < len) {
		return NULL;
	}
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

static NTSTATUS
store_urb_reset_dev(PIRP irp, struct urb_req *urb_r)
{
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;

	hdr = get_usbip_hdr_from_read_irp(irp, sizeof(struct usbip_header));
	if (hdr == NULL) {
		irp->IoStatus.Information = 0;
		return STATUS_BUFFER_TOO_SMALL;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, 0, 0, 0, 0);

	build_setup_packet(setup, 0, BMREQUEST_CLASS, BMREQUEST_TO_OTHER, USB_REQUEST_SET_FEATURE);
	setup->wLength = 0;
	setup->wValue = 4; // Reset
	setup->wIndex = 0;

	irp->IoStatus.Information = sizeof(struct usbip_header);

	return STATUS_SUCCESS;
}

static PVOID
get_buf(PVOID buf, PMDL bufMDL)
{
	if (buf == NULL) {
		if (bufMDL != NULL)
			buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
		if (buf == NULL) {
			DBGE(DBG_READ, "No transfer buffer\n");
		}
	}
	return buf;
}

static NTSTATUS
store_urb_get_dev_desc(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, sizeof(struct usbip_header));
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	build_setup_packet(setup, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);

	setup->wLength = (unsigned short)urb_desc->TransferBufferLength;
	setup->wValue = (urb_desc->DescriptorType << 8) | urb_desc->Index;

	switch (urb_desc->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		setup->wIndex = 0;
		break;
	case USB_INTERFACE_DESCRIPTOR_TYPE:
		setup->wIndex = urb_desc->Index;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		setup->wIndex = urb_desc->LanguageId;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_get_intf_desc(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, sizeof(struct usbip_header));
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	build_setup_packet(setup, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_DESCRIPTOR);

	setup->wLength = (unsigned short)urb_desc->TransferBufferLength;
	setup->wValue = (urb_desc->DescriptorType << 8) | urb_desc->Index;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_class_vendor(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vc = &urb->UrbControlVendorClassRequest;
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;
	char	in, type, recip;
	int	len;

	in = urb_vc->TransferFlags & USBD_TRANSFER_DIRECTION_IN;
	len = sizeof(struct usbip_header);
	if (!in)
		len += urb_vc->TransferBufferLength;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, len);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	switch (urb_vc->Hdr.Function) {
	case URB_FUNCTION_CLASS_DEVICE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_CLASS_INTERFACE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_CLASS_ENDPOINT:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_CLASS_OTHER:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_OTHER;
		break;
	case URB_FUNCTION_VENDOR_DEVICE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_VENDOR_INTERFACE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_VENDOR_ENDPOINT:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_VENDOR_OTHER:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, in, 0,
				    urb_vc->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_vc->TransferBufferLength);
	build_setup_packet(setup, in, type, recip, urb_vc->Request);
	//FIXME what is the usage of RequestTypeReservedBits?
	setup->wLength = (unsigned short)urb_vc->TransferBufferLength;
	setup->wValue = urb_vc->Value;
	setup->wIndex = urb_vc->Index;

	if (!in) {
		RtlCopyMemory(hdr + 1, urb_vc->TransferBuffer, urb_vc->TransferBufferLength);
	}
	irp->IoStatus.Information = len;
	return  STATUS_SUCCESS;
}

static NTSTATUS
store_urb_select_config(PIRP irp, struct urb_req *urb_r)
{
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, sizeof(struct usbip_header));
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, 0, 0, 0, 0);
	build_setup_packet(setup, 0, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	setup->wLength = 0;
	setup->wValue = 1;
	setup->wIndex = 0;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_select_interface(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_SELECT_INTERFACE	*urb_si = &urb->UrbSelectInterface;
	struct usbip_header	*hdr;
	struct usb_ctrl_setup	*setup;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, sizeof(struct usbip_header));
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	setup = (struct usb_ctrl_setup *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, 0, 0, 0, 0);
	build_setup_packet(setup, 0, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	setup->wLength = 0;
	setup->wValue = urb_si->Interface.AlternateSetting;
	setup->wIndex = urb_si->Interface.InterfaceNumber;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return  STATUS_SUCCESS;
}

static NTSTATUS
store_urb_bulk(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*urb_bi = &urb->UrbBulkOrInterruptTransfer;
	struct usbip_header	*hdr;
	BOOLEAN	in;
	int	type;
	int	len;

	in = (urb_bi->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ? TRUE: FALSE;
	type = PIPE2TYPE(urb_bi->PipeHandle);

	len = sizeof(struct usbip_header);
	if (!in)
		len += urb_bi->TransferBufferLength;

	irp->IoStatus.Information = 0;

	hdr = get_usbip_hdr_from_read_irp(irp, len);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (type != USB_ENDPOINT_TYPE_BULK && type != USB_ENDPOINT_TYPE_INTERRUPT) {
		DBGE(DBG_READ, "Error, not a bulk pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid, in, urb_bi->PipeHandle,
				    urb_bi->TransferFlags, urb_bi->TransferBufferLength);
	RtlZeroMemory(hdr->u.cmd_submit.setup, 8);
	if (!in) {
		PVOID	buf = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
		if (buf == NULL)
			return STATUS_INSUFFICIENT_RESOURCES;
		RtlCopyMemory(hdr + 1, buf, urb_bi->TransferBufferLength);
	}
	irp->IoStatus.Information = len;
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_iso(PIRP irp, PURB urb, struct urb_req *urb_r)
{
	struct _URB_ISOCH_TRANSFER	*urb_iso = &urb->UrbIsochronousTransfer;
	struct usbip_header	*hdr;
	struct usbip_iso_packet_descriptor	*iso_desc;
	int	in, type;
	ULONG	i, offset;
	int	len;
	char	*buf;

	irp->IoStatus.Information = 0;

	in = PIPE2DIRECT(urb_iso->PipeHandle);
	type = PIPE2TYPE(urb_iso->PipeHandle);
	if (type != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
		DBGE(DBG_READ, "Error, not a iso pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	len = sizeof(struct usbip_header) + urb_iso->NumberOfPackets * sizeof(struct usbip_iso_packet_descriptor);
	if (!(urb_iso->TransferFlags & USBD_TRANSFER_DIRECTION_IN)) {
		len += urb_iso->TransferBufferLength;
	}

	hdr = get_usbip_hdr_from_read_irp(irp, len);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urb_r->seq_num, urb_r->pdodata->devid,
				    in, urb_iso->PipeHandle, urb_iso->TransferFlags | USBD_SHORT_TRANSFER_OK,
				    urb_iso->TransferBufferLength);
	hdr->u.cmd_submit.start_frame = RtlUlongByteSwap(urb_iso->StartFrame);
	hdr->u.cmd_submit.number_of_packets = RtlUlongByteSwap(urb_iso->NumberOfPackets);

	buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	if (!in) {
		RtlCopyMemory(hdr + 1, buf, urb_iso->TransferBufferLength);
	}

	iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + urb_iso->TransferBufferLength);
	offset = 0;
	for (i = 0; i < urb_iso->NumberOfPackets; i++) {
		if (urb_iso->IsoPacket[i].Offset < offset) {
			DBGW(DBG_READ, "strange iso packet offset:%d %d", offset, urb_iso->IsoPacket[i].Offset);
			return STATUS_INVALID_PARAMETER;
		}
		iso_desc->offset = RtlUlongByteSwap(urb_iso->IsoPacket[i].Offset);
		if (i > 0)
			(iso_desc - 1)->length = RtlUlongByteSwap(urb_iso->IsoPacket[i].Offset - offset);
		offset = urb_iso->IsoPacket[i].Offset;
		iso_desc->actual_length = 0;
		iso_desc->status = 0;
		iso_desc++;
	}
	(iso_desc - 1)->length = RtlUlongByteSwap(urb_iso->TransferBufferLength - offset);

	irp->IoStatus.Information = len;
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_req_submit(PIRP irp, struct urb_req *urb_r)
{
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USHORT		code_func;
	NTSTATUS	status;

	DBGI(DBG_READ, "process_urb_req_submit: urb_r: %s\n", dbg_urb_req(urb_r));

	irpstack = IoGetCurrentIrpStackLocation(urb_r->irp);
	urb = irpstack->Parameters.Others.Argument1;
	if (urb == NULL) {
		DBGE(DBG_READ, "process_urb_req_submit: null urb\n");

		irp->IoStatus.Information = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	code_func = urb->UrbHeader.Function;
	DBGI(DBG_READ, "process_urb_req_submit: urb_r: %s, func:%s\n", dbg_urb_req(urb_r), dbg_urbfunc(code_func));

	switch (code_func) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk(irp, urb, urb_r);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso(irp, urb, urb_r);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urb_get_dev_desc(irp, urb, urb_r);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		status = store_urb_get_intf_desc(irp, urb, urb_r);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urb_class_vendor(irp, urb, urb_r);
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		status = store_urb_select_config(irp, urb_r);
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		status = store_urb_select_interface(irp, urb, urb_r);
		break;
	default:
		irp->IoStatus.Information = 0;
		DBGE(DBG_READ, "unhandled urb function: %s\n", dbg_urbfunc(code_func));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

NTSTATUS
process_urb_req(PIRP irp, struct urb_req *urb_r)
{
	PIO_STACK_LOCATION	irpstack;
	ULONG		ioctl_code;
	NTSTATUS	status;

	DBGI(DBG_READ, "process_urb_req: urb_r: %s\n", dbg_urb_req(urb_r));

	irpstack = IoGetCurrentIrpStackLocation(urb_r->irp);
	ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;
	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_urb_req_submit(irp, urb_r);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = store_urb_reset_dev(irp, urb_r);
		break;
	default:
		DBGW(DBG_READ, "unhandled ioctl: %s\n", dbg_ioctl_code(ioctl_code));
		irp->IoStatus.Information = 0;
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static NTSTATUS
process_read_irp(PPDO_DEVICE_DATA pdodata, PIRP read_irp)
{
	struct urb_req	*urb_r;
	KIRQL	oldirql;
	NTSTATUS status = STATUS_PENDING;

	KeAcquireSpinLock(&pdodata->q_lock, &oldirql);

	urb_r = find_pending_urb_req(pdodata);
	if (urb_r == NULL) {
		if (pdodata->pending_read_irp)
			status = STATUS_INVALID_DEVICE_REQUEST;
		else {
			IoMarkIrpPending(read_irp);
			pdodata->pending_read_irp = read_irp;
		}
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		return status;
	}

	status = process_urb_req(read_irp, urb_r);
	if (status == STATUS_SUCCESS || !IoSetCancelRoutine(urb_r->irp, NULL)) {
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		return status;
	}
	/* set_read_irp failed, we must complete ioctl_irp */
	RemoveEntryList(&urb_r->list);
	KeReleaseSpinLock(&pdodata->q_lock, oldirql);

	urb_r->irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	IoCompleteRequest(urb_r->irp, IO_NO_INCREMENT);
	ExFreeToNPagedLookasideList(&g_lookaside, urb_r);

	return status;
}

PAGEABLE NTSTATUS
Bus_Read(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PFDO_DEVICE_DATA	fdoData;
	PPDO_DEVICE_DATA	pdodata;
	PCOMMON_DEVICE_DATA     commonData;
	PIO_STACK_LOCATION	stackirp;
	NTSTATUS		status;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_READ, "Bus_Read: Enter\n");

	if (!commonData->IsFDO) {
		DBGE(DBG_READ, "read for fdo is not allowed\n");

		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//
	if (fdoData->common.DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}
	stackirp = IoGetCurrentIrpStackLocation(Irp);
	pdodata = stackirp->FileObject->FsContext;
	if (pdodata == NULL || !pdodata->Present) {
		status = STATUS_INVALID_DEVICE_REQUEST;
	}
	else if (pdodata->pending_read_irp) {
		status = STATUS_INVALID_PARAMETER;
	}
	else {
		status = process_read_irp(pdodata, Irp);
	}
END:
	DBGI(DBG_GENERAL | DBG_READ, "Bus_Read: Leave: %s\n", dbg_ntstatus(status));
	if (status != STATUS_PENDING) {
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}
	Bus_DecIoCount(fdoData);
	return status;
}
