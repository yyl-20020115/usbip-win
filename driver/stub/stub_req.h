#pragma once

#include "stub_dev.h"

void reply_stub_req(usbip_stub_dev_t *devstub, unsigned long seqnum);
void reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned long seqnum, int err);
void reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len);