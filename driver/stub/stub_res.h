#pragma once

#include "stub_dev.h"

typedef struct {
	PIRP	irp;
	unsigned int	cmd;
	unsigned long	seqnum;
	int	err;
	/* NOTE: actual_length in ret_submit for output endpoint should be same as transfer_buffer_length.
	 * So non-zero data_len with NULL data is possible for output endpoint. 
	 */
	PVOID	data;
	int	data_len;
	LIST_ENTRY	list;
} stub_res_t;

#ifdef DBG
const char *dbg_stub_res(stub_res_t *sres);
#endif

stub_res_t *
create_stub_res(unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy);
void free_stub_res(stub_res_t *sres);

void add_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres, PIRP irp);
void del_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres);
BOOLEAN cancel_pending_stub_res(usbip_stub_dev_t *devstub, unsigned int seqnum);

NTSTATUS collect_done_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read);

void reply_stub_req(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum);
void reply_stub_req_out(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int data_len);
void reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int err);
void reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy);