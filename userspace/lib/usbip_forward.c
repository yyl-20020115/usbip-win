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
	FILE	*fp;
	va_list ap;

	if (fopen_s(&fp, "debug_pdu.log", "a+") != 0)
		return;

	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fclose(fp);
}

static void
dump_usbip_header(struct usbip_header *hdr)
{
	dbg_to_file("cmd:%x,seq:%x,devid:%x,dir:%x,ep:%x\n",
		hdr->base.command, hdr->base.seqnum, hdr->base.devid, hdr->base.direction, hdr->base.ep);

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		dbg_to_file("CMD_SUBMIT: flags:%x,len:%x,sf:%x,#p:%x,intv:%x\n",
			hdr->u.cmd_submit.transfer_flags,
			hdr->u.cmd_submit.transfer_buffer_length,
			hdr->u.cmd_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.cmd_submit.interval);
		dbg_to_file("     setup: %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
			hdr->u.cmd_submit.setup[0], hdr->u.cmd_submit.setup[1], hdr->u.cmd_submit.setup[2],
			hdr->u.cmd_submit.setup[3], hdr->u.cmd_submit.setup[4], hdr->u.cmd_submit.setup[5],
			hdr->u.cmd_submit.setup[6], hdr->u.cmd_submit.setup[7]);
			break;
	case USBIP_CMD_UNLINK:
		dbg_to_file("CMD_UNLINK: seq:%x\n", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_SUBMIT:
		dbg_to_file("RET_SUBMIT: st:%x,al:%x,sf:%x,#p:%x,ec:%x\n",
			hdr->u.ret_submit.status,
			hdr->u.ret_submit.actual_length,
			hdr->u.ret_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.ret_submit.error_count);
		break;
	case USBIP_RET_UNLINK:
		dbg_to_file("RET_UNLINK: status:%x\n", hdr->u.ret_unlink.status);
		break;
	default:
		/* NOT REACHED */
		dbg_to_file("UNKNOWN\n");
		break;
	}
}

#define DBGF(fmt, ...)		dbg_to_file(fmt, ## __VA_ARGS__)
#define DBG_USBIP_HEADER(hdr)	dump_usbip_header(hdr)
#define DBG_USBIP_HEADER_R(hdr)	do { struct usbip_header Hdr = *hdr; swap_usbip_header_endian(&Hdr); dump_usbip_header(&Hdr); } while (0)

#else

#define DBGF(fmt, ...)
#define DBG_USBIP_HEADER(hdr)
#define DBG_USBIP_HEADER_R(hdr)

#endif

static void
swap_usbip_header_base_endian(struct usbip_header_basic *base)
{
	base->command	= htonl(base->command);
	base->seqnum	= htonl(base->seqnum);
	base->devid	= htonl(base->devid);
	base->direction	= htonl(base->direction);
	base->ep	= htonl(base->ep);
}

