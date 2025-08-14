# RU Energy Model for ns3-oran (ns-3.41)

A lightweight, RU‑centric energy model for **ns‑3** that plugs into the **NIST ns3‑oran** module. It adds:

- `OranRuPowerModel`: a parametric RU power/current model (PA efficiency, per‑TRX overheads, sleep mode, supply/cooling losses, optional mmWave overheads).
- `OranRuDeviceEnergyModel`: an `ns3::DeviceEnergyModel` that reports current to an `EnergySource`, using **live** TxPower from `LteEnbPhy` when available or a `TxPowerDbm` fallback.

The accompanying paper `Radio_Unit_Energy_Modeling_for_Open_RAN_in_ns3_oran-3.pdf` describes the math, assumptions, and validation.

---

## Repository layout / placement

Place the two source files into your ns‑3.41 tree **exactly** here:

```
ns-3.41/
└── contrib/
    └── oran/
        └── model/
            ├── oran-ru-energy-model.h
            └── oran-ru-energy-model.cc
```

You can keep the PDF at the repo root (or `docs/`) for reference.

---

## Prerequisites

- **ns‑3.41** built with CMake.
- **NIST ns3‑oran** module cloned into `contrib/oran` (https://github.com/usnistgov/ns3-oran).
- C++17 compiler.

> The RU device energy model touches the LTE PHY (`LteEnbPhy`) to read **TxPower**, so the **LTE** and **Energy** modules must be available/linked.

---

## Build integration (CMake, contrib/oran)

1) **Drop the files** into `contrib/oran/model/` as shown above.

2) **Add them to the oran library target.** In `contrib/oran/CMakeLists.txt`, find the `build_lib(` section for the oran library and extend it like this (snippet):

```cmake
# contrib/oran/CMakeLists.txt

build_lib(
  LIBNAME ${liboran}
  SOURCE_FILES
    # ... existing sources ...
    model/oran-ru-energy-model.cc
  HEADER_FILES
    # ... existing headers ...
    model/oran-ru-energy-model.h
  LIBRARIES_TO_LINK
    # ... existing deps ...
    ${liblte}
    ${libenergy}
)
```

3) **Configure & build** from the ns‑3 root:

```bash
./ns3 configure --enable-examples
./ns3
# or, explicitly:
cmake -S . -B build
cmake --build build -j
```

If CMake can’t find `energy` during link, ensure `${libenergy}` is listed in `LIBRARIES_TO_LINK` as shown above.

---

## Using the model in your scenario

Include the header and attach the RU device model to an energy source. If you pass an `LteEnbPhy` pointer, the model will use `phy->GetTxPower()` live; otherwise it falls back to the `TxPowerDbm` attribute on the device model (default **30 dBm**).

```cpp
#include "ns3/basic-energy-source.h"
#include "ns3/lte-module.h"
#include "ns3/oran-ru-energy-model.h"
using namespace ns3;

// 1) Create an energy source (default 48 V matches the model)
Ptr<BasicEnergySource> src = CreateObject<BasicEnergySource>();
src->SetSupplyVoltage(48.0);

// 2) Create the RU device energy model and bind it to the source
Ptr<OranRuDeviceEnergyModel> em = CreateObject<OranRuDeviceEnergyModel>();
em->SetEnergySource(src);
src->AppendDeviceEnergyModel(em);

// 3) Attach to the eNB PHY to read live TxPower (dBm)
Ptr<LteEnbNetDevice> enb = /* ... your eNB ... */;
Ptr<LteEnbPhy> phy = enb->GetPhy();
em->SetLteEnbPhy(phy);  // live TxPower; otherwise uses TxPowerDbm

// 4) Configure RU hardware parameters (optional)
Ptr<OranRuPowerModel> pm = em->GetRuPowerModel();
pm->SetAttribute("NumTrx", UintegerValue(64));
pm->SetAttribute("EtaPA", DoubleValue(0.30));
pm->SetAttribute("FixedOverheadW", DoubleValue(80.0));
pm->SetAttribute("MmwaveOverheadW", DoubleValue(0.0));
pm->SetAttribute("DeltaAf", DoubleValue(0.0));
pm->SetAttribute("DeltaDC", DoubleValue(0.07));
pm->SetAttribute("DeltaMS", DoubleValue(0.09));
pm->SetAttribute("DeltaCool", DoubleValue(0.10));
pm->SetAttribute("Vdc", DoubleValue(48.0));
pm->SetAttribute("SleepPowerW", DoubleValue(5.0));
pm->SetAttribute("SleepThresholdDbm", DoubleValue(0.0));
pm->SetAttribute("LossesInSleep", BooleanValue(false));

// 5) Optional: connect traces
em->TraceConnectWithoutContext("PowerW", MakeCallback(&OnPowerW));
em->TraceConnectWithoutContext("CurrentA", MakeCallback(&OnCurrentA));
em->TraceConnectWithoutContext("TxPowerDbmTrace", MakeCallback(&OnTxDbm));

// 6) Retrieve total energy at the end of the run
double joules = em->GetTotalEnergyConsumption();
```

