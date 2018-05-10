#pragma once

//
// This guid is used in IoCreateDeviceSecure call to create PDOs. The idea is to
// allow the administrators to control access to the child device, in case the
// device gets enumerated as a raw device - no function driver, by modifying the 
// registry. If a function driver is loaded for the device, the system will override
// the security descriptor specified in the call to IoCreateDeviceSecure with the 
// one specifyied for the setup class of the child device.
//

DEFINE_GUID(GUID_SD_BUSENUM_PDO,
        0x9d3039dd, 0xcca5, 0x4b4d, 0xb3, 0x3d, 0xe2, 0xdd, 0xc8, 0xa8, 0xc5, 0x2e);
// {9D3039DD-CCA5-4b4d-B33D-E2DDC8A8C52E}