/* libusb-win32, Generic Windows USB Library
* Copyright (c) 2010 Travis Robinson <libusbdotnet@gmail.com>
* Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "stub_driver.h"
#include "stub_dbg.h"
#include "usbip_proto.h"
#include "stub_cspkt.h"
#include "stub_usbd.h"
#include "stub_req.h"

static void
process_get_status(usbip_stub_dev_t *devstub, unsigned long seqnum, usb_cspkt_t *csp)
{
	USHORT	op, idx = 0;
	USHORT	data;

	DBGI(DBG_READWRITE, "get_status\n");

	switch (CSPKT_RECIPIENT(csp)) {
	case BMREQUEST_TO_DEVICE:
		op = URB_FUNCTION_GET_STATUS_FROM_DEVICE;
		break;
	case BMREQUEST_TO_INTERFACE:
		op = URB_FUNCTION_GET_STATUS_FROM_INTERFACE;
		break;
	case BMREQUEST_TO_ENDPOINT:
		op = URB_FUNCTION_GET_STATUS_FROM_ENDPOINT;
		break;
	default:
		op = URB_FUNCTION_GET_STATUS_FROM_OTHER;
		break;
	}
	if (get_usb_status(devstub, op, idx, &data))
		reply_stub_req_data(devstub, seqnum, &data, 2);
	else
		reply_stub_req_err(devstub, seqnum, -1);
}

static void
process_get_desc(usbip_stub_dev_t *devstub, unsigned long seqnum, usb_cspkt_t *csp)
{
	UCHAR	descType = CSPKT_DESCRIPTOR_TYPE(csp);
	USHORT	idLang = 0;
	PVOID	pdesc = NULL;

	DBGI(DBG_READWRITE, "get_desc: %s\n", dbg_cspkt_desctype(CSPKT_DESCRIPTOR_TYPE(csp)));

	pdesc = ExAllocatePoolWithTag(NonPagedPool, csp->wLength, USBIP_STUB_POOL_TAG);
	if (pdesc == NULL) {
		DBGE(DBG_READWRITE, "process_get_desc: out of memory\n");
		reply_stub_req_err(devstub, seqnum, -1);
		return;
	}
	if (descType == USB_STRING_DESCRIPTOR_TYPE)
		idLang = csp->wIndex.W;
	if (!get_usb_desc(devstub, descType, CSPKT_DESCRIPTOR_INDEX(csp), idLang, pdesc, csp->wLength)) {
		DBGW(DBG_READWRITE, "process_get_desc: failed to get descriptor\n");
		ExFreePool(pdesc);
		reply_stub_req_err(devstub, seqnum, -1);
		return;
	}
	reply_stub_req_data(devstub, seqnum, pdesc, csp->wLength);
	ExFreePool(pdesc);
}

static void
process_select_conf(usbip_stub_dev_t *devstub, unsigned long seqnum, usb_cspkt_t *csp)
{
	if (select_usb_conf(devstub, csp->wValue.W))
		reply_stub_req(devstub, seqnum);
	else
		reply_stub_req_err(devstub, seqnum, -1);
}

static NTSTATUS
process_standard_request(usbip_stub_dev_t *devstub, unsigned long seqnum, usb_cspkt_t *csp)
{
	switch (csp->bRequest) {
	case USB_REQUEST_GET_STATUS:
		process_get_status(devstub, seqnum, csp);
		return STATUS_SUCCESS;
	case USB_REQUEST_GET_DESCRIPTOR:
		process_get_desc(devstub, seqnum, csp);
		return STATUS_SUCCESS;
	case USB_REQUEST_SET_CONFIGURATION:
		process_select_conf(devstub, seqnum, csp);
		return STATUS_SUCCESS;
	case USB_REQUEST_SET_INTERFACE:
		break;
	default:
		break;
	}
	return STATUS_INVALID_PARAMETER;
}

static struct usbip_header *
get_usbip_hdr_from_write_irp(PIRP irp, ULONG *plen)
{
	PIO_STACK_LOCATION	irpstack;
	ULONG	len;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	len = irpstack->Parameters.Write.Length;
	if (len < sizeof(struct usbip_header)) {
		return NULL;
	}
	*plen = len;
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

NTSTATUS
stub_dispatch_write(usbip_stub_dev_t *devstub, IRP *irp)
{
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;
	UCHAR		reqType;
	ULONG		len;
	NTSTATUS	status = STATUS_UNSUCCESSFUL;///TODO

	DBGI(DBG_GENERAL | DBG_READWRITE, "dispatch_write: enter\n");

	hdr = get_usbip_hdr_from_write_irp(irp, &len);
	if (hdr == NULL) {
		DBGE(DBG_READWRITE, "small write irp\n");
		return STATUS_INVALID_PARAMETER;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	DBGI(DBG_READWRITE, "dispatch_write: csp: %s\n", dbg_ctlsetup_packet(csp));

	reqType = CSPKT_REQUEST_TYPE(csp);
	switch (reqType) {
	case BMREQUEST_STANDARD:
		status = process_standard_request(devstub, hdr->base.seqnum, csp);
		break;
	case BMREQUEST_CLASS:
		break;
	case BMREQUEST_VENDOR:
		break;
	default:
		DBGE(DBG_READWRITE, "invalid request type:", dbg_cspkt_reqtype(reqType));
		break;
	}

	if (NT_SUCCESS(status))
		irp->IoStatus.Information = len;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}