**Traces exposed by the device model**
- `CurrentA` — instantaneous RU current (A)
- `PowerW` — instantaneous RU power (W)
- `TxPowerDbmTrace` — TxPower actually used (from PHY or fallback attribute)

---

## Attributes & defaults

**On `OranRuPowerModel` (hardware & losses)**

| Attribute             | Default  | Notes |
|----------------------|----------|-------|
| `EtaPA`              | `0.30`   | Power amplifier efficiency (0..1) |
| `FixedOverheadW`     | `80.0`   | Per‑TRX fixed overhead (RF+BB+misc) [W] |
| `MmwaveOverheadW`    | `0.0`    | Additional mmWave overhead (per‑TRX) [W] |
| `DeltaAf`            | `0.0`    | Antenna feeder loss (fraction, e.g., 0.5 ≈ 3 dB) |
| `DeltaDC`            | `0.07`   | DC‑DC conversion loss (fraction) |
| `DeltaMS`            | `0.09`   | Mains supply loss (fraction) |
| `DeltaCool`          | `0.10`   | Cooling loss (fraction) |
| `NumTrx`             | `1`      | Number of TRX chains |
| `Vdc`                | `48.0`   | DC supply voltage [V] |
| `SleepPowerW`        | `5.0`    | Per‑TRX sleep/standby power [W] |
| `SleepThresholdDbm`  | `0.0`    | TxPower at/below which RU is in sleep |
| `LossesInSleep`      | `false`  | Apply supply/cooling losses to sleep power |

**On `OranRuDeviceEnergyModel` (integration)**

| Attribute       | Default        | Notes |
|-----------------|----------------|-------|
| `TxPowerDbm`    | `30.0` dBm     | Fallback when no `LteEnbPhy` attached |
| `PowerModel`    | *(created)*    | Pointer to the `OranRuPowerModel` used |
| `LteEnbPhy`     | `nullptr`      | Optional pointer; when set, model reads `GetTxPower()` |

---

## Validated workflow (from the paper)

The paper demonstrates ns‑3.41 runs with `ns3‑oran`, sweeping TxPower (e.g., **20–49 dBm**), logging RU power/current via traces, and computing total energy and energy efficiency (bits/J). Use the attributes above to match your RU profile and scenario.

---

## Troubleshooting

- **Undefined references to energy classes** while linking `liboran`: ensure `${libenergy}` is in `LIBRARIES_TO_LINK` for the oran module.
- **No current change during the run**: either attach `LteEnbPhy` (live TxPower) or set `TxPowerDbm` explicitly on the device model.
- **Header not found in your example**: include it as `#include "ns3/oran-ru-energy-model.h"` in your program, and make sure your example links against `oran` (and its transitive deps).

---

## Citing

If you use this model, please cite:

> A. Wadud and N. Afraz, “Radio Unit Energy Modeling for Open RAN in ns3-oran,” 2025.

---

## License

SPDX‑License‑Identifier: **BSD‑3‑Clause** (see headers).
