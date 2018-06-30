/* libusb-win32, Generic Windows USB Library
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
#include "stub_dev.h"

#include <usbdlib.h>

static NTSTATUS
call_usbd(usbip_stub_dev_t *devstub, void *urb, ULONG control_code)
{
	KEVENT	event;
	IRP *irp;
	IO_STACK_LOCATION	*irpstack;
	IO_STATUS_BLOCK		io_status;
	NTSTATUS status;

	DBGI(DBG_GENERAL, "call_usbd: enter\n");

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(control_code, devstub->next_stack_dev, NULL, 0, NULL, 0, TRUE, &event, &io_status);
	if (irp == NULL) {
		DBGE(DBG_GENERAL, "IoBuildDeviceIoControlRequest failed\n");
		return STATUS_NO_MEMORY;
	}

	irpstack = IoGetNextIrpStackLocation(irp);
	irpstack->Parameters.Others.Argument1 = urb;
	irpstack->Parameters.Others.Argument2 = NULL;

	status = IoCallDriver(devstub->pdo, irp);
	if (status == STATUS_PENDING) {
		LARGE_INTEGER	timeout;
		timeout.QuadPart = -(5000 * 10000);
		if (KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout) == STATUS_TIMEOUT) {
			DBGW(DBG_GENERAL, "call_usbd: timeout\n");
			IoCancelIrp(irp);
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		}
		status = io_status.Status;
	}

	DBGI(DBG_GENERAL, "call_usbd: status = %s, usbd_status:%x\n", dbg_ntstatus(status), ((PURB)urb)->UrbHeader.Status);
	return status;
}

BOOLEAN
get_usb_status(usbip_stub_dev_t *devstub, USHORT op, USHORT idx, PVOID buf, PUCHAR plen)
{
	URB		Urb;
	NTSTATUS	status;

	UsbBuildGetStatusRequest(&Urb, op, idx, buf, NULL, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status)) {
		*plen = (UCHAR)Urb.UrbControlGetStatusRequest.TransferBufferLength;
		return TRUE;
	}
	return FALSE;
}

BOOLEAN
get_usb_desc(usbip_stub_dev_t *devstub, UCHAR descType, UCHAR idx, USHORT idLang, PVOID buff, ULONG *pbufflen)
{
	URB		Urb;
	NTSTATUS	status;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), descType, idx, idLang, buff, NULL, *pbufflen, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status)) {
		*pbufflen = Urb.UrbControlDescriptorRequest.TransferBufferLength;
		return TRUE;
	}
	return FALSE;
}

PUSB_CONFIGURATION_DESCRIPTOR
get_usb_dsc_conf(usbip_stub_dev_t *devstub, UCHAR idx)
{
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	URB		Urb;
	USB_CONFIGURATION_DESCRIPTOR	ConfDesc;
	NTSTATUS	status;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
		USB_CONFIGURATION_DESCRIPTOR_TYPE, idx, 0, &ConfDesc, NULL,
		sizeof(USB_CONFIGURATION_DESCRIPTOR), NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_ERROR(status))
		return NULL;

	dsc_conf = ExAllocatePoolWithTag(NonPagedPool, ConfDesc.wTotalLength, USBIP_STUB_POOL_TAG);
	if (dsc_conf == NULL)
		return NULL;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
		USB_CONFIGURATION_DESCRIPTOR_TYPE, idx, 0, dsc_conf, NULL,
		ConfDesc.wTotalLength, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_ERROR(status)) {
		ExFreePool(dsc_conf);
		return NULL;
	}

	return dsc_conf;
}

static PUSBD_INTERFACE_LIST_ENTRY
build_intf_list(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf)
{
	PUSBD_INTERFACE_LIST_ENTRY	pintf_list;
	PVOID	start;
	int	size;
	int	n_dsc_intf;

	size = sizeof(USBD_INTERFACE_LIST_ENTRY) * (dsc_conf->bNumInterfaces + 1);
	pintf_list = ExAllocatePoolWithTag(NonPagedPool, size, USBIP_STUB_POOL_TAG);
	RtlZeroMemory(pintf_list, size);

	n_dsc_intf = 0;
	start = dsc_conf;
	while (n_dsc_intf < dsc_conf->bNumInterfaces) {
		PUSB_INTERFACE_DESCRIPTOR	dsc_intf;
		dsc_intf = dsc_find_intf(dsc_conf, start);
		if (dsc_intf == NULL)
			break;
		if (dsc_intf->bAlternateSetting == 0) {
			/* add only a interface with 0 alternate value */
			pintf_list[n_dsc_intf].InterfaceDescriptor = dsc_intf;
			n_dsc_intf++;
		}
		start = NEXT_DESC(dsc_intf);
	}
	return pintf_list;
}

