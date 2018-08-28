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

	if (!sres->hdr_sent) {
		dbg_snprintf(buf, 1024, "%s", dbg_usbip_hdr(&sres->header));
	}
	else {
		dbg_snprintf(buf, 1024, "seq:%u,data_len:%d", sres->header.base.seqnum, sres->data_len);
	}
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
create_stub_res(unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len, ULONG n_pkts, BOOLEAN need_copy)
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

	RtlZeroMemory(&sres->header, sizeof(struct usbip_header));
	sres->irp = NULL;
	sres->hdr_sent = FALSE;
	sres->header.base.command = cmd;
	sres->header.base.seqnum = seqnum;
	sres->data = data;
	sres->data_len = data_len;

	switch (cmd) {
	case USBIP_RET_SUBMIT:
		sres->header.u.ret_submit.status = err;
		sres->header.u.ret_submit.actual_length = data_len;
		sres->header.u.ret_submit.number_of_packets = n_pkts;
		break;
	case USBIP_RET_UNLINK:
		sres->header.u.ret_unlink.status = err;
		break;
	default:
		break;
	}
	InitializeListHead(&sres->list);

	return sres;
}

static BOOLEAN
store_irp_stub_res(PIRP irp, PVOID data, ULONG data_len)
{
	PIO_STACK_LOCATION	irpstack;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	if (irpstack->Parameters.Read.Length < data_len) {
		DBGW(DBG_GENERAL, "too small read buffer: %lu < %lu\n", irpstack->Parameters.Read.Length, data_len);
		return FALSE;
	}
	RtlCopyMemory(irp->AssociatedIrp.SystemBuffer, data, data_len);
	return TRUE;
}

static void
save_pending_sres(usbip_stub_dev_t *devstub, stub_res_t *sres)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);

	NT_ASSERT(devstub->sres_pending == NULL);
	devstub->sres_pending = sres;

	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
}

static void
send_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read, stub_res_t *sres)
{
	PVOID	data;
	ULONG	data_len;

	if (!sres->hdr_sent) {
		data_len = sizeof(struct usbip_header);
		data = &sres->header;
	}
	else {
		data = sres->data;
		data_len = sres->data_len;
	}
	if (!store_irp_stub_res(irp_read, data, data_len)) {
		DBGE(DBG_GENERAL, "send_stub_res: failed to store irp: %s\n", dbg_stub_res(sres));
		irp_read->IoStatus.Status = STATUS_UNSUCCESSFUL;
		free_stub_res(sres);
	}
	else {
		DBGI(DBG_GENERAL, "send_stub_res: %s\n", dbg_stub_res(sres));
		irp_read->IoStatus.Information = data_len;
		if (!sres->hdr_sent && sres->data != NULL && sres->data_len > 0) {
			sres->hdr_sent = TRUE;
			save_pending_sres(devstub, sres);
		}
		else {
			free_stub_res(sres);
		}
	}
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
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
		if (sres->header.base.seqnum == seqnum) {
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

static void
send_irp_sres(usbip_stub_dev_t *devstub, KIRQL oldirql)
{
	PIRP	irp_read;
	stub_res_t	*sres;

	NT_ASSERT(devstub->irp_stub_read != NULL);
	NT_ASSERT(devstub->sres_pending != NULL || !IsListEmpty(&devstub->stub_res_head_done));

	irp_read = devstub->irp_stub_read;
	devstub->irp_stub_read = NULL;

	if (devstub->sres_pending != NULL) {
		sres = devstub->sres_pending;
		devstub->sres_pending = NULL;
	}
	else {
		PLIST_ENTRY	le;
		le = RemoveHeadList(&devstub->stub_res_head_done);
		sres = CONTAINING_RECORD(le, stub_res_t, list);
	}

	KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

	send_stub_res(devstub, irp_read, sres);
}

NTSTATUS
collect_done_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->sres_pending == NULL && IsListEmpty(&devstub->stub_res_head_done)) {
		IoSetCancelRoutine(irp_read, on_irp_read_cancelled);
		IoMarkIrpPending(irp_read);
		devstub->irp_stub_read = irp_read;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		return STATUS_PENDING;
	}
	else {
		NT_ASSERT(devstub->irp_stub_read == NULL);
		devstub->irp_stub_read = irp_read;
		send_irp_sres(devstub, oldirql);
		return STATUS_SUCCESS;
	}
}

void
reply_stub_req(usbip_stub_dev_t *devstub, stub_res_t *sres)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	InsertTailList(&devstub->stub_res_head_done, &sres->list);
	if (devstub->irp_stub_read == NULL) {
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
	}
	else {
		send_irp_sres(devstub, oldirql);
	}
}

void
reply_stub_req_hdr(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum)
{
	reply_stub_req_err(devstub, cmd, seqnum, 0);
}

void
reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int err)
{
	stub_res_t	*sres;

	sres = create_stub_res(cmd, seqnum, err, NULL, 0, 0, FALSE);
	if (sres != NULL)
		reply_stub_req(devstub, sres);
}

void
reply_stub_req_out(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int data_len)
{
	stub_res_t	*sres;

	sres = create_stub_res(cmd, seqnum, 0, NULL, data_len, 0, FALSE);
	if (sres != NULL)
		reply_stub_req(devstub, sres);
}

void
reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy)
{
	stub_res_t	*sres;

	sres = create_stub_res(USBIP_RET_SUBMIT, seqnum, 0, data, data_len, 0, need_copy);
	if (sres != NULL)
		reply_stub_req(devstub, sres);
}