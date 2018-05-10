#include "usbreq.h"

#include "busenum.h"

#include <usbdi.h>
#include "usbip_proto.h"
#include "code2name.h"

void
show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe)
{
	KdPrint(("pipe num %d:\n"
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
		pipe->PipeFlags));
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

void
build_setup_packet(struct usb_ctrl_setup *setup,
	unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request)
{
	setup->bRequestType = type << 5;
	if (direct_in)
		setup->bRequestType |= USB_ENDPOINT_DIRECTION_MASK;
	setup->bRequestType |= recip;
	setup->bRequest = request;
}

void *
seek_to_next_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int *offset, unsigned char type)
{
	unsigned int	o = *offset;
	PUSB_COMMON_DESCRIPTOR desc;

	if (config->wTotalLength <= o)
		return NULL;
	do {
		if (o + sizeof(*desc) > config->wTotalLength)
			return NULL;
		desc = (PUSB_COMMON_DESCRIPTOR)((char *)config + o);
		if (desc->bLength + o > config->wTotalLength)
			return NULL;
		o += desc->bLength;
		if (desc->bDescriptorType == type) {
			*offset = o;
			return desc;
		}
	} while (1);
}

void *
seek_to_one_intf_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int *offset, unsigned int num, unsigned int alternatesetting)
{
	do {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;

		intf_desc = seek_to_next_desc(config, offset, USB_INTERFACE_DESCRIPTOR_TYPE);
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
