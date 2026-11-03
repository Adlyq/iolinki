# Evaluation FAQ

The [website FAQ](https://iolinki.com/faq.html) covers commercial and evaluation
questions. Tier scope, pricing and support terms live in
[LICENSE.COMMERCIAL](../LICENSE.COMMERCIAL); the signed agreement determines a
customer's rights.

## What can I run today?

Start with [the reference software device](../examples/reference_device/README.md)
or the existing host tests. These demonstrate protocol/software behavior.
The Nucleo UART sample is a transport starting point, and does not supply a complete
physical transceiver implementation.

## Can I integrate without CMake, using IAR?

The core is C99 and can be added as sources to a native IDE project. There is no
verified EWARM board project yet. Use `include/` as the include path and the core
source list in `CMakeLists.txt`, replacing the Linux time/platform code with
your own. Exclude `phy_virtual.c` and the Linux sources from a physical target.
If supplying strong platform hooks, exclude `platform.c` too; this avoids
relying on compiler-specific weak-symbol behavior, but you must then also
provide `iolink_platform_critical_sections_ready()` returning 0.

The core sources are `device.c`, `crc.c`, `dll.c`, `dll_isr.c`, `isdu.c`, `events.c`,
`data_storage.c`, `params.c`, `device_info.c`; add `phy_tiol112.c` when using the new
TIOL112 driver. Provide the time source through `config.time_us` and the
critical-section symbols declared in `include/iolinki/platform.h`, and the
storage backend through `iolink_device_config_t::storage` when Data Storage is
enabled. The legacy `iolink_nvm_*` helpers have been removed. Ensure all library
and application units share configuration defines and that startup/linker files
match the actual MCU. A successful GCC build is not evidence of IAR compatibility.

## Is there a TIOL112 implementation?

Yes: [the portable transceiver driver](hardware/TIOL112.md) handles EN, SIO, baud
selection, wake-up consumption and NFAULT through explicit MCU callbacks. A
particular MCU still needs UART/GPIO/timer/NVM integration and physical testing.

## Have the examples been physically validated or certified?

No physical validation report or official certification record is published with
these new examples. Host unit tests and simulated frame exchanges do not establish
physical interoperability, signal timing, EMC robustness or official certification.
