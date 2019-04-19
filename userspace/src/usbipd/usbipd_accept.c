#include "usbipd.h"

#include <ws2tcpip.h>

#include "usbip_network.h"
#include "usbipd_stub.h"

static void
recv_pdu(SOCKET connfd, BOOL *pneed_close_sockfd)
{
	uint16_t	code = OP_UNSPEC;
	int	ret;

	*pneed_close_sockfd = TRUE;

	ret = usbip_net_recv_op_common(connfd, &code);
	if (ret < 0) {
		dbg("%s: could not receive opcode: %#0x", __FUNCTION__, code);
		return;
	}

	switch (code) {
	case OP_REQ_DEVLIST:
		info("%s: received request: %#0x - list devices", __FUNCTION__, code);
		ret = recv_request_devlist(connfd);
		break;
	case OP_REQ_IMPORT:
		info("%s: received request: %#0x - attach device", __FUNCTION__, code);
		ret = recv_request_import(connfd);
		if (ret == 0)
			*pneed_close_sockfd = FALSE;
		break;
	case OP_REQ_DEVINFO:
	case OP_REQ_CRYPKEY:
	default:
		err("%s: received an unknown opcode: %#0x", __FUNCTION__, code);
		break;
	}

	if (ret == 0)
		info("%s: request %#0x: complete", __FUNCTION__, code);
	else
		info("%s: request %#0x: failed", __FUNCTION__, code);
}

static SOCKET
do_accept(SOCKET listenfd)
{
	SOCKET	connfd;
	struct sockaddr_storage	ss;
	socklen_t	len = sizeof(ss);

	memset(&ss, 0, sizeof(ss));
	connfd = accept(listenfd, (struct sockaddr *)&ss, &len);
	if (connfd == INVALID_SOCKET) {
		err("failed to accept connection");
	}
	else {
		char	host[NI_MAXHOST], port[NI_MAXSERV];
		int		rc;

		rc = getnameinfo((struct sockaddr *)&ss, len, host, sizeof(host),
			port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
		if (rc != 0)
			err("getnameinfo: %s", gai_strerror(rc));

		info("connection from %s:%s", host, port);
	}
	return connfd;
}

static void
process_request(SOCKET listenfd)
{
	SOCKET connfd;
	BOOL	need_close_sockfd;

	connfd = do_accept(listenfd);
	if (connfd == INVALID_SOCKET)
		return;

	recv_pdu(connfd, &need_close_sockfd);
	if (need_close_sockfd)
		closesocket(connfd);
}

void
accept_request(SOCKET *sockfds, fd_set *pfds)
{
	int	i;

	for (i = 0; sockfds[i] != INVALID_SOCKET; i++) {
		if (FD_ISSET(sockfds[i], pfds)) {
			process_request(sockfds[i]);
		}
	}
}
