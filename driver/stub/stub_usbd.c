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

	irp = IoBuildDeviceIoControlRequest(control_code, devstub->pdo, NULL, 0, NULL, 0, TRUE, &event, &io_status);
	if (irp == NULL) {
		DBGE(DBG_GENERAL, "IoBuildDeviceIoControlRequest failed\n");
		return STATUS_NO_MEMORY;
	}

	irpstack = IoGetNextIrpStackLocation(irp);
	irpstack->Parameters.Others.Argument1 = urb;
	irpstack->Parameters.Others.Argument2 = NULL;

	status = IoCallDriver(devstub->pdo, irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = io_status.Status;
	}

	DBGI(DBG_GENERAL, "call_usbd: status = %s\n", dbg_ntstatus(status));
	return status;
}

BOOLEAN
get_usb_device_desc(usbip_stub_dev_t *devstub, PUSB_DEVICE_DESCRIPTOR pdesc)
{
	URB		Urb;
	NTSTATUS	status;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, pdesc, NULL, sizeof(USB_DEVICE_DESCRIPTOR), NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_SUCCESS(status))
		return TRUE;
	return FALSE;
}

PUSB_CONFIGURATION_DESCRIPTOR
get_usb_conf_desc(usbip_stub_dev_t *devstub, UCHAR idx)
{
	PUSB_CONFIGURATION_DESCRIPTOR	desc;
	URB		Urb;
	USB_CONFIGURATION_DESCRIPTOR	ConfDesc;
	NTSTATUS	status;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
				     USB_CONFIGURATION_DESCRIPTOR_TYPE, idx, 0, &ConfDesc, NULL,
				     sizeof(USB_CONFIGURATION_DESCRIPTOR), NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_ERROR(status))
		return NULL;

	desc = ExAllocatePoolWithTag(NonPagedPool, ConfDesc.wTotalLength, USBIP_STUB_POOL_TAG);
	if (desc == NULL)
		return NULL;

	UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
				     USB_CONFIGURATION_DESCRIPTOR_TYPE, idx, 0, desc, NULL,
				     ConfDesc.wTotalLength, NULL);
	status = call_usbd(devstub, &Urb, IOCTL_INTERNAL_USB_SUBMIT_URB);
	if (NT_ERROR(status)) {
		ExFreePool(desc);
		return NULL;
	}

	return desc;
}
