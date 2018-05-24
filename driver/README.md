# Build

Build is tested on Win10, Visual studio 2017 Community, Windows Driver Kit 10.0.17134 and
Windows SDK 10.0.17134.

- Open usbip_vhci.vcxproj
- Build Project
- x86/x64 platforms are supported
- Driver file(usbip_vhci.sys), INF(usbip_vhci.inf), Test Certificate(usbip_vhci.cer) are created
  under Debug/x64 folder. A driver catalog file(usbip_vhci.cat) is in Debug/x64/usbip_vhci.

# Install Driver

Test driver certificate can be generated via VS 2017.

- Install a test certificate(usbip_vhci.cer) on a target machine.
  - It should be installed into "Trusted Root Certification Authority"
- Copy usbip_vhci.sys, usbip_vhci.inf, usbip_vhci.cat to a target.
- Start a device manager
- Choose "Add Legacy Hardware" from the "Action" menu.
- Select 'Install the hardware that I manually select from the list'.
- Click 'Next'.
- Click 'Have Disk', click 'Browse', choose the copied folder, and click OK.
- Click on the 'USB/IP VHCI, and then click Next.
- Click Finish at 'Completing the Add/Remove Hardware Wizard.'
