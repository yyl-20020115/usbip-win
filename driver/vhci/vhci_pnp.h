#pragma once

#include "basetype.h"
#include "vhci_dev.h"

#define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->common.PreviousPnPState =  (_Data_)->common.DevicePnPState;\
        (_Data_)->common.DevicePnPState = (_state_);

#define RESTORE_PREVIOUS_PNP_STATE(_Data_)   \
        (_Data_)->common.DevicePnPState =   (_Data_)->common.PreviousPnPState;

PAGEABLE NTSTATUS
vhci_unplug_dev(ULONG port, pusbip_vhub_dev_t vhub);