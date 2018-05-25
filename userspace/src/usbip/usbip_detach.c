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

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_vhci.h"

static const char usbip_detach_usage_string[] =
	"usbip detach <args>\n"
	"    -p, --port=<port>    "
	" port the device is on\n";

void usbip_detach_usage(void)
{
	printf("usage: %s", usbip_detach_usage_string);
}

static int detach_port(const char *portstr)
{
	HANDLE hdev;
	uint8_t portnum;
	int ret;

	if (sscanf_s(portstr, "%hhu", &portnum) != 1) {
		err("invalid port %s", portstr);
		return 1;
	}
	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		err("open vhci_driver");
		return 1;
	}

	ret = usbip_vhci_detach_device(hdev, portnum);
	usbip_vhci_driver_close(hdev);
	return (ret == 0) ? 0: 1;
}

int usbip_detach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "port", required_argument, NULL, 'p' },
		{ NULL, 0, NULL, 0 }
	};
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "p:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			ret = detach_port(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_detach_usage();
out:
	return ret;
}