static void
swap_cmd_submit_endian(struct usbip_header_cmd_submit *pdu)
{
	pdu->transfer_flags	= ntohl(pdu->transfer_flags);
	pdu->transfer_buffer_length = ntohl(pdu->transfer_buffer_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->interval = ntohl(pdu->interval);
}

static void
swap_ret_submit_endian(struct usbip_header_ret_submit *pdu)
{
	pdu->status = ntohl(pdu->status);
	pdu->actual_length = ntohl(pdu->actual_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->error_count = ntohl(pdu->error_count);
}

static void
swap_cmd_unlink_endian(struct usbip_header_cmd_unlink *pdu)
{
	pdu->seqnum = ntohl(pdu->seqnum);
}

static void
swap_ret_unlink_endian(struct usbip_header_ret_unlink *pdu)
{
	pdu->status = ntohl(pdu->status);
}

static void
swap_usbip_header_endian(struct usbip_header *pdu)
{
	unsigned int	cmd;

	swap_usbip_header_base_endian(&pdu->base);
	cmd = pdu->base.command;

	switch (cmd) {
	case USBIP_CMD_SUBMIT:
		swap_cmd_submit_endian(&pdu->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		swap_ret_submit_endian(&pdu->u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		swap_cmd_unlink_endian(&pdu->u.cmd_unlink);
		break;
	case USBIP_RET_UNLINK:
		swap_ret_unlink_endian(&pdu->u.ret_unlink);
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

static int
get_xfer_len(BOOL is_req, struct usbip_header *hdr)
{
	if (is_req) {
		if (ntohl(hdr->base.command) == USBIP_CMD_UNLINK)
			return 0;
		if (hdr->base.direction)
			return 0;
		if (!record_outq_seqnum(ntohl(hdr->base.seqnum))) {
			err("failed to record. out queue full");
		}
		return ntohl(hdr->u.cmd_submit.transfer_buffer_length);
	}
	else {
		if (ntohl(hdr->base.command) == USBIP_RET_UNLINK)
			return 0;
		if (is_outq_seqnum(ntohl(hdr->base.seqnum)))
			return 0;
		return ntohl(hdr->u.ret_submit.actual_length);
	}
}

static int
get_iso_len(BOOL is_req, struct usbip_header *hdr)
{
	if (is_req) {
		if (ntohl(hdr->base.command) == USBIP_CMD_UNLINK)
			return 0;
		return ntohl(hdr->u.cmd_submit.number_of_packets) * sizeof(struct usbip_iso_packet_descriptor);
	}
	else {
		if (ntohl(hdr->base.command) == USBIP_RET_UNLINK)
			return 0;
		return ntohl(hdr->u.ret_submit.number_of_packets) * sizeof(struct usbip_iso_packet_descriptor);
	}
}

static BOOL
write_to_dev(BOOL is_req, char *buf, int buf_len, int len, SOCKET sockfd, HANDLE hdev, OVERLAPPED *pov)
{
	struct usbip_header	*hdr;
	unsigned long	xfer_len, iso_len, len_data;
	unsigned long	out;

	hdr = (struct usbip_header *)buf;

	if (len < sizeof(struct usbip_header)) {
		err("write_to_dev: too small buffer: len: %d", len);
		DBG_USBIP_HEADER(hdr);
		return FALSE;
	}

	xfer_len = get_xfer_len(is_req, hdr);
	iso_len = get_iso_len(is_req, hdr);

	swap_usbip_header_endian(hdr);

	DBGF("dev: write: seq %d\n", hdr->base.seqnum);

	DBG_USBIP_HEADER(hdr);

	len_data = xfer_len + iso_len;
	if (buf_len < len_data + sizeof(struct usbip_header)) {
		err("too small buffer: buflen: %d, xfer_len:%ld, iso_len:%ld", buf_len, xfer_len, iso_len);
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
		fix_iso_desc_endian(sock_read_buf + sizeof(struct usbip_header) + xfer_len,
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
sock_read_async(BOOL is_req, SOCKET sockfd, HANDLE hdev, OVERLAPPED *ov_sock, OVERLAPPED *ov_dev)
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

		if (len == 0) {
			DBGF("socket closed");
			return FALSE;
		}
		if (len != sizeof(struct usbip_header)) {
			err("sock_read_async: incomplete header: len: %d\n", len);
			/* TODO: need buffering */
		}

		DBGF("Bytes read from socket synchronously: %d\n", len);

		if (!write_to_dev(!is_req, sock_read_buf, FORWARD_BUFSIZE, len, sockfd, hdev, ov_dev))
			return FALSE;
	}
}

static BOOL
sock_read_completed(BOOL is_req, SOCKET sockfd, HANDLE hdev, OVERLAPPED *ov_sock, OVERLAPPED *ov_dev)
{
	unsigned long	len;

	if (!GetOverlappedResult((HANDLE)sockfd, ov_sock, &len, FALSE)) {
		err("get overlapping failed: %ld", GetLastError());
		return FALSE;
	}

	if (len == 0) {
		DBGF("socket closed");
		return FALSE;
	}

	DBGF("Bytes read from socket asynchronously: %d\n",len);

	if (!write_to_dev(!is_req, sock_read_buf, FORWARD_BUFSIZE, len, sockfd, hdev, ov_dev))
		return FALSE;

	return sock_read_async(is_req, sockfd, hdev, ov_sock, ov_dev);
}

static BOOL
write_to_sock(BOOL is_req, char *buf, int len, SOCKET sockfd)
{
	struct usbip_header	*hdr;
	unsigned long	xfer_len, iso_len;
	int	ret;

	hdr = (struct usbip_header *)buf;

	if (len < sizeof(struct usbip_header)) {
		err("write_to_sock: too small buffer: len: %d", len);
		return FALSE;
	}
	xfer_len = get_xfer_len(is_req, hdr);
	iso_len = get_iso_len(is_req, hdr);

	if (len != sizeof(struct usbip_header) + xfer_len + iso_len) {
		err("invalid buffer length:%d, out_len:%ld, iso_len:%ld\n", len, xfer_len, iso_len);
		return FALSE;
	}

	DBGF("sock: write: seq:%d\n", ntohl(hdr->base.seqnum));
	DBG_USBIP_HEADER_R(hdr);

	ret = usbip_net_send(sockfd, buf, len);
	if (ret != len) {
		err("failed to send: len:%d, ret:%d\n", len, ret);
		return FALSE;
	}

	return TRUE;
}

static BOOL
dev_read_async(BOOL is_req, HANDLE hdev, SOCKET sockfd, OVERLAPPED *pov)
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

		DBGF("Bytes read from dev synchronously: %d\n", len);

		if (!write_to_sock(!is_req, dev_read_buf, len, sockfd))
			return FALSE;
	}
}

static BOOL
dev_read_completed(BOOL is_req, HANDLE hdev, SOCKET sockfd, OVERLAPPED *ov)
{
	unsigned long len;

	if (!GetOverlappedResult(hdev, ov, &len, FALSE)) {
		err("get overlapping failed: %ld", GetLastError());
		return FALSE;
	}

	DBGF("Bytes read from dev asynchronously: %d\n", len);

	if (!write_to_sock(!is_req, dev_read_buf, len, sockfd))
		return FALSE;
	return dev_read_async(is_req, hdev, sockfd, ov);
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
usbip_forward(SOCKET sockfd, HANDLE hdev, BOOL is_inbound)
{
	HANDLE		evts[3];
	OVERLAPPED	ovs[3];
	BOOL		is_req_sock, is_req_dev;

	dev_read_buf = (char *)malloc(FORWARD_BUFSIZE);
	sock_read_buf = (char *)malloc(FORWARD_BUFSIZE);

	if (dev_read_buf == NULL || sock_read_buf == NULL) {
		err("cannot allocate buffers");
		return;
	}

	if (!setup_overlapped_events(ovs, evts))
		return;

	signal(SIGINT, signalhandler);

	is_req_sock = is_inbound ? FALSE: TRUE;
	is_req_dev = !is_req_sock;

	dev_read_async(is_req_dev, hdev, sockfd, &ovs[0]);
	sock_read_async(is_req_sock, sockfd, hdev, &ovs[1], &ovs[2]);

	while (!interrupted) {
		DWORD	ret;

		DBGF("waiting\n");
		ret = WaitForMultipleObjects(2, evts, FALSE, 500);
		DBGF("wait result: %x\n", ret);

		switch (ret) {
		case WAIT_TIMEOUT:
			// do nothing just give CTRL-C a chance to be detected
			break;
		case WAIT_OBJECT_0:
			if (!dev_read_completed(is_req_dev, hdev, sockfd, &ovs[0]))
				goto out;
			break;
		case WAIT_OBJECT_0 + 1:
			if (!sock_read_completed(is_req_sock, sockfd, hdev, &ovs[1], &ovs[2]))
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
