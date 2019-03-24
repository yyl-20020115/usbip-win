# USB/IP for Windows

- This project aims to support both a USB/IP server and a client on Windows platform.


## Build

### Notes
- Build is tested on Windows 10 x64 and the projects are configured for this target by default.
- x86/x64 platforms should be supported. However, we don't have an x86 setup for testing at the moment.
- For Windows 7 users, change usbip\_stub and usbip\_vhci projects' Target OS version to Windows 7.
  - Right-click on the project > Properties > Driver Settings > Target OS version > Windows 7

### Build Tools
- Visual Studio 2017 Community
- Windows SDK 10.0.17134
- Windows Driver Kit 10.0.17134

### Build Process
- Open usbip_win.sln
- Set certificate driver signing for usbip\_stub and usbip\_vhci projects.
  - Right-click on the project > Properties > Driver Signing > Test Certificate
  - Browse to driver/usbip\_test.pfx
- Build solution or desired project.

- All output files are created under {Debug,Release}/{x64,x86} folder

## Install

### Windows USB/IP server

- Prepare a linux machine as a USB/IP client
  - Tested on Ubuntu 16.04
  - Kernel 4.15.0-29 (USB/IP kernel module crash was observed on some other version)
  - \# modprobe vhci-hcd

- Install USB/IP test certificate
  - Install driver/usbip\_test.pfx (password: usbip)
  - Certificate should be installed into "Trusted Root Certification Authority" and "Trusted Publishers"
    on local machine (not current user)
- Enable test signing
  - bcdedit.exe /set TESTSIGNING ON
  - reboot the system to apply
- Copy usbip.exe, usbipd.exe, usb.ids, usbip\_stub.sys, usbip\_stub.inx into a folder in target machine
  - You can find usbip.exe, usbipd.exe, usbip\_stub.sys in output folder.
  - userspace/usb.ids
  - driver/stub/usbip\_stub.inx
- Find USB device id
  - You can get device id from usbip listing
    - usbip.exe list -l
  - Bus id is always 1. So output from usbip.exe listing is shown as:

<pre><code>
    usbip.exe list -l
      - busid 1-59 (045e:00cb)
        Microsoft Corp. : Basic Optical Mouse v2.0 (045e:00cb)
      - busid 1-30 (80ee:0021)
        VirtualBox : USB Tablet (80ee:0021)
</code></pre>

- Bind USB device to usbip stub
  - This command replaces an existing function driver with usbip stub driver
	- This should be executed using administrator privilege
	- usbip\_stub.inx and usbip\_stub.sys files should be in the same folder as usbip.exe
  - usbip.exe bind -b 1-59
- Run usbipd.exe
  - usbipd.exe -d -4
	- TCP port 3240 should be allowed by firewall

- Attach USB/IP device on linux machine
  - \# usbip attach -r &lt;usbip server ip&gt; -p 1-59

### Windows USB/IP client

- Prepare a linux machine as a USB/IP server
  - tested on Ubuntu 16.04(Kernerl 4.15.0-29)
  - \# modprobe usbip-host

- Run usbipd on a USB/IP server(Linux)
  - \# usbipd -4 -d

- Install USB/IP test certificate
  - Install driver/usbip\_test.pfx(password: usbip)
  - Certificate should be installed into "Trusted Root Certification Authority" on local machine(not current user)
- Enable test signing
  - bcdedit.exe /set TESTSIGNING ON
  - reboot the system to apply
- Copy usbip.exe, usbip\_vhci.sys, usbip\_vhci.inf, usbip\_vhci.cer, usbip\_vhci.cat into a folder in target machine
  - You can find usbip.exe, usbip\_vhci.sys, usbip\_vhci.cer, usbip\_vhci.inf in output folder.
  - usbip\_vhci.cat can be found from usbip\_vhci subfolder of output folder
- Install USB/IP vhci driver
  - Start Device manager
  - Choose "Add Legacy Hardware" from the "Action" menu.
  - Select 'Install the hardware that I manually select from the list'.
  - Click 'Next'.
  - Click 'Have Disk', click 'Browse', choose the copied folder, and click OK.
  - Click on the 'USB/IP VHCI, and then click Next.
  - Click Finish at 'Completing the Add/Remove Hardware Wizard.'
- Attach a remote USB device
  - usbip.exe attach -r &lt;usbip server ip&gt; -b 2-2

<hr>
<sub>This project was supported by Basic Science Research Program through the National Research Foundation of Korea(NRF) funded by the Ministry of Education(2016R1A6A3A11930295).</sub>
