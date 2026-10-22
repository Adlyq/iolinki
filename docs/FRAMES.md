# IO-Link Frame & M-Sequence Support

This document describes the IO-Link M-Sequences and frame structures supported by the `iolinki` stack, both in the core library and the simulation tools.

## 1. Supported M-Sequences

The stack supports multiple M-Sequence types defined in the IO-Link V1.1.5 specification. The M-Sequence type determines how Process Data (PD) and On-request Data (OD) are interleaved.

| Type | Description | PD-in | PD-out | OD | ISR status |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Type 0** | On-request data only | 0 | 0 | 1 | Supported |
| **Type 1_1** | Legacy interleave only (Fig A.6) | 2 | 2 | 0 | **Rejected at init** (Table A.10 NOTE 3) |
| **Type 1_2** | OD only (Fig A.7) | 0 | 0 | 2 | Supported |
| **Type 1_V** | Variable OD length (Fig A.8) | 0 | 0 | 8/32 | Supported |
| **Type 2_1** | 1 PD-in + OD (Fig A.9) | 1 | 0 | 1 | Supported |
| **Type 2_2** | 2 PD-in + OD (Fig A.10) | 2 | 0 | 1 | Supported |
| **Type 2_3** | 1 PD-out + OD (Fig A.11) | 0 | 1 | 1 | Supported |
| **Type 2_4** | 2 PD-out + OD (Fig A.12) | 0 | 2 | 1 | Supported |
| **Type 2_5** | PD-in + PD-out + OD (Fig A.13) | 1 | 1 | 1 | Supported |
| **Type 2_V** | Variable PD and OD (Fig A.14) | 0..32 | 0..32 | 1/2/8/32 | Supported |

> The fixed-type layouts are implemented by the **opt-in ISR transport**
> (`-DIOLINK_ISR_PUMP=ON`). The default streaming transport retains upstream's
> generic `MC|CKT|PD-out|OD` layout and is left to upstream.

### 1.1 Message checksum (A.1.6)

The checksum is an XOR of every message octet seeded with `0x52`, compressed
from 8 to 6 bits by the equations in (A.1). It lives in the **CKT** octet of a
master message and in the **CKS** octet of a device reply; those octets' bits
0-5 are zero before the checksum is computed. There is no trailing checksum
octet.

### 1.2 Type 0 (On-request Data Only)
Used during STARTUP and PREOPERATE for identification and parameterization, and
in OPERATE for On-request Data access.
- **Master -> Device (read)**: `[MC] [CKT]` (2 bytes)
- **Master -> Device (write)**: `[MC] [CKT] [OD]`
- **Device -> Master (read reply)**: `[OD] [CKS]`
- **Device -> Master (write reply)**: `[CKS]` (1 byte)

A Type-0 request on the page, diagnosis or ISDU communication channel carries
one OD octet on a write. FlowCTRL for ISDU lives in `MC & 0x1F`.

### 1.3 Fixed Type 1_x / 2_x layouts (ISR transport)

Every fixed type has its own request/reply shape; the variable TYPE_2_V is the
only type that is not in this table (see Figure A.14).

| Type | READ request | WRITE request | READ reply | WRITE reply |
| :--- | :--- | :--- | :--- | :--- |
| 1_2 | `MC CKT` | `MC CKT OD0 OD1` | `OD0 OD1 CKS` | `CKS` |
| 1_V | `MC CKT` | `MC CKT OD0..ODm-1` | `OD0..ODm-1 CKS` | `CKS` |
| 2_1 | `MC CKT` | `MC CKT OD` | `OD PD CKS` | `PD CKS` |
| 2_2 | `MC CKT` | `MC CKT OD` | `OD PD0 PD1 CKS` | `PD0 PD1 CKS` |
| 2_3 | `MC CKT PD` | `MC CKT PD OD` | `OD CKS` | `CKS` |
| 2_4 | `MC CKT PD0 PD1` | `MC CKT PD0 PD1 OD` | `OD CKS` | `CKS` |
| 2_5 | `MC CKT PD` | `MC CKT PD OD` | `OD PD CKS` | `PD CKS` |

Fixed PD/OD lengths are enforced at `iolink_device_init()` (ISR build); a
mismatched configuration is rejected with
`IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT`. TYPE_1_1 is rejected with
`IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE` because it forces legacy interleave, which a
V1.1.5 Device shall not implement (Table A.10 NOTE 3).

The CKT carries the M-sequence type in bits 6-7 (`01` for Type 1_x, `10` for
Type 2_x) plus the 6-bit checksum. There is **no leading status octet and no
toggle bit**: the Event flag is CKS bit 7 and the PD-valid flag is CKS bit 6
(1 = invalid), per A.1.5.

## 2. Simulation Frames (Virtual Master)

The Python Virtual Master (`tools/virtual_master`) uses a dedicated framing mechanism to simulate the physical layer over a Virtual UART (PTY).

### 2.1 PTY Framing
To ensure synchronization and robust parsing in software simulation, the Master-Device communication follows strict timing and boundary rules:

- **Startup Trigger**: Simulated by a wake-up marker byte (`0x55`) that the virtual PHY's `detect_wakeup` scans for; the first M-sequence must follow within T_DSIO (300 ms).
- **Cycle Time**: Defaults to 10ms, configurable via `VirtualMaster`.
- **Sync Loss Recovery**: The Device stack automatically resets to `STARTUP` if no bytes are received for >1000ms.

## 3. Configuration & Capabilities

### 3.1 Stack Limits
- **Max Process Data**: 32 bytes (Input and Output).
- **ISDU Buffer**: 256 bytes.
- **Event Queue**: 4 entries (default `IOLINK_EVENT_QUEUE_SIZE`).

### 3.2 Dynamic Configuration
The stack is configured during `iolink_init()` via the `iolink_config_t` structure:

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< Type 0/1_2/1_V/2_1..2_5/2_V */
    uint8_t min_cycle_time;         /**< Encoded per spec */
    uint8_t pd_in_len;              /**< 0-32 bytes (fixed by the type) */
    uint8_t pd_out_len;             /**< 0-32 bytes (fixed by the type) */
    uint8_t od_width;               /**< OD octets; 0 = type default */
} iolink_config_t;
```
