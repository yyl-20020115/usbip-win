#include "stub_driver.h"

#include "usbip_proto.h"
#include "stub_res.h"
#include "stub_dbg.h"

extern void
set_ret_submit_usbip_header(struct usbip_header *hdr, unsigned long seqnum, int status, int actual_length);

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
create_stub_res(unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
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
send_stub_res(PIRP irp_read, unsigned long seqnum, int err, PVOID data, int data_len)
{
	struct usbip_header	*hdr;
	ULONG	len_read = sizeof(struct usbip_header);

	DBGI(DBG_GENERAL, "reply_stub_res: seq:%u,err:%d,len:%d\n", seqnum, err, data_len);

	if (err == 0)
		len_read += data_len;

	hdr = get_usbip_hdr_from_read_irp(irp_read, len_read);
	if (hdr == NULL) {
		irp_read->IoStatus.Status = STATUS_UNSUCCESSFUL;
		IoCompleteRequest(irp_read, IO_NO_INCREMENT);
		return;
	}
	if (err == 0) {
		set_ret_submit_usbip_header(hdr, seqnum, err, data_len);
		if (data_len > 0)
			RtlCopyMemory((PCHAR)hdr + sizeof(struct usbip_header), data, data_len);
	}
	else {
		set_ret_submit_usbip_header(hdr, seqnum, err, 0);
	}
	irp_read->IoStatus.Information = len_read;
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
}

static void
send_stub_res_async(PIRP irp_read, stub_res_t *sres)
{
	send_stub_res(irp_read, sres->seqnum, sres->err, sres->data, sres->data_len);
	free_stub_res(sres);
}

BOOLEAN
collect_pending_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (IsListEmpty(&devstub->stub_res_head)) {
		IoMarkIrpPending(irp_read);
		devstub->irp_stub_read = irp_read;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
		return FALSE;
	}
	else {
		stub_res_t	*sres;
		PLIST_ENTRY	le;

		le = RemoveHeadList(&devstub->stub_res_head);
		sres = CONTAINING_RECORD(le, stub_res_t, list);

		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		send_stub_res_async(irp_read, sres);
		return TRUE;
	}
}

static void
reply_result(usbip_stub_dev_t *devstub, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == NULL) {
		stub_res_t	*sres;

		sres = create_stub_res(seqnum, err, data, data_len, need_copy);
		if (sres != NULL)
			InsertTailList(&devstub->stub_res_head, &sres->list);
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);
	}
	else {
		PIRP	irp_read = devstub->irp_stub_read;
		devstub->irp_stub_read = NULL;
		KeReleaseSpinLock(&devstub->lock_stub_res, oldirql);

		send_stub_res(irp_read, seqnum, err, data, data_len);
		if (data && !need_copy)
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
	}
}

void
reply_stub_req_async(usbip_stub_dev_t *devstub, stub_res_t *sres)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&devstub->lock_stub_res, &oldirql);
	if (devstub->irp_stub_read == NULL) {
		InsertTailList(&devstub->stub_res_head, &sres->list);
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