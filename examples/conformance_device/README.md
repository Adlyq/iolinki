# Conformance reference device

This is an L3 reference integration for exercising the generic iolinki library
against IO-Link Test Specification cases. It is not a product and does not claim
product certification.

## Purpose

The example shows that the library can support a conformant Device when the
application supplies the required configuration:

- full identification and DPP1 values;
- Data Storage and block-parameter descriptors;
- Config1/2/3/4 test objects;
- events, PDIn/PDOut and NVM;
- selectable test profiles for different Test Spec scenarios.

Product-specific IODD values, local UI behaviour and hardware certification stay
in the product project.

## Profiles

| Profile | PDIn | PDOut | Use |
|---|---:|---:|---|
| `full` | 3 | 1 | DS, block parameters, Config1/2/3/4 |
| `config` | 1 | 1 | Config1/2/3/4 and fixed-length parameters |
| `pdout-only` | 0 | 1 | PDOut-only cases such as the PD-valid status check |
| `minimal` | 0 | 0 | Negative/trimmed scenarios |
| `type0` | 0 | 0 | Linux TYPE_0 master runner, full DS/config parameters |

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

The Linux build produces `build/examples/conformance_device/conformance_device`.
It requires a TTY or PTY, for example one created by `socat` or a test runner.

```sh
./build/examples/conformance_device/conformance_device \
  --profile full --phy /dev/pts/5 --nvm /tmp/conformance.nvm
```

## Smoke test and master subset

`tools/test_conformance_device_smoke.py` starts the device on a PTY, sends the
SIO wake-up byte and checks that the process is still in a valid startup state.
It is a transport smoke test, not a Test Spec run.

`tools/conformance_device_master.py` is a minimal TYPE_0 master. It starts the
`type0` profile, reaches OPERATE and checks a subset of DPP1/ISDU/Config/DS
cases: `0052`, `0055`, `0063`, `0064`, `0090`, `0091`, `0095`, `0096`, `0097`,
`0107`, `0108`, `0143`, `0144`, `0147`, `0155`, `0316`, `0317`, `0318`,
`0321`, `0322`, `0324`, `0325`, `0326`, `0327`, `0328`, `0330`, `0373`, `0374`,
`0069-0076`, `0077`-`0084`, complete Config1/Config4 block downloads, DS
upload/store/checksum flows, DeviceStatus/DetailedDeviceStatus before and
after events, event-memory appear/disappear, PREOPERATE, communication pause,
power cycles and short events. It is an end-to-end runner, not the full
142-case master.

```sh
python3 tools/test_conformance_device_smoke.py \
  ./build/examples/conformance_device/conformance_device
python3 tools/conformance_device_master.py \
  ./build/examples/conformance_device/conformance_device
python3 tools/test_conformance_iodd_oracle.py \
  ./build/examples/conformance_device/conformance_device
```

## Control commands

The process reads commands from stdin:

```text
event <code> <error|warning|notification>
clearevent <code>
pdvalid <0|1>
localchange
powercycle
resetinfo
factoryreset
backtobox
dump
quit
```

`localchange` emulates a teach-in/panel edit (it changes Config2) and then
requests a Data Storage upload. `resetinfo` reports the application-level
startup count, observed reset reason and whether the parameter snapshot is at
its default; `factoryreset`/`backtobox` apply the application defaults and
record the reason. `powercycle` preserves the observation counters and reloads
the file-backed NVM.

These hooks are application-level test controls; they are not part of the
library API.

## IODD and oracle

- `device.json` is the synthetic test description.
- `device.xml` is generated with `tools/iodd_gen.py`.
- `iodd_expected.json` is the profile oracle: PD lengths, parameter lengths,
  access modes, default values, min cycle time, M-sequence capability and the
  assigned VendorID. The oracle checks DPP1 identification values, DS
  index/subindex consistency and Config block round-trips.
- The `type0` synthetic profile advertises `minCycleTime = 3000 us` and
  `mSequenceCapability = 1`, matching what the stack derives from the TYPE_0
  M-sequence and the configured process-data widths.

The synthetic IODD proves that the library can serve the configured profile. It
is not a product IODD and must not be used for product certification.

## Scope

Implemented:

- full PD and identification path;
- Config1 fixed 64-octet parameter with `0x8033/0x8034` length errors;
- Config2 16-bit parameter;
- Config3 200-octet variable parameter;
- four Config4 block parameters;
- Data Storage descriptors and NVM-backed storage;
- event injection, PD-valid control and local-change hook;
- `powercycle` re-initialisation with file-backed NVM.

Still to add:

- the remaining Test Spec cases and a full Python Test Spec runner;
- DID override demonstration;
- MCU PHY port and physical HIL;
- automatic IODD drift checks in CI beyond the current synthetic oracle.

Known production gaps observed by this integration are tracked by the
conformance audit, not worked around in the application layer: the composite
DataStorageIndex sub0 captures `Data_Storage_Size` before the image rebuild,
`ParamDownloadStore` in Idle maps to an unexpected `ParamDownloadEnd`
(`0x8036`), and `DS_UploadEnd` in Idle does not clear a pending
`DS_UPLOAD_FLAG`.
