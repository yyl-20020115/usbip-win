#include "vhci.h"

#include "devconf.h"
#include "usbip_vhci_api.h"
#include "usbip_proto.h"

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

devconf_t
alloc_devconf_from_urb(struct _URB_CONTROL_DESCRIPTOR_REQUEST *urb_desc)
{
	PUSB_CONFIGURATION_DESCRIPTOR	cfg;
	devconf_t	devconf;
	int	len;

	if (urb_desc->DescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
		return NULL;

	cfg = (PUSB_CONFIGURATION_DESCRIPTOR)urb_desc->TransferBuffer;
	len = urb_desc->TransferBufferLength;
	if (len < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
		DBGE(DBG_GENERAL, "bad descriptor length\n");
		return NULL;
	}
	if (cfg->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE || cfg->wTotalLength != len) {
		DBGI(DBG_GENERAL, "not full cfg: dropped\n");
		return NULL;
	}

	devconf = ExAllocatePoolWithTag(NonPagedPool, len, USBIP_VHCI_POOL_TAG);
	if (devconf == NULL) {
		DBGE(DBG_GENERAL, "out of memory");
		return NULL;
	}

	RtlCopyMemory(devconf, urb_desc->TransferBuffer, len);
	return devconf;
}

PUSB_COMMON_DESCRIPTOR
find_usbconf_desc(devconf_t devconf, unsigned int *poffset, unsigned char type)
{
	unsigned int	offset = *poffset;

	do {
		PUSB_COMMON_DESCRIPTOR	desc;

		if (devconf->wTotalLength <= offset + sizeof(USB_COMMON_DESCRIPTOR))
			return NULL;

		desc = (PUSB_COMMON_DESCRIPTOR)((PUINT8)devconf + offset);
		if (devconf->wTotalLength < offset + desc->bLength)
			return NULL;

		offset += desc->bLength;
		if (desc->bDescriptorType == type) {
			*poffset = offset;
			return desc;
		}
	} while (TRUE);
}

PUSB_INTERFACE_DESCRIPTOR
find_intf_desc(devconf_t devconf, unsigned int *poffset, unsigned int num, unsigned int alternatesetting)
{
	do {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;

		intf_desc = (PUSB_INTERFACE_DESCRIPTOR)find_usbconf_desc(devconf, poffset, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (intf_desc == NULL)
			break;
		if (intf_desc->bInterfaceNumber < num)
			continue;
		if (intf_desc->bInterfaceNumber > num)
			break;
		if (intf_desc->bAlternateSetting < alternatesetting)
			continue;
		if (intf_desc->bAlternateSetting > alternatesetting)
			break;
		return intf_desc;
	} while (1);

	return NULL;
}

static PUSB_INTERFACE_DESCRIPTOR
find_intf_desc_by_handle(devconf_t devconf, USBD_INTERFACE_HANDLE handle)
{
	unsigned int	offset = 0;
	unsigned int	i;

	for (i = 0; i < (UINT_PTR)handle; i++) {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;

		intf_desc = (PUSB_INTERFACE_DESCRIPTOR)find_usbconf_desc(devconf, &offset, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (intf_desc == NULL)
			return NULL;
	}
	return (PUSB_INTERFACE_DESCRIPTOR)find_usbconf_desc(devconf, &offset, USB_INTERFACE_DESCRIPTOR_TYPE);
}

static NTSTATUS
setup_endpoints(USBD_INTERFACE_INFORMATION *intf, devconf_t devconf, unsigned int *poffset, UCHAR speed)
{
	unsigned int	i;

	for (i = 0; i < intf->NumberOfPipes; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	ep_desc;

		show_pipe(i, &intf->Pipes[i]);

		ep_desc = (PUSB_ENDPOINT_DESCRIPTOR)find_usbconf_desc(devconf, poffset, USB_ENDPOINT_DESCRIPTOR_TYPE);

		if (ep_desc == NULL) {
			DBGW(DBG_IOCTL, "no ep desc\n");
			return FALSE;
		}

		set_pipe(&intf->Pipes[i], ep_desc, speed);
		show_pipe(i, &intf->Pipes[i]);
	}
	return TRUE;
}

static NTSTATUS
setup_intf(USBD_INTERFACE_INFORMATION *intf, ULONG len, devconf_t devconf, unsigned int *poffset, UCHAR speed)
{
	PUSB_INTERFACE_DESCRIPTOR	intf_desc;

	if (sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION) > len) {
		DBGE(DBG_URB, "insufficient interface information size?\n");
		///TODO: need to check
		return STATUS_SUCCESS;
	}

	intf_desc = find_intf_desc(devconf, poffset, intf->InterfaceNumber, intf->AlternateSetting);
	if (intf_desc == NULL) {
		DBGW(DBG_IOCTL, "no interface desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (intf_desc->bNumEndpoints != intf->NumberOfPipes) {
		DBGW(DBG_IOCTL, "number of pipes is not same:(%d,%d)\n", intf_desc->bNumEndpoints, intf->NumberOfPipes);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (intf->NumberOfPipes > 0) {
		if (sizeof(USBD_INTERFACE_INFORMATION) + (intf->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION) > len) {
			DBGE(DBG_URB, "insufficient interface information size\n");
			return STATUS_INVALID_PARAMETER;
		}
	}

	intf->Class = intf_desc->bInterfaceClass;
	intf->SubClass = intf_desc->bInterfaceSubClass;
	intf->Protocol = intf_desc->bInterfaceProtocol;

	if (!setup_endpoints(intf, devconf, poffset, speed))
		return STATUS_INVALID_DEVICE_REQUEST;
	return STATUS_SUCCESS;
}

NTSTATUS
select_config(struct _URB_SELECT_CONFIGURATION *urb_selc, devconf_t devconf, UCHAR speed)
{
	USBD_INTERFACE_INFORMATION	*intf;
	unsigned int	offset = 0;
	unsigned int	i;

	if (!RtlEqualMemory(devconf, urb_selc->ConfigurationDescriptor, sizeof(struct _USB_CONFIGURATION_DESCRIPTOR))) {
		DBGW(DBG_URB, "select_devconf: not the same config desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	/* it has no means */
	urb_selc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678;

	intf = &urb_selc->Interface;
	for (i = 0; i < urb_selc->ConfigurationDescriptor->bNumInterfaces; i++) {
		NTSTATUS	status;
		ULONG	len;

		if (intf->InterfaceNumber != i || intf->AlternateSetting != 0) {
			DBGW(DBG_URB, "Unexpected case: idx: %d, iface no: %d, alternate: %d",
			     i, intf->InterfaceNumber, intf->AlternateSetting);
			return STATUS_INVALID_PARAMETER;
		}

		len = (ULONG)(urb_selc->Hdr.Length - ((PUINT8)intf - (PUINT8)urb_selc));
		if ((status = setup_intf(intf, len, devconf, &offset, speed)) != STATUS_SUCCESS)
			return status;

		intf->InterfaceHandle = (USBD_INTERFACE_HANDLE)(i + 1);

		/*
		 * get next interface information.
		 *
		 * (# of pipes) - 1 means that USBD_INTERFACE_INFORMATION already has
		 * one USBD_ENDPOINT_INFORMATION: (# of pipes - 1)
		 */
		intf = (USBD_INTERFACE_INFORMATION *)((PUINT8)(intf + 1) + (intf->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION));
	}

	/* it seems we must return now */
	return STATUS_SUCCESS;
}

NTSTATUS
select_interface(struct _URB_SELECT_INTERFACE *urb_seli, devconf_t devconf, UCHAR speed)
{
	PUSB_INTERFACE_DESCRIPTOR	intf_desc;
	USBD_INTERFACE_INFORMATION	*intf;
	unsigned int	offset;

	intf = &urb_seli->Interface;

	intf_desc = find_intf_desc_by_handle(devconf, intf->InterfaceHandle);
	if (intf_desc == NULL) {
		DBGW(DBG_URB, "non-existent interface: handle: %d", (int)(UINT_PTR)intf->InterfaceHandle);
		return STATUS_INVALID_PARAMETER;
	}
	offset = (unsigned int)((PUINT8)intf_desc - (PUINT8)devconf);
	return setup_intf(intf, intf->Length, devconf, &offset, speed);
}
