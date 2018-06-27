#include <ntddk.h>

#include <ntstrsafe.h>

#include "usbip_proto.h"

#ifdef DBG

int
dbg_snprintf(char *buf, int size, const char *fmt, ...)
{
	va_list	arglist;
	size_t	len;
	NTSTATUS	status;

	va_start(arglist, fmt);
	status = RtlStringCchVPrintfA(buf, size, fmt, arglist);
	va_end(arglist);

	if (NT_ERROR(status))
		return 0;
	status = RtlStringCchLengthA(buf, size, &len);
	if (NT_ERROR(status))
		return 0;
	return (int)len;
}

const char *
dbg_usbip_hdr(struct usbip_header *hdr)
{
	static char	buf[512];
	int	n;

	n = dbg_snprintf(buf, 512, "seq:%u,%s,ep:%u", hdr->base.seqnum, hdr->base.direction ? "in": "out", hdr->base.ep);
	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		dbg_snprintf(buf + n, 512 - n, ",tlen:%u", hdr->u.cmd_submit.transfer_buffer_length);
		break;
	case USBIP_RET_SUBMIT:
		dbg_snprintf(buf + n, 512 - n, ",alen:%u", hdr->u.ret_submit.actual_length);
		break;
	default:
		break;
	}
	return buf;
}

#endif