BOOLEAN
select_usb_conf(usbip_stub_dev_t *devstub, USHORT idx)
{
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	PURB		purb;
	PUSBD_INTERFACE_LIST_ENTRY	pintf_list;
	NTSTATUS	status;

	dsc_conf = get_usb_dsc_conf(devstub, (UCHAR)idx);
	if (dsc_conf == NULL) {
		DBGE(DBG_GENERAL, "select_usb_conf: non-existent configuration descriptor: index: %hu\n", idx);
		return FALSE;
	}

	pintf_list = build_intf_list(dsc_conf);

	status = USBD_SelectConfigUrbAllocateAndBuild(devstub->hUSBD, dsc_conf, pintf_list, &purb);
	if (NT_ERROR(status)) {
		DBGE(DBG_GENERAL, "select_usb_conf: failed to selectConfigUrb: %s\n", dbg_ntstatus(status));
		ExFreePoolWithTag(pintf_list, USBIP_STUB_POOL_TAG);
		return FALSE;
	}

	status = call_usbd(devstub, purb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status)) {
		struct _URB_SELECT_CONFIGURATION	*purb_selc = &purb->UrbSelectConfiguration;

		if (devstub->devconf)
			free_devconf(devstub->devconf);
		devstub->devconf = create_devconf(purb_selc->ConfigurationDescriptor, purb_selc->ConfigurationHandle, &purb_selc->Interface);
		USBD_UrbFree(devstub->hUSBD, purb);
		ExFreePoolWithTag(pintf_list, USBIP_STUB_POOL_TAG);
		ExFreePoolWithTag(dsc_conf, USBIP_STUB_POOL_TAG);
		return TRUE;
	}
	else {
		DBGI(DBG_GENERAL, "select_usb_conf: failed to select configuration: %s\n", dbg_devstub(devstub));
	}
	USBD_UrbFree(devstub->hUSBD, purb);
	ExFreePoolWithTag(pintf_list, USBIP_STUB_POOL_TAG);
	ExFreePoolWithTag(dsc_conf, USBIP_STUB_POOL_TAG);
	return FALSE;
}

BOOLEAN
select_usb_intf(usbip_stub_dev_t *devstub, devconf_t *devconf, UCHAR intf_num, USHORT alt_setting)
{
	PUSBD_INTERFACE_INFORMATION	info_intf;
	PURB	purb;
	struct _URB_SELECT_INTERFACE	*purb_seli;
	ULONG	len_urb;
	NTSTATUS	status;

	info_intf = get_info_intf(devconf, intf_num);
	if (info_intf == NULL) {
		DBGW(DBG_GENERAL, "select_usb_intf: non-existent interface: num: %hhu, alt:%hu\n", intf_num, alt_setting);
		return FALSE;
	}

	len_urb = sizeof(struct _URB_SELECT_INTERFACE) - sizeof(USBD_INTERFACE_INFORMATION) + INFO_INTF_SIZE(info_intf);
	purb = (PURB)ExAllocatePoolWithTag(NonPagedPool, len_urb, USBIP_STUB_POOL_TAG);
	if (purb == NULL) {
		DBGE(DBG_GENERAL, "select_usb_intf: out of memory\n");
		return FALSE;
	}
	UsbBuildSelectInterfaceRequest(purb, (USHORT)len_urb, devconf->hConf, intf_num, (UCHAR)alt_setting);
	purb_seli = &purb->UrbSelectInterface;
	RtlCopyMemory(&purb_seli->Interface, info_intf, INFO_INTF_SIZE(info_intf));

	status = call_usbd(devstub, purb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	ExFreePoolWithTag(purb, USBIP_STUB_POOL_TAG);
	if (NT_SUCCESS(status)) {
		update_devconf(devconf, info_intf);
		return TRUE;
	}
	return FALSE;
}

BOOLEAN
get_usb_device_desc(usbip_stub_dev_t *devstub, PUSB_DEVICE_DESCRIPTOR pdesc)
{
	ULONG	len = sizeof(USB_DEVICE_DESCRIPTOR);
	return get_usb_desc(devstub, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, pdesc, &len);
}

BOOLEAN
reset_pipe(usbip_stub_dev_t *devstub, USBD_PIPE_HANDLE hPipe)
{
	URB	urb;
	NTSTATUS	status;

	urb.UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
	urb.UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
	urb.UrbPipeRequest.PipeHandle = hPipe;

	status = call_usbd(devstub, &urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status))
		return TRUE;
	return FALSE;
}

BOOLEAN
submit_class_vendor_req(usbip_stub_dev_t *devstub, BOOLEAN is_in, USHORT cmd, UCHAR reservedBits, UCHAR request, USHORT value, USHORT index, PVOID data, ULONG len)
{
	URB		Urb;
	ULONG		flags = 0;
	NTSTATUS	status;

	if (is_in)
		flags |= USBD_TRANSFER_DIRECTION_IN;
	UsbBuildVendorRequest(&Urb, cmd, sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST), flags, reservedBits, request, value, index, data, NULL, len, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status))
		return TRUE;
	return FALSE;
}

BOOLEAN
submit_bulk_transfer(usbip_stub_dev_t *devstub, USBD_PIPE_HANDLE hPipe, PVOID data, USHORT datalen, BOOLEAN is_in)
{
	URB		Urb;
	ULONG		flags = USBD_SHORT_TRANSFER_OK;
	NTSTATUS	status;

	if (is_in)
		flags |= USBD_TRANSFER_DIRECTION_IN;
	UsbBuildInterruptOrBulkTransferRequest(&Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER), hPipe, data, NULL, datalen, flags, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status))
		return TRUE;
	return FALSE;
}