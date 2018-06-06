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
#include "stub_irp.h"
#include "usbip_stub_api.h"

BOOLEAN get_usb_device_desc(usbip_stub_dev_t *devstub, PUSB_DEVICE_DESCRIPTOR pdesc);

static NTSTATUS
process_get_desc(usbip_stub_dev_t *devstub, IRP *irp)
{
	PIO_STACK_LOCATION	irpStack;
	ULONG	outlen;
	NTSTATUS	status = STATUS_SUCCESS;

	irpStack = IoGetCurrentIrpStackLocation(irp);

	outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	irp->IoStatus.Information = 0;
	if (outlen < sizeof(ioctl_usbip_stub_getdesc_t))
		status = STATUS_INVALID_PARAMETER;
	else {
		USB_DEVICE_DESCRIPTOR	desc;

		if (get_usb_device_desc(devstub, &desc)) {
			ioctl_usbip_stub_getdesc_t	*getdesc;
			getdesc = (ioctl_usbip_stub_getdesc_t *)irp->AssociatedIrp.SystemBuffer;
			getdesc->class = desc.bDeviceClass;
			getdesc->subclass = desc.bDeviceSubClass;
			getdesc->protocol = desc.bDeviceProtocol;
			irp->IoStatus.Information = sizeof(ioctl_usbip_stub_getdesc_t);
		}
		else {
			status = STATUS_UNSUCCESSFUL;
		}
	}

	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS
stub_dispatch_ioctl(usbip_stub_dev_t *devstub, IRP *irp)
{
	PIO_STACK_LOCATION	irpStack;
	ULONG			ioctl_code;

	irpStack = IoGetCurrentIrpStackLocation(irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;
	DBGI(DBG_IOCTL, "dispatch_ioctl: code: %s\n", dbg_stub_ioctl_code(ioctl_code));

	switch (ioctl_code) {
	case IOCTL_USBIP_STUB_GET_DESC:
		return process_get_desc(devstub, irp);
	default:
		break;
	}
	return complete_irp(irp, STATUS_SUCCESS, 0);
}
