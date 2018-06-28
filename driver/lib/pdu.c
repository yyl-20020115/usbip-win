#include "pdu.h"

static void
swap_cmd_submit(struct usbip_header_cmd_submit *cmd_submit)
{
	cmd_submit->transfer_flags = RtlUlongByteSwap(cmd_submit->transfer_flags);
	cmd_submit->transfer_buffer_length = RtlUlongByteSwap(cmd_submit->transfer_buffer_length);
	cmd_submit->start_frame = RtlUlongByteSwap(cmd_submit->start_frame);
	cmd_submit->number_of_packets = RtlUlongByteSwap(cmd_submit->number_of_packets);
	cmd_submit->interval = RtlUlongByteSwap(cmd_submit->interval);
}

static void
swap_ret_submit(struct usbip_header_ret_submit *ret_submit)
{
	ret_submit->status = RtlUlongByteSwap(ret_submit->status);
	ret_submit->actual_length = RtlUlongByteSwap(ret_submit->actual_length);
	ret_submit->start_frame = RtlUlongByteSwap(ret_submit->start_frame);
	ret_submit->number_of_packets = RtlUlongByteSwap(ret_submit->number_of_packets);
	ret_submit->error_count = RtlUlongByteSwap(ret_submit->error_count);
}

void
swap_usbip_header(struct usbip_header *hdr)
{
	hdr->base.command = RtlUlongByteSwap(hdr->base.command);
	hdr->base.seqnum = RtlUlongByteSwap(hdr->base.seqnum);
	hdr->base.devid = RtlUlongByteSwap(hdr->base.devid);
	hdr->base.direction = RtlUlongByteSwap(hdr->base.direction);
	hdr->base.ep = RtlUlongByteSwap(hdr->base.ep);

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		swap_cmd_submit(&hdr->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		swap_ret_submit(&hdr->u.ret_submit);
		break;
	default:
		///TODO
		break;
	}
}