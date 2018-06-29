#include "stub_driver.h"

#include "usbip_proto.h"
#include "stub_req.h"
#include "stub_dbg.h"

extern void
set_ret_submit_usbip_header(struct usbip_header *hdr, unsigned long seqnum, int status, int actual_length);

/* TODO: take care of a device removal */
static PIRP
get_pending_read_irp(usbip_stub_dev_t *devstub, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == NULL) {
		devstub->is_pending_stub_res = TRUE;
		devstub->stub_res_seqnum = seqnum;
		devstub->stub_res_err = err;
		devstub->stub_res_data_len = data_len;
		if (data != NULL && need_copy) {
			PVOID	data_copied;

			data_copied = ExAllocatePoolWithTag(NonPagedPool, data_len, USBIP_STUB_POOL_TAG);
			if (data_copied == NULL) {
				DBGE(DBG_GENERAL, "get_pending_read_irp: out of memory\n");
				devstub->stub_res_data_len = 0;
			}
			else {
				RtlCopyMemory(data_copied, data, data_len);
			}
			devstub->stub_res_data = data_copied;
		}
		else {
			devstub->stub_res_data = data;
		}

		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		return NULL;
	}
	else {
		PIRP	irp_read = devstub->irp_stub_read;
		devstub->irp_stub_read = NULL;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		return irp_read;
	}
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
reply_stub_res(PIRP irp_read, unsigned long seqnum, int err, PVOID data, int data_len)
{
	struct usbip_header	*hdr;
	ULONG	len_read = sizeof(struct usbip_header) + data_len;

	DBGI(DBG_GENERAL, "reply_stub_res: seq:%u,err:%d,len:%d\n", seqnum, err, data_len);

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

BOOLEAN
collect_pending_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (!devstub->is_pending_stub_res) {
		IoMarkIrpPending(irp_read);
		devstub->irp_stub_read = irp_read;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		return FALSE;
	}
	else {
		unsigned long	seqnum = devstub->stub_res_seqnum;
		int	err = devstub->stub_res_err;
		PVOID	data = devstub->stub_res_data;
		int	data_len = devstub->stub_res_data_len;

		devstub->is_pending_stub_res = FALSE;

		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		reply_stub_res(irp_read, seqnum, err, data, data_len);
		return TRUE;
	}
}

static void
reply_result(usbip_stub_dev_t *devstub, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
{
	PIRP	irp_read;

	irp_read = get_pending_read_irp(devstub, seqnum, err, data, data_len, need_copy);
	if (irp_read != NULL) {
		reply_stub_res(irp_read, seqnum, err, data, data_len);
	}
}

void
reply_stub_req(usbip_stub_dev_t *devstub, unsigned long seqnum)
{
	reply_result(devstub, seqnum, 0, NULL, 0, FALSE);
}

void
reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned long seqnum, int err)
{
	reply_result(devstub, seqnum, err, NULL, 0, FALSE);
}

void
reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy)
{
	reply_result(devstub, seqnum, 0, data, data_len, need_copy);
}