#include "stub_driver.h"

#include "usbip_proto.h"
#include "stub_res.h"
#include "stub_dbg.h"
#include "pdu.h"

#ifdef DBG

const char *
dbg_stub_res(stub_res_t *sres)
{
	static char	buf[1024];

	dbg_snprintf(buf, 1024, "seq:%u", sres->seqnum);
	return buf;
}

#endif

void
free_stub_res(stub_res_t *sres)
{
	if (sres == NULL)
		return;
	if (sres->data)
		ExFreePoolWithTag(sres->data, USBIP_STUB_POOL_TAG);
	ExFreePoolWithTag(sres, USBIP_STUB_POOL_TAG);
}

stub_res_t *
create_stub_res(unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
{
	stub_res_t	*sres;

	sres = ExAllocatePoolWithTag(NonPagedPool, sizeof(stub_res_t), USBIP_STUB_POOL_TAG);
	if (sres == NULL) {
		DBGE(DBG_GENERAL, "create_stub_res: out of memory\n");
		if (data != NULL && !need_copy)
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
		return NULL;
	}
	if (data != NULL && need_copy) {
		PVOID	data_copied;

		data_copied = ExAllocatePoolWithTag(NonPagedPool, data_len, USBIP_STUB_POOL_TAG);
		if (data_copied == NULL) {
			DBGE(DBG_GENERAL, "create_stub_res: out of memory. drop data.\n");
			data_len = 0;
		}
		else {
			RtlCopyMemory(data_copied, data, data_len);
		}
		data = data_copied;
	}

	sres->irp = NULL;
	sres->cmd = cmd;
	sres->seqnum = seqnum;
	sres->err = err;
	sres->data = data;
	sres->data_len = data_len;
	InitializeListHead(&sres->list);

	return sres;
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
send_stub_res(PIRP irp_read, unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len)
{
	struct usbip_header	*hdr;
	ULONG	len_read = sizeof(struct usbip_header);

	if (data != NULL)
		len_read += data_len;

	hdr = get_usbip_hdr_from_read_irp(irp_read, len_read);
	if (hdr == NULL) {
		DBGE(DBG_GENERAL, "send_stub_res: too small buffer: seqnum:%u\n", seqnum);
		irp_read->IoStatus.Status = STATUS_UNSUCCESSFUL;
		IoCompleteRequest(irp_read, IO_NO_INCREMENT);
		return;
	}

	hdr->base.command = cmd;
	hdr->base.seqnum = seqnum;
	hdr->base.devid = 0;
	hdr->base.direction = 0;
	hdr->base.ep = 0;

	switch (cmd) {
	case USBIP_RET_SUBMIT:
		hdr->u.ret_submit.status = err;
		hdr->u.ret_submit.actual_length = data_len;
		if (data != NULL && data_len > 0)
			RtlCopyMemory((PCHAR)hdr + sizeof(struct usbip_header), data, data_len);
		hdr->u.ret_submit.number_of_packets = 0;
		hdr->u.ret_submit.start_frame = 0;
		hdr->u.ret_submit.error_count = 0;
		break;
	case USBIP_RET_UNLINK:
		hdr->u.ret_unlink.status = err;
		break;
	default:
		break;
	}

	DBGI(DBG_GENERAL, "send_stub_res: %s\n", dbg_usbip_hdr(hdr));

	swap_usbip_header(hdr);
	irp_read->IoStatus.Information = len_read;
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
}

static void
send_stub_res_async(PIRP irp_read, stub_res_t *sres)
{
	if (sres->err == 0)
		send_stub_res(irp_read, sres->cmd, sres->seqnum, sres->err, sres->data, sres->data_len);
	else
		send_stub_res(irp_read, sres->cmd, sres->seqnum, sres->err, NULL, 0);
	free_stub_res(sres);
}

void
add_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres, PIRP irp)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	sres->irp = irp;
	InsertTailList(&devstub->stub_res_head_pending, &sres->list);
	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
}

void
del_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	RemoveEntryList(&sres->list);
	InitializeListHead(&sres->list);
	sres->irp = NULL;
	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
}

BOOLEAN
cancel_pending_stub_res(usbip_stub_dev_t *devstub, unsigned int seqnum)
{
	KIRQL	oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	for (le = devstub->stub_res_head_pending.Flink; le != &devstub->stub_res_head_pending; le = le->Flink) {
		stub_res_t	*sres;

		sres = CONTAINING_RECORD(le, stub_res_t, list);
		if (sres->seqnum == seqnum) {
			PIRP	irp = sres->irp;
			KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
			return IoCancelIrp(irp);
		}
	}
	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

	return FALSE;
}

static VOID
on_irp_read_cancelled(PDEVICE_OBJECT devobj, PIRP irp_read)
{
	KIRQL	oldirql;
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)devobj->DeviceExtension;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == irp_read) {
		devstub->irp_stub_read = NULL;
	}
	else {
		DBGE(DBG_GENERAL, "cancelled IRP does not match with devstub read irp");
	}
	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
	IoReleaseCancelSpinLock(irp_read->CancelIrql);

	irp_read->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
}

NTSTATUS
collect_done_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (IsListEmpty(&devstub->stub_res_head_done)) {
		IoSetCancelRoutine(irp_read, on_irp_read_cancelled);
		IoMarkIrpPending(irp_read);
		devstub->irp_stub_read = irp_read;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		return STATUS_PENDING;
	}
	else {
		stub_res_t	*sres;
		PLIST_ENTRY	le;

		le = RemoveHeadList(&devstub->stub_res_head_done);
		sres = CONTAINING_RECORD(le, stub_res_t, list);

		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		send_stub_res_async(irp_read, sres);
		return STATUS_SUCCESS;
	}
}

static void
reply_result(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == NULL) {
		stub_res_t	*sres;

		sres = create_stub_res(cmd, seqnum, err, data, data_len, need_copy);
		if (sres != NULL)
			InsertTailList(&devstub->stub_res_head_done, &sres->list);
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
	}
	else {
		PIRP	irp_read = devstub->irp_stub_read;
		devstub->irp_stub_read = NULL;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		send_stub_res(irp_read, cmd, seqnum, err, data, data_len);
		if (data != NULL && !need_copy)
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
	}
}

void
reply_stub_req_async(usbip_stub_dev_t *devstub, stub_res_t *sres)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == NULL) {
		InsertTailList(&devstub->stub_res_head_done, &sres->list);
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
	}
	else {
		PIRP	irp_read = devstub->irp_stub_read;
		devstub->irp_stub_read = NULL;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		send_stub_res_async(irp_read, sres);
	}
}

void
reply_stub_req(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum)
{
	reply_result(devstub, cmd, seqnum, 0, NULL, 0, FALSE);
}

void
reply_stub_req_out(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int data_len)
{
	reply_result(devstub, cmd, seqnum, 0, NULL, data_len, FALSE);
}

void
reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int err)
{
	reply_result(devstub, cmd, seqnum, err, NULL, 0, FALSE);
}

void
reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy)
{
	reply_result(devstub, USBIP_RET_SUBMIT, seqnum, 0, data, data_len, need_copy);
}