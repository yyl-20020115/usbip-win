#include "stub_driver.h"

#include "usbip_proto.h"

void
set_ret_submit_usbip_header(struct usbip_header *hdr, unsigned long seqnum, int status, int actual_length)
{
	hdr->base.command = RtlUlongByteSwap(USBIP_RET_SUBMIT);
	hdr->base.seqnum = RtlUlongByteSwap(seqnum);
	hdr->base.devid = 0;
	hdr->base.direction = 0;
	hdr->base.ep = 0;
	hdr->u.ret_submit.status = RtlUlongByteSwap(status);
	hdr->u.ret_submit.actual_length = RtlUlongByteSwap(actual_length);
	hdr->u.ret_submit.start_frame = 0;
	hdr->u.ret_submit.number_of_packets = 0;
	hdr->u.ret_submit.error_count = 0;
}
