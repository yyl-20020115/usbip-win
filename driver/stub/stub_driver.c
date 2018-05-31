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

NTSTATUS stub_add_device(PDRIVER_OBJECT drvobj, PDEVICE_OBJECT pdo);
NTSTATUS stub_dispatch(PDEVICE_OBJECT devobj, IRP *irp);

static void
clear_pipe_info(usbip_stub_dev_t *devstub)
{
	UNREFERENCED_PARAMETER(devstub);
	/////TODO RtlZeroMemory(devstub->config.interfaces, sizeof(devstub->config.interfaces));
}

static VOID
stub_unload(DRIVER_OBJECT *drvobj)
{
	UNREFERENCED_PARAMETER(drvobj);

	DBGI(DBG_DISPATCH, "unload\n");
}

NTSTATUS
DriverEntry(DRIVER_OBJECT *drvobj, UNICODE_STRING *regpath)
{
	int i;

	UNREFERENCED_PARAMETER(regpath);

	DBGI(DBG_DISPATCH, "DriverEntry: Enter\n");

	/* initialize the driver object's dispatch table */
	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
		drvobj->MajorFunction[i] = stub_dispatch;
	}

	drvobj->DriverExtension->AddDevice = stub_add_device;
	drvobj->DriverUnload = stub_unload;

	return STATUS_SUCCESS;
}

#if 0

NTSTATUS call_usbd_ex(libusb_device_t *dev, void *urb, ULONG control_code,
					  int timeout, int max_timeout)
{
	KEVENT event;
	NTSTATUS status;
	IRP *irp;
	IO_STACK_LOCATION *next_irp_stack;
	LARGE_INTEGER _timeout;
	IO_STATUS_BLOCK io_status;

	if (max_timeout > 0 && timeout > max_timeout)
	{
		timeout = max_timeout;
	}
	if (timeout <= 0)
		timeout = LIBUSB_MAX_CONTROL_TRANSFER_TIMEOUT;

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(control_code, dev->target_device,
		NULL, 0, NULL, 0, TRUE,
		NULL, &io_status);

	if (!irp)
	{
		return STATUS_NO_MEMORY;
	}

	next_irp_stack = IoGetNextIrpStackLocation(irp);
	next_irp_stack->Parameters.Others.Argument1 = urb;
	next_irp_stack->Parameters.Others.Argument2 = NULL;

	IoSetCompletionRoutine(irp, on_usbd_complete, &event, TRUE, TRUE, TRUE);

	status = IoCallDriver(dev->target_device, irp);
	if(status == STATUS_PENDING)
	{
		_timeout.QuadPart = -(timeout * 10000);

		if(KeWaitForSingleObject(&event, Executive, KernelMode,
			FALSE, &_timeout) == STATUS_TIMEOUT)
		{
			USBERR0("request timed out\n");
			IoCancelIrp(irp);
		}
	}

	/* wait until completion routine is called */
	KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

	status = irp->IoStatus.Status;

	/* complete the request */
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	
	USBDBG("status = %08Xh\n",status);
	return status;
}

