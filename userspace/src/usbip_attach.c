/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -h, --host=<host>      The machine with exported USB devices\n"
	"    -b, --busid=<busid>    Busid of the device on <host>\n";

void usbip_attach_usage(void)
{
	printf("usage: %s", usbip_attach_usage_string);
}

static int import_device(SOCKET sockfd, struct usbip_usb_device *udev, HANDLE *devfd)
{
	HANDLE fd;
	int port, ret;

	fd = usbip_vbus_open();
	if (INVALID_HANDLE_VALUE == fd) {
		err("open vbus driver");
		return -1;
	}

	port = usbip_vbus_get_free_port(fd);
	if (port <= 0) {
		err("no free port");
		CloseHandle(fd);
		return -1;
	}

	dbg("call from attch here\n");
	ret = usbip_vbus_attach_device(fd, port, udev);
	dbg("return from attch here\n");

	if (ret < 0) {
		err("import device");
		CloseHandle(fd);
		return -1;
	}
	dbg("devfd:%p\n", devfd);
	*devfd = fd;

	return port;
}

static int query_import_device(SOCKET sockfd, const char *busid, HANDLE *fd)
{
	int ret;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));

	/* send a request */
	ret = usbip_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	strncpy_s(request.busid, SYSFS_BUS_ID_SIZE, busid, sizeof(request.busid));
	request.busid[sizeof(request.busid) - 1] = 0;

	PACK_OP_IMPORT_REQUEST(0, &request);

	ret = usbip_send(sockfd, (void *)&request, sizeof(request));
	if (ret < 0) {
		err("send op_import_request");
		return -1;
	}

	/* recieve a reply */
	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *)&reply, sizeof(reply));
	if (ret < 0) {
		err("recv op_import_reply");
		return -1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
		err("recv different busid %s", reply.udev.busid);
		return -1;
	}

	/* import a device */
	return import_device(sockfd, &reply.udev, fd);
}

static int
attach_device(const char *host, const char *busid)
{
	SOCKET	sockfd;
	int		rhport;
	HANDLE	devfd = INVALID_HANDLE_VALUE;

	sockfd = usbip_net_tcp_connect(host, USBIP_PORT_STRING);
	if (INVALID_SOCKET == sockfd) {
		err("tcp connect");
		return 0;
	}
	rhport = query_import_device(sockfd, busid, &devfd);
	if (rhport < 0) {
		err("query");
		return 0;
	}
	info("new usb device attached to usbvbus port %d\n", rhport);
	usbip_vbus_forward(sockfd, devfd);

	dbg("detaching device");
	usbip_vbus_detach_device(devfd, rhport);

	dbg("closing connection to device");
	CloseHandle(devfd);

	dbg("closing connection to peer");
	closesocket(sockfd);

	dbg("done");
	return 1;
}

int usbip_attach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "host", required_argument, NULL, 'h' },
		{ "busid", required_argument, NULL, 'b' },
		{ NULL, 0, NULL, 0 }
	};
	char *host = NULL;
	char *busid = NULL;
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "h:b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			host = optarg;
			break;
		case 'b':
			busid = optarg;
			break;
		default:
			goto err_out;
		}
	}

	if (!host || !busid)
		goto err_out;

	ret = attach_device(host, busid);
	goto out;

err_out:
	usbip_attach_usage();
out:
	return ret;
}