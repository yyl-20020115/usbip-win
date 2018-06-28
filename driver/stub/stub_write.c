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
#include "pdu.h"

#define HDR_IS_CONTROL_TRANSFER(hdr)	((hdr)->base.ep == 0)

static NTSTATUS
not_supported(PIRP irp)
{
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_NOT_SUPPORTED;
}

static void
process_get_status(usbip_stub_dev_t *devstub, unsigned int seqnum, usb_cspkt_t *csp)
{
	USHORT	op, idx = 0;
	USHORT	data;
	UCHAR	datalen;

	DBGI(DBG_READWRITE, "get_status\n");

	switch (CSPKT_RECIPIENT(csp)) {
	case BMREQUEST_TO_DEVICE:
		op = URB_FUNCTION_GET_STATUS_FROM_DEVICE;
		break;
	case BMREQUEST_TO_INTERFACE:
		op = URB_FUNCTION_GET_STATUS_FROM_INTERFACE;
		idx = csp->wIndex.W;
		break;
	case BMREQUEST_TO_ENDPOINT:
		op = URB_FUNCTION_GET_STATUS_FROM_ENDPOINT;
		idx = csp->wIndex.W;
		break;
	default:
		op = URB_FUNCTION_GET_STATUS_FROM_OTHER;
		break;
	}
	if (get_usb_status(devstub, op, idx, &data, &datalen))
		reply_stub_req_data(devstub, seqnum, &data, (int)datalen, FALSE);
	else
		reply_stub_req_err(devstub, seqnum, -1);
}

static void
process_get_desc(usbip_stub_dev_t *devstub, unsigned int seqnum, usb_cspkt_t *csp)
{
	UCHAR	descType = CSPKT_DESCRIPTOR_TYPE(csp);
	USHORT	idLang = 0;
	PVOID	pdesc = NULL;
	ULONG	len;

	DBGI(DBG_READWRITE, "get_desc: %s\n", dbg_cspkt_desctype(CSPKT_DESCRIPTOR_TYPE(csp)));

	pdesc = ExAllocatePoolWithTag(NonPagedPool, csp->wLength, USBIP_STUB_POOL_TAG);
	if (pdesc == NULL) {
		DBGE(DBG_READWRITE, "process_get_desc: out of memory\n");
		reply_stub_req_err(devstub, seqnum, -1);
		return;
	}
	if (descType == USB_STRING_DESCRIPTOR_TYPE)
		idLang = csp->wIndex.W;
	len = csp->wLength;
	if (!get_usb_desc(devstub, descType, CSPKT_DESCRIPTOR_INDEX(csp), idLang, pdesc, &len)) {
		DBGW(DBG_READWRITE, "process_get_desc: failed to get descriptor\n");
		ExFreePool(pdesc);
		reply_stub_req_err(devstub, seqnum, -1);
		return;
	}
	reply_stub_req_data(devstub, seqnum, pdesc, len, FALSE);
}

static void
process_select_conf(usbip_stub_dev_t *devstub, unsigned int seqnum, usb_cspkt_t *csp)
{
	if (select_usb_conf(devstub, csp->wValue.W))
		reply_stub_req(devstub, seqnum);
	else
		reply_stub_req_err(devstub, seqnum, -1);
}

static void
process_standard_request(usbip_stub_dev_t *devstub, unsigned int seqnum, usb_cspkt_t *csp)
{
	switch (csp->bRequest) {
	case USB_REQUEST_GET_STATUS:
		process_get_status(devstub, seqnum, csp);
		break;
	case USB_REQUEST_GET_DESCRIPTOR:
		process_get_desc(devstub, seqnum, csp);
		break;
	case USB_REQUEST_SET_CONFIGURATION:
		process_select_conf(devstub, seqnum, csp);
		break;
	case USB_REQUEST_SET_INTERFACE:
		break;
	default:
		break;
	}
}

