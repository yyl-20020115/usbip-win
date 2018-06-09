#include "usbip_windows.h"

#include <signal.h>
#include <stdlib.h>

#include "usbip_proto.h"
#include "usbip_network.h"

#define FORWARD_BUFSIZE 1000000

static char	*dev_read_buf;
static char	*sock_read_buf;

#ifdef DEBUG_PDU

static void
dbg_to_file(char *fmt, ...)
{
	static FILE	*fp = NULL;
	va_list ap;

	if (fp == NULL) {
		if (fopen_s(&fp, "debug_pdu.log", "w") == 0)
			return;
	}

	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fflush(fp);
}

static void
dump_usbip_header(struct usbip_header *hdr)
{
	dbg_to_file("cmd:%u,seq:%u,devid:%u,dir:%u,ep:%u\n",
		hdr->base.command, hdr->base.seqnum, hdr->base.devid, hdr->base.direction, hdr->base.ep);

	switch(hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		dbg_to_file("CMD_SUBMIT: flags:%u,len:%u,sf:%u,#p:%u,intv:%u\n",
			hdr->u.cmd_submit.transfer_flags,
			hdr->u.cmd_submit.transfer_buffer_length,
			hdr->u.cmd_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.cmd_submit.interval);
			break;
	case USBIP_CMD_UNLINK:
		dbg_to_file("CMD_UNLINK: seq:%u\n", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_SUBMIT:
		dbg_to_file("RET_SUBMIT: st:%d,al:%u,sf:%d,#p:%d,ec:%d\n",
			hdr->u.ret_submit.status,
			hdr->u.ret_submit.actual_length,
			hdr->u.ret_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.ret_submit.error_count);
		break;
	case USBIP_RET_UNLINK:
		dbg_to_file("RET_UNLINK: status:%d\n", hdr->u.ret_unlink.status);
		break;
	default:
		/* NOT REACHED */
		dbg_to_file("UNKNOWN\n");
	}
}

#define DBGMORE(fmt, ...)		dbg_to_file(fmt, ## __VA_ARGS__)
#define DBG_USBIP_HEADER(hdr)	dump_usbip_header(hdr)

#else

#define DBGMORE(fmt, ...)
#define DBG_USBIP_HEADER(hdr)

#endif

static void
correct_endian_basic(struct usbip_header_basic *base, int send)
{
	if (send) {
		base->command	= htonl(base->command);
		base->seqnum	= htonl(base->seqnum);
		base->devid	= htonl(base->devid);
		base->direction	= htonl(base->direction);
		base->ep	= htonl(base->ep);
	} else {
		base->command	= ntohl(base->command);
		base->seqnum	= ntohl(base->seqnum);
		base->devid	= ntohl(base->devid);
		base->direction	= ntohl(base->direction);
		base->ep	= ntohl(base->ep);
	}
}

static void
correct_endian_ret_submit(struct usbip_header_ret_submit *pdu)
{
	pdu->status	= ntohl(pdu->status);
	pdu->actual_length = ntohl(pdu->actual_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->error_count = ntohl(pdu->error_count);
}

static void
correct_endian_cmd_submit(struct usbip_header_cmd_submit *pdu)
{
	pdu->transfer_flags	= ntohl(pdu->transfer_flags);
	pdu->transfer_buffer_length = ntohl(pdu->transfer_buffer_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->interval = ntohl(pdu->interval);
}

static void
usbip_header_correct_endian(struct usbip_header *pdu, int send)
{
	unsigned int cmd = 0;

	if (send)
		cmd = pdu->base.command;

	correct_endian_basic(&pdu->base, send);

	if (!send)
		cmd = pdu->base.command;

	switch (cmd) {
	case USBIP_CMD_SUBMIT:
		correct_endian_cmd_submit(&pdu->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		correct_endian_ret_submit(&pdu->u.ret_submit);
		break;
	default:
		/* NOTREACHED */
		err("unknown command in pdu header: %d", cmd);
		break;
	}
}

static void
fix_iso_desc_endian(char *buf, int num)
{
	struct usbip_iso_packet_descriptor	*ip_desc;
	int i;

	ip_desc = (struct usbip_iso_packet_descriptor *)buf;
	for (i = 0; i < num; i++) {
		ip_desc->offset = ntohl(ip_desc->offset);
		ip_desc->status = ntohl(ip_desc->status);
		ip_desc->length = ntohl(ip_desc->length);
		ip_desc->actual_length = ntohl(ip_desc->actual_length);
		ip_desc++;
	}
}

#define OUT_Q_LEN 256
static long out_q_seqnum_array[OUT_Q_LEN];

static BOOL
record_outq_seqnum(unsigned long seqnum)
{
	int	i;

	for (i = 0; i < OUT_Q_LEN; i++) {
		if (out_q_seqnum_array[i])
			continue;
		out_q_seqnum_array[i] = seqnum;
		return TRUE;
	}
	return FALSE;
}

static BOOL
is_outq_seqnum(unsigned long seqnum)
{
	int	i;

	for (i = 0; i < OUT_Q_LEN; i++) {
		if (out_q_seqnum_array[i] != seqnum)
			continue;
		out_q_seqnum_array[i] = 0;
		return TRUE;
	}
	return FALSE;
}

static BOOL
write_to_dev(char *buf, int buf_len, int len, SOCKET sockfd, HANDLE hdev, OVERLAPPED *pov)
{
	struct usbip_header	*hdr;
	unsigned long	in_len, iso_len, len_data;
	unsigned long	out;

	hdr = (struct usbip_header *)buf;

	if (len < sizeof(struct usbip_header)) {
		err("too small buffer: len: %d", len);
		DBG_USBIP_HEADER(hdr);
		return FALSE;
	}

	usbip_header_correct_endian(hdr, 0);

	DBGMORE("recv seq %d\n", hdr->base.seqnum);

	DBG_USBIP_HEADER(hdr);

	if (is_outq_seqnum(htonl(hdr->base.seqnum)))
		in_len = 0;
	else
		in_len = hdr->u.ret_submit.actual_length;

	iso_len = hdr->u.ret_submit.number_of_packets * sizeof(struct usbip_iso_packet_descriptor);

	len_data = in_len + iso_len;
	if (buf_len < len_data + sizeof(struct usbip_header)) {
		err("too small buffer: buflen: %d, in_len:%ld, iso_len:%ld", buf_len, in_len, iso_len);
		return FALSE;
	}

	if (len_data > 0) {
		int	ret;

		ret = usbip_net_recv(sockfd, buf + sizeof(struct usbip_header), len_data);
		if (ret != len_data) {
			err("failed to recv data: ret: %d", ret);
			return FALSE;
		}
	}

	if (iso_len > 0)
		fix_iso_desc_endian(sock_read_buf + sizeof(struct usbip_header) + in_len,
				    hdr->u.ret_submit.number_of_packets);
	if (!WriteFile(hdev, buf, sizeof(struct usbip_header) + len_data, &out, pov)) {
		err("failed to WriteFile: %ld", GetLastError());
		return FALSE;
	}
	if (out != sizeof(struct usbip_header) + len_data) {
		err("failed to write fully: outlen: %d", out);
		return FALSE;
	}
	return TRUE;
}

static BOOL
sock_read_async(SOCKET sockfd, HANDLE hdev, OVERLAPPED *ov_sock, OVERLAPPED *ov_dev)
{
	while (TRUE) {
		unsigned long	len;

		if (!ReadFile((HANDLE)sockfd,  sock_read_buf, sizeof(struct usbip_header), &len, ov_sock)) {
			DWORD	err = GetLastError();

			if (err == ERROR_IO_PENDING)
				return TRUE;

			err("failed to read: err:%d\n", err);
			return FALSE;
		}

		if (len != sizeof(struct usbip_header)) {
			err("incomplete header err: %d\n", GetLastError());
		}

		DBGMORE("Bytes read from socket synchronously: %d\n", len);

		if (!write_to_dev(sock_read_buf, FORWARD_BUFSIZE, len, sockfd, hdev, ov_dev))
			return FALSE;
	}
}

static BOOL
sock_read_completed(SOCKET sockfd, HANDLE hdev, OVERLAPPED *ov_sock, OVERLAPPED *ov_dev)
{
	unsigned long	len;

	if (!GetOverlappedResult((HANDLE)sockfd, ov_sock, &len, FALSE)) {
		err("get overlapping failed: %ld", GetLastError());
		return FALSE;
	}

	DBGMORE("Bytes read from socket asynchronously: %d\n",len);

	if (!write_to_dev(sock_read_buf, FORWARD_BUFSIZE, len, sockfd, hdev, ov_dev))
		return FALSE;
	return sock_read_async(sockfd, hdev, ov_sock, ov_dev);
}

static BOOL
write_to_sock(char *buf, int len, SOCKET sockfd)
{
	struct usbip_header	*hdr;
	unsigned long out_len, iso_len;
	int	ret;

	hdr = (struct usbip_header *)buf;

	if (len < sizeof(struct usbip_header)) {
		err("too small buffer: %d\n", len);
		return FALSE;
	}
	if (!hdr->base.direction)
		out_len = ntohl(hdr->u.cmd_submit.transfer_buffer_length);
	else
		out_len = 0;
	if (hdr->u.cmd_submit.number_of_packets)
		iso_len = sizeof(struct usbip_iso_packet_descriptor) * ntohl(hdr->u.cmd_submit.number_of_packets);
	else
		iso_len = 0;
	if (len != sizeof(struct usbip_header) + out_len + iso_len) {
		err("invalid buffer length:%d, out_len:%ld, iso_len:%ld\n", len, out_len, iso_len);
		return FALSE;
	}
	if (!hdr->base.direction && !record_outq_seqnum(hdr->base.seqnum)) {
		err("failed to record. out queue full");
		return FALSE;
	}
	DBGMORE("send seq:%d\r", ntohl(hdr->base.seqnum));

	ret = usbip_net_send(sockfd, buf, len);
	if (ret != len) {
		err("failed to send: len:%d, ret:%d\n", len, ret);
		return FALSE;
	}

	return TRUE;
}

static BOOL
dev_read_async(HANDLE hdev, SOCKET sockfd, OVERLAPPED *pov)
{
	while (TRUE) {
		unsigned long	len;
		if (!ReadFile(hdev, dev_read_buf, FORWARD_BUFSIZE, &len, pov)) {
			DWORD	err = GetLastError();
			if (err == ERROR_IO_PENDING)
				return TRUE;

			err("ReadFile failed: err: %ld\n", err);
			return FALSE;
		}
		if (!write_to_sock(dev_read_buf, len, sockfd))
			return FALSE;
	}
}

static BOOL
dev_read_completed(HANDLE hdev, SOCKET sockfd, OVERLAPPED *ov)
{
	unsigned long len;

	if (!GetOverlappedResult(hdev, ov, &len, FALSE)) {
		err("get overlapping failed: %ld", GetLastError());
		return FALSE;
	}
	if (!write_to_sock(dev_read_buf, len, sockfd))
		return FALSE;
	return dev_read_async(hdev, sockfd, ov);
}

static volatile BOOL	interrupted;

static void
signalhandler(int signal)
{
	interrupted = TRUE;
}

static BOOL
setup_overlapped_events(OVERLAPPED ovs[], HANDLE evts[])
{
	int	i;

	for(i = 0; i < 3; i++) {
		evts[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (evts[i] == NULL) {
			err("cannot create new events");
			return FALSE;
		}
		ovs[i].Offset = ovs[i].OffsetHigh = 0;
		ovs[i].hEvent = evts[i];
	}
	return TRUE;
}

void
usbip_forward(SOCKET sockfd, HANDLE hdev)
{
	HANDLE		evts[3];
	OVERLAPPED	ovs[3];

	dev_read_buf = (char *)malloc(FORWARD_BUFSIZE);
	sock_read_buf = (char *)malloc(FORWARD_BUFSIZE);

	if (dev_read_buf == NULL || sock_read_buf == NULL) {
		err("cannot allocate buffers");
		return;
	}

	if (!setup_overlapped_events(ovs, evts))
		return;

	signal(SIGINT, signalhandler);

	dev_read_async(hdev, sockfd, &ovs[0]);
	sock_read_async(sockfd, hdev, &ovs[1], &ovs[2]);

	while (!interrupted) {
		DWORD	ret;

		DBGMORE("wait\n");
		ret =  WaitForMultipleObjects(2, evts, FALSE, 500);

		switch (ret) {
		case WAIT_TIMEOUT:
			// do nothing just give CTRL-C a chance to be detected
			break;
		case WAIT_OBJECT_0:
			if (!dev_read_completed(hdev, sockfd, &ovs[0]))
				goto out;
			break;
		case WAIT_OBJECT_0 + 1:
			if (!sock_read_completed(sockfd, hdev, &ovs[1], &ovs[2]))
				goto out;
			break;
		default:
			err("failed to wait: ret: %d\n", ret);
			goto out;
		}
	}

out:
	if (interrupted) {
		info("CTRL-C received\n");
	}

	free(dev_read_buf);
	free(sock_read_buf);

	/* cancel overlapped I/O */
	CancelIo(hdev);
	CancelIo((HANDLE)sockfd);
}
