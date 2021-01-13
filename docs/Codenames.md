On an Mac Mini M1 (2020):

* ioreg gives AGXAcceleratorG13G_B0 (with clients of type AGXDeviceUserClient), parent type sgx@4000000
* Also has gfx-asc@6400000 -> AppleASCWrapV4 -> ... -> AGXFirmwareKextG13RTBuddy
* Metal dispatches to AGXMetal13_3

All in all, looks like this is a **G13** chip.
