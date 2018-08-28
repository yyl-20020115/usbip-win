# USB/IP for Windows

- This project aims to support both a usbip server and a client on windows platform.

## Build

### Build Tools
- Visual Studio 2017 Community
- Windows SDK 10.0.17134
- Windows Driver Kit 10.0.17134
- Build is tested on Win10

### Build Process
- Open usbip_win.sln
- Set certificate driver signing for usbip_stub, usbip_vhci project
  - Use driver/usbip_test.pfx for testing usbip-win
- Build solution
- x86/x64 platforms are supported
- All output files are created under {Debug,Release}/{x64,x86} folder

## Install

### Usbip server

- Prepare a linux machine as a usbip client
  - tested on Ubuntu 16.04
  - Kernel 4.15.0-29 (usbip kernel module crash was observed on some other version)
  - # modprobe vhci-hcd
  
- Install USBIP test certificate
  - Install driver/usbip_test.pfx(password: usbip)
  - Certificate should be installed into "Trusted Root Certification Authority" and "Trusted Publishers"
    on local machine(not current user)
- Enable test signing
  - bcdedit.exe /set TESTSIGNING ON
  - reboot the system to apply
- Copy usbip.exe, usbipd.exe, usb.ids, usbip_stub.sys, usbip_stub.inx into a folder in target machine
  - You can find usbip.exe, usbipd.exe, usbip_stub.sys in output folder.
  - userspace/usb.ids
  - driver/stub/usbip_stub.inx
- Find usb device id
  - You can get device id from usbip listing
    - &lt;target dir&gt;\usbip.exe list -l
  - Bus id is always 1. So output from usbip.exe listing is shown as:

<pre><code>
    C:\work\usbip.exe list -l
      - busid 1-59 (045e:00cb)
        Microsoft Corp. : Basic Optical Mouse v2.0 (045e:00cb)
      - busid 1-30 (80ee:0021)
        VirtualBox : USB Tablet (80ee:0021)
</code></pre>

- Bind usb device to usbip stub
  - This command replaces an existing function driver with usbip stub driver
  - Should be executed with an administrator privilege
  - usbip bind -b 1-59
  - usbip\_stub.inx, usbip\_stub.sys files should be exist in the same folder with usbip.exe
- Run usbipd.exe
  - usbipd.exe -d -4
  - TCP port 3240 should be allowed by firewall

- Attach usbip device on linux machine
  - # usbip attach -r <usbip server ip> -p 1-59

### Usbip client

- Prepare a linux machine as a usbip server
  - tested on Ubuntu 16.04(Kernerl 4.15.0-29)
  - # modprobe usbip-host

- Run usbipd on a usbip server(Linux)
  - # usbipd -4 -d

- Install USBIP test certificate
  - Install driver/usbip_test.pfx(password: usbip)
  - Certificate should be installed into "Trusted Root Certification Authority" on local machine(not current user)
- Enable test signing
- Copy usbip.exe, usbip_vhci.sys, usbip_vhci.inf, usbip_vhci.cer, usbip_vhci.cat into a folder in target machine
  - You can find usbip.exe, usbip_vhci.sys, usbip_vhci.cer, usbip_vhci.inf in output folder.
  - usbip_vhci.cat can be found from usbip_vhci subfolder of output folder
- Install USBIP vhci driver
  - Start a device manager
  - Choose "Add Legacy Hardware" from the "Action" menu.
  - Select 'Install the hardware that I manually select from the list'.
  - Click 'Next'.
  - Click 'Have Disk', click 'Browse', choose the copied folder, and click OK.
  - Click on the 'USB/IP VHCI, and then click Next.
  - Click Finish at 'Completing the Add/Remove Hardware Wizard.'
- Attach a remote USB device
  - *BUT* currently, it does not seem to be working

<hr>
<sub>This project was supported by Basic Science Research Program through the National Research Foundation of Korea(NRF) funded by the Ministry of Education(2016R1A6A3A11930295).</sub>
