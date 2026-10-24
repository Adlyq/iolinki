# LabWired actual-firmware tests

The release includes executable recipes for the same G0 firmware source used by
its build artifacts. These run the MCU instructions and firmware ISR; they do not
replace firmware with a host protocol implementation.

| Target | Engine pin | Assertions |
|---|---|---|
| STM32G0B1RE/TIOL112 | `4ff8cbaaa87418ace5e85be45c220b371f8c5b6f` (merged PR1294) | Startup, 1 MHz timer, GPIO UART mux and firmware WAKE interrupt/COM2 setup; CLI startup witness |

Focused tests run in both execution modes. A prior focused pass does not mean
every engine-wide check passed. New release CI rebuilds the firmware from its
exact source and archives the simulator log; use that log for release-specific
evidence.

Install the engine's pinned Rust toolchain (`rust-toolchain.toml`), native build
dependencies and firmware prerequisites described by each example README.
Clone the engine separately and check out the indicated target's exact pin:

```sh
git clone https://github.com/w1ne/labwired-core.git /tmp/labwired-iolinki-g0
git -C /tmp/labwired-iolinki-g0 checkout 4ff8cbaaa87418ace5e85be45c220b371f8c5b6f
LABWIRED_ROOT=/tmp/labwired-iolinki-g0 IOLINKI_FIRMWARE=/absolute/path/reference-device.elf bash validation/labwired/run.sh g0
```

The script rejects an unexpected engine commit or missing firmware; there is no
fallback to canned logs.

The UART/interrupt tests apply WAKE/OL edges as explicit inputs. Complete analog
TIOL112/L6362A transceiver behavior, cable/supply fault recovery and full
master/device operation are separate work. No physical-master, IAR compilation
or certification result is implied by these recipes. The repaired thermal
application is separately maintained at
https://github.com/w1ne/thermal-io-link-condition-sensor; its host/build evidence
is not the same as these counter firmware tests.