static NTSTATUS DDKAPI on_usbd_complete(DEVICE_OBJECT *device_object,
                                        IRP *irp, void *context)
{
    KeSetEvent((KEVENT *) context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}


bool_t get_pipe_handle(libusb_device_t *dev, int endpoint_address,
                       USBD_PIPE_HANDLE *pipe_handle)
{
    int i, j;

    *pipe_handle = NULL;

    for (i = 0; i < LIBUSB_MAX_NUMBER_OF_INTERFACES; i++)
    {
        if (dev->config.interfaces[i].valid)
        {
            for (j = 0; j < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; j++)
            {
                if (dev->config.interfaces[i].endpoints[j].address
                        == endpoint_address)
                {
                    *pipe_handle = dev->config.interfaces[i].endpoints[j].handle;

                    return !*pipe_handle ? FALSE : TRUE;
                }
            }
        }
    }

    return FALSE;
}

bool_t get_pipe_info(libusb_device_t *dev, int endpoint_address,
                       libusb_endpoint_t** pipe_info)
{
    int i, j;

    *pipe_info = NULL;

    for (i = 0; i < LIBUSB_MAX_NUMBER_OF_INTERFACES; i++)
    {
        if (dev->config.interfaces[i].valid)
        {
            for (j = 0; j < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; j++)
            {
                if (dev->config.interfaces[i].endpoints[j].address
                        == endpoint_address)
                {
                    *pipe_info = &dev->config.interfaces[i].endpoints[j];

                    return !*pipe_info ? FALSE : TRUE;
                }
            }
        }
    }

    return FALSE;
}

bool_t update_pipe_info(libusb_device_t *dev,
                        USBD_INTERFACE_INFORMATION *interface_info)
{
    int i;
    int number;
	int maxTransferSize;
	int maxPacketSize;

    if (!interface_info)
    {
        return FALSE;
    }

    number = interface_info->InterfaceNumber;

    if (interface_info->InterfaceNumber >= LIBUSB_MAX_NUMBER_OF_INTERFACES)
    {
        return FALSE;
    }

    USBMSG("interface %d\n", number);

    dev->config.interfaces[number].valid = TRUE;

    for (i = 0; i < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; i++)
    {
        dev->config.interfaces[number].endpoints[i].address = 0;
        dev->config.interfaces[number].endpoints[i].handle = NULL;
    }

    if (interface_info)
    {
        for (i = 0; i < (int)interface_info->NumberOfPipes
                && i < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; i++)
        {
			maxPacketSize = interface_info->Pipes[i].MaximumPacketSize;
			maxTransferSize = interface_info->Pipes[i].MaximumTransferSize;

			USBMSG("EP%02Xh maximum-packet-size=%d maximum-transfer-size=%d\n",
				interface_info->Pipes[i].EndpointAddress,
				maxPacketSize,
				maxTransferSize);

			dev->config.interfaces[number].endpoints[i].handle  = interface_info->Pipes[i].PipeHandle;
            dev->config.interfaces[number].endpoints[i].address = interface_info->Pipes[i].EndpointAddress;
            dev->config.interfaces[number].endpoints[i].maximum_packet_size = maxPacketSize;
            dev->config.interfaces[number].endpoints[i].interval = interface_info->Pipes[i].Interval;
            dev->config.interfaces[number].endpoints[i].pipe_type = interface_info->Pipes[i].PipeType;
 			dev->config.interfaces[number].endpoints[i].pipe_flags = interface_info->Pipes[i].PipeFlags;
          
			if (maxPacketSize)
			{
				// set max the maximum transfer size default to an interval of max packet size.
				maxTransferSize = maxTransferSize - (maxTransferSize % maxPacketSize);
				if (maxTransferSize < maxPacketSize) 
				{
					maxTransferSize = LIBUSB_MAX_READ_WRITE;
				}
				else if (maxTransferSize > LIBUSB_MAX_READ_WRITE)
				{
					maxTransferSize = LIBUSB_MAX_READ_WRITE - (LIBUSB_MAX_READ_WRITE % maxPacketSize);
				}

				if (maxTransferSize != interface_info->Pipes[i].MaximumTransferSize)
				{
					USBWRN("overriding EP%02Xh maximum-transfer-size=%d\n",
						dev->config.interfaces[number].endpoints[i].address,
						maxTransferSize);
				}
			}
			else
			{
				if (!maxTransferSize)
				{
					// use the libusb-win32 default
					maxTransferSize = LIBUSB_MAX_READ_WRITE;
				}
			}
			dev->config.interfaces[number].endpoints[i].maximum_transfer_size = maxTransferSize;
		}
	}
    return TRUE;
}

USB_INTERFACE_DESCRIPTOR *
find_interface_desc(USB_CONFIGURATION_DESCRIPTOR *config_desc,
                    unsigned int size, int interface_number, int altsetting)
{
    usb_descriptor_header_t *desc = (usb_descriptor_header_t *)config_desc;
    char *p = (char *)desc;
    USB_INTERFACE_DESCRIPTOR *if_desc = NULL;

    if (!config_desc || (size < config_desc->wTotalLength))
        return NULL;

    while (size && desc->length <= size)
    {
        if (desc->type == USB_INTERFACE_DESCRIPTOR_TYPE)
        {
            if_desc = (USB_INTERFACE_DESCRIPTOR *)desc;

            if ((if_desc->bInterfaceNumber == (UCHAR)interface_number)
                    && (if_desc->bAlternateSetting == (UCHAR)altsetting))
            {
                return if_desc;
            }
        }

        size -= desc->length;
        p += desc->length;
        desc = (usb_descriptor_header_t *)p;
    }

    return NULL;
}

USB_INTERFACE_DESCRIPTOR* find_interface_desc_ex(USB_CONFIGURATION_DESCRIPTOR *config_desc,
												 unsigned int size,
												 interface_request_t* intf,
												 unsigned int* size_left)
{
#define INTF_FIELD 0
#define ALTF_FIELD 1

	usb_descriptor_header_t *desc = (usb_descriptor_header_t *)config_desc;
    char *p = (char *)desc;
	int lastInfNumber, lastAltNumber;
	int currentInfIndex;
	short InterfacesByIndex[LIBUSB_MAX_NUMBER_OF_INTERFACES][2];

    USB_INTERFACE_DESCRIPTOR *if_desc = NULL;

	memset(InterfacesByIndex,0xFF,sizeof(InterfacesByIndex));

    if (!config_desc)
        return NULL;

	size = size > config_desc->wTotalLength ? config_desc->wTotalLength : size;

    while (size && desc->length <= size)
    {
        if (desc->type == USB_INTERFACE_DESCRIPTOR_TYPE)
        {
			// this is a new interface or alternate interface
            if_desc = (USB_INTERFACE_DESCRIPTOR *)desc;
			for (currentInfIndex=0; currentInfIndex<LIBUSB_MAX_NUMBER_OF_INTERFACES;currentInfIndex++)
			{
				if (InterfacesByIndex[currentInfIndex][INTF_FIELD]==-1)
				{
					// this is a new interface
					InterfacesByIndex[currentInfIndex][INTF_FIELD]=if_desc->bInterfaceNumber;
					InterfacesByIndex[currentInfIndex][ALTF_FIELD]=0;
					break;
				}
				else if (InterfacesByIndex[currentInfIndex][INTF_FIELD]==if_desc->bInterfaceNumber)
				{
					// this is a new alternate interface
					InterfacesByIndex[currentInfIndex][ALTF_FIELD]++;
					break;
				}
			}

			// if the interface index is -1, then we don't care; 
			// i.e. any interface number or index
			if (intf->interface_index!=FIND_INTERFACE_INDEX_ANY)
			{
				if (intf->intf_use_index)
				{
					// looking for a particular interface index; if this is not it then continue on.
					if (intf->interface_index != currentInfIndex)
						goto NextInterface;
				}
				else
				{
					// looking for a particular interface number; if this is not it then continue on.
					if (intf->interface_number != if_desc->bInterfaceNumber)
						goto NextInterface;
				}
			}

			if (intf->altsetting_index!=FIND_INTERFACE_INDEX_ANY)
			{
				if (intf->altf_use_index)
				{
					// looking for a particular alternate interface index; if this is not it then continue on.
					if (intf->altsetting_index != InterfacesByIndex[currentInfIndex][ALTF_FIELD])
						goto NextInterface;
				}
				else
				{
					// looking for a particular alternate interface number; if this is not it then continue on.
					if (intf->altsetting_number != if_desc->bAlternateSetting)
						goto NextInterface;
				}
			}

			// found a match
			intf->interface_index = (unsigned char)currentInfIndex;
			intf->altsetting_index = (unsigned char)InterfacesByIndex[currentInfIndex][ALTF_FIELD];
			intf->interface_number = if_desc->bInterfaceNumber;
			intf->altsetting_number = if_desc->bAlternateSetting;

			if (size_left)
			{
				*size_left=size;
			}
			return if_desc;

        }

NextInterface:
        size -= desc->length;
        p += desc->length;
        desc = (usb_descriptor_header_t *)p;
    }

    return NULL;
}

USB_ENDPOINT_DESCRIPTOR *
find_endpoint_desc_by_index(USB_INTERFACE_DESCRIPTOR *interface_desc,
                    unsigned int size, int pipe_index)
{
    usb_descriptor_header_t *desc = (usb_descriptor_header_t *)interface_desc;
    char *p = (char *)desc;
	int currentPipeIndex;
	short PipesByIndex[LIBUSB_MAX_NUMBER_OF_ENDPOINTS];

    USB_ENDPOINT_DESCRIPTOR *ep_desc = NULL;
	memset(PipesByIndex,0xFF,sizeof(PipesByIndex));

	if (size && desc->length <= size)
	{
        size -= desc->length;
        p += desc->length;
        desc = (usb_descriptor_header_t *)p;
	}

    while (size && desc->length <= size)
    {
        if (desc->type == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            ep_desc = (USB_ENDPOINT_DESCRIPTOR *)desc;
			for (currentPipeIndex=0; currentPipeIndex<LIBUSB_MAX_NUMBER_OF_ENDPOINTS;currentPipeIndex++)
			{
				if (PipesByIndex[currentPipeIndex]==-1)
				{
					PipesByIndex[currentPipeIndex]=ep_desc->bEndpointAddress;
					break;
				}
				else if (PipesByIndex[currentPipeIndex]==ep_desc->bEndpointAddress)
				{
					// the pipe is defined twice in the same interface
					USBWRN("invalid endpoint descriptor at pipe index=%d\n",currentPipeIndex);
					break;
				}
			}

			if (pipe_index == currentPipeIndex)
			{
				return ep_desc;
			}
        }
		else
		{
			break;
		}

        size -= desc->length;
        p += desc->length;
        desc = (usb_descriptor_header_t *)p;
    }

    return NULL;
}


ULONG get_current_frame(IN PDEVICE_EXTENSION deviceExtension, IN PIRP Irp)
/*++

Routine Description:

    This routine send an irp/urb pair with
    function code URB_FUNCTION_GET_CURRENT_FRAME_NUMBER
    to fetch the current frame

Arguments:

    DeviceObject - pointer to device object
    PIRP - I/O request packet

Return Value:

    Current frame

--*/
{
    KEVENT                               event;
    PIO_STACK_LOCATION                   nextStack;
    struct _URB_GET_CURRENT_FRAME_NUMBER urb;

    //
    // initialize the urb
    //

    urb.Hdr.Function = URB_FUNCTION_GET_CURRENT_FRAME_NUMBER;
    urb.Hdr.Length = sizeof(urb);
    urb.FrameNumber = (ULONG) -1;

    nextStack = IoGetNextIrpStackLocation(Irp);
    nextStack->Parameters.Others.Argument1 = (PVOID) &urb;
    nextStack->Parameters.DeviceIoControl.IoControlCode =
                                IOCTL_INTERNAL_USB_SUBMIT_URB;
    nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

    KeInitializeEvent(&event,
                      NotificationEvent,
                      FALSE);

    IoSetCompletionRoutine(Irp,
                           on_usbd_complete,
                           &event,
                           TRUE,
                           TRUE,
                           TRUE);


    IoCallDriver(deviceExtension->target_device, Irp);

    KeWaitForSingleObject((PVOID) &event,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    return urb.FrameNumber;
}
#endif
