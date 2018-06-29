#include "vhci.h"

#include <usbdlib.h>

#include "vhci_devconf.h"
#include "usbip_vhci_api.h"
#include "usbip_proto.h"

#define NEXT_USBD_INTERFACE_INFO(info_intf)	(USBD_INTERFACE_INFORMATION *)((PUINT8)(info_intf + 1) + \
		((info_intf)->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION))

#define MAKE_PIPE(ep, type, interval) ((USBD_PIPE_HANDLE)((ep) | ((interval) << 8) | ((type) << 16)))

void
show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe)
{
	DBGI(DBG_GENERAL, "pipe num %d:\n"
	     "MaximumPacketSize: %d\n"
	     "EndpointAddress: 0x%02x\n"
	     "Interval: %d\n"
	     "PipeType: %d\n"
	     "PiPeHandle: 0x%08x\n"
	     "MaximumTransferSize %d\n"
	     "PipeFlags 0x%08x\n", num,
	     pipe->MaximumPacketSize,
	     pipe->EndpointAddress,
	     pipe->Interval,
	     pipe->PipeType,
	     pipe->PipeHandle,
	     pipe->MaximumTransferSize,
	     pipe->PipeFlags);
}

void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, unsigned char speed)
{
	USHORT	mult;
	pipe->MaximumPacketSize = ep_desc->wMaxPacketSize;
	pipe->EndpointAddress = ep_desc->bEndpointAddress;
	pipe->Interval = ep_desc->bInterval;
	pipe->PipeType = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
	/* From usb_submit_urb in linux */
	if (pipe->PipeType == USB_ENDPOINT_TYPE_ISOCHRONOUS && speed == USB_SPEED_HIGH) {
		mult = 1 + ((pipe->MaximumPacketSize >> 11) & 0x03);
		pipe->MaximumPacketSize &= 0x7ff;
		pipe->MaximumPacketSize *= mult;
	}
	pipe->PipeHandle = MAKE_PIPE(ep_desc->bEndpointAddress, pipe->PipeType, ep_desc->bInterval);
}

static PUSB_INTERFACE_DESCRIPTOR
find_dsc_intf_by_handle(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, USBD_INTERFACE_HANDLE handle)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf = (PUSB_INTERFACE_DESCRIPTOR)dsc_conf;
	unsigned int	i;

	for (i = 0; i < (UINT_PTR)handle; i++) {
		dsc_intf = (PUSB_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, dsc_intf, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (dsc_intf == NULL)
			return NULL;
		dsc_intf = NEXT_DESC_INTF(dsc_intf);
	}
	return (PUSB_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, dsc_intf, USB_INTERFACE_DESCRIPTOR_TYPE);
}

static NTSTATUS
setup_endpoints(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PVOID start, UCHAR speed)
{
	unsigned int	i;

	for (i = 0; i < intf->NumberOfPipes; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	dsc_ep;

		show_pipe(i, &intf->Pipes[i]);

		dsc_ep = (PUSB_ENDPOINT_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_ENDPOINT_DESCRIPTOR_TYPE);
		if (dsc_ep == NULL) {
			DBGW(DBG_IOCTL, "no ep desc\n");
			return FALSE;
		}

		set_pipe(&intf->Pipes[i], dsc_ep, speed);
		show_pipe(i, &intf->Pipes[i]);
		start = NEXT_DESC(dsc_ep);
	}
	return TRUE;
}

static NTSTATUS
setup_intf(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR speed)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf;

	if (sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION) > intf->Length) {
		DBGE(DBG_URB, "insufficient interface information size?\n");
		///TODO: need to check
		return STATUS_SUCCESS;
	}

	dsc_intf = USBD_ParseConfigurationDescriptorEx(dsc_conf, dsc_conf, intf->InterfaceNumber, intf->AlternateSetting, -1, -1, -1);
	if (dsc_intf == NULL) {
		DBGW(DBG_IOCTL, "no interface desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (dsc_intf->bNumEndpoints != intf->NumberOfPipes) {
		DBGW(DBG_IOCTL, "number of pipes is not same:(%d,%d)\n", dsc_intf->bNumEndpoints, intf->NumberOfPipes);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (intf->NumberOfPipes > 0) {
		if (sizeof(USBD_INTERFACE_INFORMATION) + (intf->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION) > intf->Length) {
			DBGE(DBG_URB, "insufficient interface information size\n");
			return STATUS_INVALID_PARAMETER;
		}
	}

	intf->Class = dsc_intf->bInterfaceClass;
	intf->SubClass = dsc_intf->bInterfaceSubClass;
	intf->Protocol = dsc_intf->bInterfaceProtocol;

	if (!setup_endpoints(intf, dsc_conf, dsc_intf, speed))
		return STATUS_INVALID_DEVICE_REQUEST;
	return STATUS_SUCCESS;
}

NTSTATUS
select_config(struct _URB_SELECT_CONFIGURATION *urb_selc, UCHAR speed)
{
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	PUSBD_INTERFACE_INFORMATION	info_intf;
	unsigned int	i;

	/* it has no means */
	urb_selc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678;

	dsc_conf = urb_selc->ConfigurationDescriptor;
	info_intf = &urb_selc->Interface;
	for (i = 0; i < urb_selc->ConfigurationDescriptor->bNumInterfaces; i++) {
		NTSTATUS	status;

		if ((status = setup_intf(info_intf, dsc_conf, speed)) != STATUS_SUCCESS)
			return status;

		info_intf->InterfaceHandle = (USBD_INTERFACE_HANDLE)(i + 1);
		info_intf = NEXT_USBD_INTERFACE_INFO(info_intf);
	}

	/* it seems we must return now */
	return STATUS_SUCCESS;
}

NTSTATUS
select_interface(struct _URB_SELECT_INTERFACE *urb_seli, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR speed)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf;
	PUSBD_INTERFACE_INFORMATION	info_intf;

	info_intf = &urb_seli->Interface;

	dsc_intf = find_dsc_intf_by_handle(dsc_conf, info_intf->InterfaceHandle);
	if (dsc_intf == NULL) {
		DBGW(DBG_URB, "non-existent interface: handle: %d", (int)(UINT_PTR)info_intf->InterfaceHandle);
		return STATUS_INVALID_PARAMETER;
	}
	return setup_intf(info_intf, dsc_conf, speed);
}