static void
process_class_request(usbip_stub_dev_t *devstub, usb_cspkt_t *csp, struct usbip_header *hdr)
{
	PVOID	data;
	ULONG	datalen;
	USHORT	cmd;
	UCHAR	reservedBits;
	unsigned long	seqnum;
	BOOLEAN	is_in, res;

	datalen = hdr->u.cmd_submit.transfer_buffer_length;
	if (datalen == 0)
		data = NULL;
	else {
		data = (PVOID)(hdr + 1);
	}

	is_in = csp->bmRequestType.Dir ? TRUE : FALSE;

	switch (csp->bmRequestType.Recipient) {
	case BMREQUEST_TO_DEVICE:
		cmd = URB_FUNCTION_CLASS_DEVICE;
		break;
	case BMREQUEST_TO_INTERFACE:
		cmd = URB_FUNCTION_CLASS_INTERFACE;
		break;
	case BMREQUEST_TO_ENDPOINT:
		cmd = URB_FUNCTION_CLASS_ENDPOINT;
		break;
	default:
		cmd = URB_FUNCTION_CLASS_OTHER;
		break;
	}

	reservedBits = csp->bmRequestType.Reserved;
	seqnum = hdr->base.seqnum;
	res = submit_class_vendor_req(devstub, is_in, cmd, reservedBits, csp->bRequest, csp->wValue.W, csp->wIndex.W, data, datalen);
	if (res) {
		if (is_in)
			reply_stub_req_data(devstub, seqnum, data, datalen, TRUE);
		else
			reply_stub_req(devstub, seqnum);
	}
	else
		reply_stub_req_err(devstub, seqnum, -1);
}

static NTSTATUS
process_control_transfer(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	usb_cspkt_t	*csp;
	UCHAR		reqType;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	DBGI(DBG_READWRITE, "dispatch_write: hdr: %s, csp: %s\n", dbg_usbip_hdr(hdr), dbg_ctlsetup_packet(csp));

	reqType = CSPKT_REQUEST_TYPE(csp);
	switch (reqType) {
	case BMREQUEST_STANDARD:
		process_standard_request(devstub, hdr->base.seqnum, csp);
		return STATUS_SUCCESS;
	case BMREQUEST_CLASS:
		process_class_request(devstub, csp, hdr);
		return STATUS_SUCCESS;
	case BMREQUEST_VENDOR:
		return STATUS_NOT_SUPPORTED;
	default:
		DBGE(DBG_READWRITE, "invalid request type:", dbg_cspkt_reqtype(reqType));
		return STATUS_UNSUCCESSFUL;
	}
}

static NTSTATUS
process_bulk_transfer(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	USBD_PIPE_HANDLE	hPipe = NULL;
	PVOID	data;
	USHORT	datalen;
	BOOLEAN	is_in;

	datalen = (USHORT)hdr->u.cmd_submit.transfer_buffer_length;
	is_in = hdr->base.direction ? TRUE : FALSE;
	if (is_in) {
		data = ExAllocatePoolWithTag(NonPagedPool, datalen, USBIP_STUB_POOL_TAG);
		if (data == NULL) {
			DBGE(DBG_GENERAL, "process_bulk_transfer: out of memory\n");
			reply_stub_req_err(devstub, hdr->base.seqnum, -1);
			return STATUS_UNSUCCESSFUL;
		}
	}
	else {
		data = (PVOID)(hdr + 1);
	}
	if (submit_bulk_transfer(devstub, hPipe, data, datalen, is_in)) {
		if (is_in)
			reply_stub_req_data(devstub, hdr->base.seqnum, data, datalen, FALSE);
		else
			reply_stub_req(devstub, hdr->base.seqnum);
		return STATUS_SUCCESS;
	}
	reply_stub_req_err(devstub, hdr->base.seqnum, -1);
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS
process_data_transfer(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	if (is_iso_transfer(devstub->devconfs, hdr->base.ep, hdr->base.direction ? TRUE: FALSE)) {
		
	}
	else {
		return process_bulk_transfer(devstub, hdr);
	}
	return STATUS_NOT_SUPPORTED;
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
	ULONG		len;
	NTSTATUS	status;

	DBGI(DBG_GENERAL | DBG_READWRITE, "dispatch_write: enter\n");

	hdr = get_usbip_hdr_from_write_irp(irp, &len);
	if (hdr == NULL) {
		DBGE(DBG_READWRITE, "small write irp\n");
		return STATUS_INVALID_PARAMETER;
	}

	if (HDR_IS_CONTROL_TRANSFER(hdr)) {
		status = process_control_transfer(devstub, hdr);
	}
	else {
		status = process_data_transfer(devstub, hdr);
	}

	if (NT_SUCCESS(status))
		irp->IoStatus.Information = len;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}
