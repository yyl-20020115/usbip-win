#include "busenum.h"

#include <usbdi.h>

#include "usbip_proto.h"
#include "usbreq.h"

#define CHECK_SIZE_RW() \
	do { \
		if (buf == NULL) \
			return STATUS_BUFFER_TOO_SMALL; \
		if (req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) { \
			if (len < sizeof(*h)) \
				return STATUS_BUFFER_TOO_SMALL; \
		} else { \
			if (len < sizeof(*h) + req->TransferBufferLength) \
				return STATUS_BUFFER_TOO_SMALL; \
		} \
	} while (0)

#define CHECK_SIZE_READ() \
	do { \
		if (len < sizeof(*h) || buf == NULL) \
			return STATUS_BUFFER_TOO_SMALL; \
	} while (0)

#define USBDEVFS_URB_SHORT_NOT_OK	0x01
#define USBDEVFS_URB_ISO_ASAP		0x02
#define USBDEVFS_URB_NO_FSBR		0x20
#define USBDEVFS_URB_ZERO_PACKET	0x40
#define USBDEVFS_URB_NO_INTERRUPT	0x80

unsigned int
transflag(unsigned int flags)
{
	unsigned int linux_flags = 0;
	if (!(flags&USBD_SHORT_TRANSFER_OK))
		linux_flags |= USBDEVFS_URB_SHORT_NOT_OK;
	if (flags&USBD_START_ISO_TRANSFER_ASAP)
		linux_flags |= USBDEVFS_URB_ISO_ASAP;
	return RtlUlongByteSwap(linux_flags);
}

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    unsigned int direct, USBD_PIPE_HANDLE pipe, unsigned int flags, unsigned int len)
{
	h->base.command = RtlUlongByteSwap(USBIP_CMD_SUBMIT);
	h->base.seqnum = RtlUlongByteSwap(seqnum);
	h->base.devid = RtlUlongByteSwap(devid);
	h->base.direction = RtlUlongByteSwap(direct ? USBIP_DIR_IN : USBIP_DIR_OUT);
	h->base.ep = RtlUlongByteSwap(PIPE2ADDR(pipe));
	h->u.cmd_submit.transfer_flags = transflag(flags);
	h->u.cmd_submit.transfer_buffer_length = RtlUlongByteSwap(len);
	h->u.cmd_submit.start_frame = 0;
	h->u.cmd_submit.number_of_packets = 0;
	h->u.cmd_submit.interval = RtlUlongByteSwap(PIPE2INTERVAL(pipe));
}
