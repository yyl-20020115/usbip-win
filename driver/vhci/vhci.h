#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions

#include "basetype.h"
#include "vhci_dbg.h"

#define USBIP_VHCI_POOL_TAG (ULONG) 'VhcI'

#define ERROR(fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_ERROR_LEVEL, "usbip_vhci:(EE) " fmt, ## __VA_ARGS__)
#define INFO(fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_INFO_LEVEL, "usbip_vhci: " fmt, ## __VA_ARGS__)

extern NPAGED_LOOKASIDE_LIST g_lookaside;
