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
