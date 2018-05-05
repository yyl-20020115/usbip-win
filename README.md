# USB/IP for Windows

- This project aims to support both a usbip server and a client on windows platform.
- Build environment: Visual studio 2017 + Windows Driver Kit 10

## Userspace Tools
The userspace tools can be used to manage connections and devices. They are based on the ones shipped with the Linux kernel (in `tools/usb/usbip`). [► Documentation](./userspace/README)

## Driver
To import USB devices from a remote host, the device driver must be installed. [► Documentation](./driver/README)
