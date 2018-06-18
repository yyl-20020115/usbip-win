#include "stub_driver.h"

#include "usbip_proto.h"
#include "stub_req.h"
#include "stub_dbg.h"

extern void
set_ret_submit_usbip_header(struct usbip_header *hdr, unsigned long seqnum, int status, int actual_length);

void
set_pending_read_irp(usbip_stub_dev_t *devstub, PIRP irp)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_irp_read, &oldirql);
	if (devstub->irp_read == NULL) {
		devstub->irp_read = irp;
		KeReleaseSpinLock(&devstub->lock_irp_read, oldirql);
		KeSetEvent(&devstub->event_read, IO_NO_INCREMENT, FALSE);
	}
	else {
		DBGE(DBG_GENERAL, "!!!!!!No way");
		KeReleaseSpinLock(&devstub->lock_irp_read, oldirql);
	}
}

/* TODO: take care of a device removal */
static PIRP
get_pending_read_irp(usbip_stub_dev_t *devstub)
{
	PIRP	irp_read;
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_irp_read, &oldirql);
	while (devstub->irp_read == NULL) {
		KeReleaseSpinLock(&devstub->lock_irp_read, oldirql);
		KeWaitForSingleObject(&devstub->event_read, Executive, KernelMode, FALSE, NULL);
		KeAcquireSpinLock(&devstub->lock_irp_read, &oldirql);
	}

	irp_read = devstub->irp_read;
	devstub->irp_read = NULL;
	KeReleaseSpinLock(&devstub->lock_irp_read, oldirql);

	return irp_read;
}

static struct usbip_header *
get_usbip_hdr_from_read_irp(PIRP irp, ULONG len)
{
	PIO_STACK_LOCATION	irpstack;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	if (irpstack->Parameters.Read.Length < len) {
		DBGW(DBG_GENERAL, "too small read buffer: %uld < %uld\n", irpstack->Parameters.Read.Length, len);
		return NULL;
	}
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

static void
reply_result(usbip_stub_dev_t *devstub, unsigned long seqnum, int err, PVOID data, int data_len)
{
	PIRP	irp_read;
	struct usbip_header	*hdr;
	ULONG	len_read = sizeof(struct usbip_header) + data_len;

	DBGI(DBG_GENERAL, "reply_result: seq:%u,err:%d,len:%d\n", seqnum, err, data_len);

	irp_read = get_pending_read_irp(devstub);
	hdr = get_usbip_hdr_from_read_irp(irp_read, len_read);
	if (hdr == NULL) {
		irp_read->IoStatus.Status = STATUS_UNSUCCESSFUL;
		IoCompleteRequest(irp_read, IO_NO_INCREMENT);
		return;
	}
	set_ret_submit_usbip_header(hdr, seqnum, err, data_len);
	if (data_len > 0)
		RtlCopyMemory((PCHAR)hdr + sizeof(struct usbip_header), data, data_len);
	irp_read->IoStatus.Information = len_read;
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
}

void
reply_stub_req(usbip_stub_dev_t *devstub, unsigned long seqnum)
{
	reply_result(devstub, seqnum, 0, NULL, 0);
}

void
reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned long seqnum, int err)
{
	reply_result(devstub, seqnum, err, NULL, 0);
}

void
reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len)
{
	reply_result(devstub, seqnum, 0, data, data_len);
}