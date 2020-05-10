#pragma once

#include "basetype.h"
#include "vhci_dev.h"

#define INITIALIZE_PNP_STATE(_Data_)    \
        (_Data_)->common.DevicePnPState =  NotStarted;\
        (_Data_)->common.PreviousPnPState = NotStarted;

#define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->common.PreviousPnPState =  (_Data_)->common.DevicePnPState;\
        (_Data_)->common.DevicePnPState = (_state_);

#define RESTORE_PREVIOUS_PNP_STATE(_Data_)   \
        (_Data_)->common.DevicePnPState =   (_Data_)->common.PreviousPnPState;

extern PAGEABLE NTSTATUS vhci_unplug_vpdo(ULONG port, pusbip_vhub_dev_t vhub);
extern PAGEABLE NTSTATUS vhci_eject_vpdo(ULONG port, pusbip_vhub_dev_t vhub);