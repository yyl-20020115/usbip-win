#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions

#include "basetype.h"
#include "debug.h"

#define BUSENUM_POOL_TAG (ULONG) 'suBT'

#define ERROR(fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_ERROR_LEVEL, "USBIPEnum:(EE) " fmt, ## __VA_ARGS__)
#define INFO(fmt, ...)	DbgPrintEx(DPFLTR_SYSTEM_ID, DPFLTR_INFO_LEVEL, "USBIPEnum: " fmt, ## __VA_ARGS__)

extern NPAGED_LOOKASIDE_LIST g_lookaside;