#pragma once

#include <ntddk.h>

#include "usbip_proto.h"

void swap_usbip_header(struct usbip_header *hdr);