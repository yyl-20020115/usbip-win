#pragma once

#include <ntddk.h>

NTSTATUS complete_irp(IRP *irp, NTSTATUS status, ULONG info);
BOOLEAN is_my_irp(usbip_stub_dev_t *devstub, IRP *irp);
NTSTATUS pass_irp_down(usbip_stub_dev_t *devstub, IRP *irp, PIO_COMPLETION_ROUTINE completion_routine, void *context);
