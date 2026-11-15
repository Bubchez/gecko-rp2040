# Simulation firmware third-party notices

These texts accompany the T09 UF2. They do not grant permissions for inherited
Arduino/ESPHome code. No SDK/compiler implementation is vendored here. The pinned
build downloads unmodified external dependencies; notices are copied verbatim.
The independently authored bridge source is covered by `pico_w/LICENSE`.

| Component | Exact source | Applicable terms / notice |
|---|---|---|
| Raspberry Pi Pico SDK 2.2.0 | `raspberrypi/pico-sdk` commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` | BSD-3-Clause, `notices/PICO_SDK_BSD3.txt`; compiled-source copyright list in `SOURCE_COPYRIGHTS.txt`. |
| CYW43 driver and included radio resources | SDK submodule `dd7568229f3bf7a37737b9e1ef250c26efe75b23` | **Raspberry Pi semiconductor-only grant**, copied as `CYW43_LICENSE_RP.txt`. This image targets Pico W/RP2040 only. Do not treat this as a general MIT/BSD radio licence or redistribute for other chips. |
| lwIP | SDK submodule `77dcd25a72509eb83f72b033d219b1d40cd8eb95` | BSD-3-Clause, `LWIP_BSD3.txt`. |
| TinyUSB | SDK submodule `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` | MIT, `TINYUSB_MIT.txt`; RPi USB port files also retain SDK BSD attribution. |
| CMSIS stub headers | SDK tree above | Apache-2.0, `CMSIS_APACHE2.txt`; ARM copyright notices remain in the pinned headers. |
| GCC 13.3.1 runtime / standard C++ headers | xPack 13.3.1-1.1, verified distribution archive | GPLv3 with GCC Runtime Library Exception 3.1, `GCC_GPL3.txt` and `GCC_RUNTIME_EXCEPTION.txt`. Independent bridge code is compiled with an eligible unmodified GCC process. Compiler executables are not redistributed. |
| newlib 4.4.0 / libgloss runtime | Same xPack archive | Multiple permissive notices preserved in full as `NEWLIB_NOTICES.txt` and `LIBGLOSS_NOTICES.txt`. No newlib source modifications. |

The SDK's alternate noncommercial CYW43 licence is not the grant relied upon:
`LICENSE.RP` explicitly permits use and redistribution in conjunction with RP2040
or another Raspberry Pi semiconductor. Preserve these notices with any UF2 copy.
Bluetooth, mbedTLS, a web framework, upstream AVR Wire and original Gecko component
code are not linked. Pico printf is replaced by the compiler/newlib implementation;
its otherwise-applicable Marco Paland implementation is not linked.

Toolchain archive URL, SHA256, immutable SDK/submodule commits and build-tool pins
are in `firmware/build-lock.json`. The artifact manifest records the exact build
inputs and source digest. Future dependency changes require a new licence review;
none of these grants resolves the missing original upstream source/asset grant.
