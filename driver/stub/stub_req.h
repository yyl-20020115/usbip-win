#pragma once

#include "stub_dev.h"

typedef struct {
	unsigned long	seqnum;
	int	err;
	PVOID	data;
	int	data_len;
	LIST_ENTRY	list;
} stub_res_t;

stub_res_t *
create_stub_res(unsigned long seqnum, int err, PVOID data, int data_len, BOOLEAN need_copy);
void free_stub_res(stub_res_t *sres);

void reply_stub_req(usbip_stub_dev_t *devstub, unsigned long seqnum);
void reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned long seqnum, int err);
void reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy);