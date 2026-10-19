# Generic Diagnostics Library

A small C99 diagnostics core for embedded firmware that needs fault tracking,
reset history, device identity, and optional persistence without being tied to
CAN, ISO-TP, flash drivers, filesystems, or an RTOS.

This library is for products that eventually need to answer questions like:

- What fault happened, and is it still active?
- Is the fault pending, confirmed, or aging out?
- How many times did it occur or clear?
- Did the device reset abnormally?
- Which product, device type, and instance reported the problem?
- Which diagnostic state should survive a reset, and when is it safe to write it?

`libdiag` owns the diagnostic state machine. Your firmware owns the platform.
You decide how faults are detected, where bytes are stored, how messages are
framed, and when storage writes are allowed.

## Why This Exists

Many embedded projects start with a few fault flags and later grow into a mix of
runtime errors, persistent trouble codes, reset counters, service tools, and
bootloader/application handoff state. That usually becomes product-specific code
that is hard to reuse and easy to tie to one bus or storage device.

This project keeps those concerns separated:

- The core tracks DTC state, counters, operation-cycle behavior, lifecycle state,
  and compact identity.
- Storage and transport are callback interfaces supplied by the application.
- Persistence uses an explicit serialized capsule instead of raw C structures.
- Runtime memory is caller-owned and fixed at initialization.
- Feature switches remove unused modules from constrained builds.

The core avoids heap allocation, hidden flash writes, platform locks, and
protocol-specific assumptions.

## UDS In Plain English

UDS, or Unified Diagnostic Services, is the automotive diagnostic model defined
by ISO 14229. In practice, it is a request/response language that lets an
external tester ask a device what it is, what went wrong, whether faults are
current or historical, and whether certain service actions should be performed.

UDS is commonly carried over CAN with ISO-TP. The diagnostic model itself is
larger than that transport choice: service IDs, subfunctions, data identifiers,
DTCs, status bits, positive responses, and negative responses. A similar model
can be carried over CAN, Ethernet, UART, a test harness, or a product-specific
link when the framing layer supports it.

This project borrows the useful parts of that model, especially DTCs and clear
diagnostic state boundaries. The library provides the state behind a diagnostic
interface; a downstream project can expose that state through a UDS-like server,
a custom binary protocol, or a factory test tool.

### Why UDS Exists

As embedded systems became more complex, service tools needed a common way to
inspect devices without rebuilding firmware or attaching a debugger. A diagnostic
protocol solves that by giving an external tool controlled access to internal
state:

- Identify the device and firmware component being queried.
- Read current and historical trouble codes.
- Distinguish active, pending, confirmed, and aging faults.
- Clear selected diagnostic state after service.
- Read counters and lifecycle information after resets.
- Perform these actions through a predictable request/response contract.

That is the same need this library targets outside the automotive-only world:
debug the device from the outside when it is already built, sealed, deployed, or
running in a larger system.

### What A UDS-Style Message Looks Like

The exact bytes depend on the service, but the shape is simple. A tester sends a
service request. The device returns either a positive response or a negative
response.

```mermaid
sequenceDiagram
    participant Tool as External diagnostic tool
    participant Proto as Product protocol/framing
    participant Dev as Device firmware
    participant Diag as libdiag state

    Tool->>Proto: Request bytes
    Proto->>Dev: Decode service and parameters
    Dev->>Diag: Query identity, DTCs, counters, lifecycle
    Diag-->>Dev: Bounded diagnostic state
    Dev-->>Proto: Response payload
    Proto-->>Tool: Positive or negative response bytes
```

Example UDS-style service frames:

| Purpose | Request shape | Positive response shape |
|---------|---------------|-------------------------|
| Read data by identifier | `0x22 DID_H DID_L` | `0x62 DID_H DID_L data...` |
| Read DTC information | `0x19 subfunction filters...` | `0x59 subfunction DTC/status...` |
| Clear diagnostic information | `0x14 group_of_dtc` | `0x54` |

Negative responses also have a recognizable form:

```text
0x7F <original_service_id> <negative_response_code>
```

For example, a request to read an unsupported identifier might return:

```text
request:  22 F1 90
response: 7F 22 31
```

That means "service 0x22 failed with response code 0x31". A protocol layer above
this library can make those choices. `libdiag` focuses on the reliable internal
state that such responses need.

### Where This Library Fits

```mermaid
flowchart TB
    subgraph FaultSources["Things that can go wrong"]
        HW_INIT["hardware init failure"]
        HW_RUN["hardware runtime failure"]
        PROTO["protocol failure"]
        BOOT["boot/update issue"]
        LOGIC["application logic fault"]
        RESET["watchdog, brownout, abnormal reset"]
    end

    subgraph Firmware["Your firmware"]
        MON["fault monitors and reset detection"]
        API["diag_* API calls"]
        PROTOCOL["your diagnostic protocol layer"]
    end

    subgraph Lib["libdiag"]
        STATE["DTC, lifecycle, identity, counters"]
        DIRTY["dirty flags"]
        CAPSULE["optional persistent capsule"]
    end

    subgraph Outside["Outside the device"]
        TOOL["service tool, factory tool, host, test rig"]
    end

    FaultSources --> MON
    MON --> API
    API --> STATE
    STATE --> DIRTY
    DIRTY --> CAPSULE
    TOOL <-->|"read, clear, inspect"| PROTOCOL
    PROTOCOL --> STATE
```

A useful embedded diagnostics core should expose any failure the firmware can
detect:

- Hardware initialization failures.
- Sensor, actuator, memory, or peripheral failures.
- Communication/protocol errors.
- Bootloader, update, or application handoff issues.
- Watchdog resets, brownouts, or unexpected reset reasons.
- Application-level state machine faults.

Your firmware decides what each fault means and which faults deserve persistent
DTCs. The library makes those faults consistent, queryable, countable, clearable,
and optionally persistent.

## Mental Model

Your application monitors real conditions. When something crosses a threshold,
it reports that fact to `libdiag`. The library updates bounded RAM state and
marks persistent sections dirty when needed. Later, at a time your product
chooses, you can save the dirty state through your storage adapter.

```mermaid
graph LR
    APP["Application monitors<br/>temperature, voltage, watchdog, sensors"]
    CTX["diag_context<br/>caller-owned RAM"]
    DTC["DTC module<br/>pending, confirmed, aging, counters"]
    LIFE["Lifecycle module<br/>reset reason and reset counters"]
    ID["Identity module<br/>compact numeric identity"]
    CAPS["Capsule<br/>versioned persistent byte format"]
    STORE["Your storage adapter<br/>flash, EEPROM, RAM, file"]
    TRANS["Your transport adapter<br/>CAN, UART, TCP, BLE, test harness"]
    TOOL["Your protocol/tooling<br/>UDS-like, binary, JSON, custom"]

    APP -->|"diag_dtc_set_active()<br/>diag_lifecycle_observe_reset()"| CTX
    CTX --> DTC
    CTX --> LIFE
    CTX --> ID
    DTC -->|"dirty flag"| CAPS
    LIFE -->|"dirty flag"| CAPS
    CAPS -->|"diag_save() / diag_load()"| STORE
    TOOL --> TRANS
    TRANS -.-> CTX
```

The important boundary is this: setting a fault never writes flash. Fault paths
update RAM. Persistence happens only when your firmware calls `diag_save()`.

## Library Layers

`libdiag` is intentionally small and still has clear layers. The application
talks to the public API. The public API updates caller-owned diagnostic state.
Optional modules add DTCs, lifecycle, identity, persistence, and transport hooks.
Platform behavior stays outside the core behind callbacks.

```mermaid
flowchart TB
    subgraph AppLayer["Application and product logic"]
        MON["Fault monitors<br/>hardware checks, protocol checks, boot checks"]
        CYCLE["Operation-cycle scheduler<br/>when a diagnostic cycle ends"]
        SERVICE["Diagnostic service layer<br/>UDS-like or custom command handling"]
    end

    subgraph PublicAPI["Public C API"]
        API_CTX["context API<br/>diag_init, diag_deinit, dirty flags"]
        API_DTC["DTC API<br/>register, set active, clear, list"]
        API_LIFE["lifecycle API<br/>observe reset, snapshot counters"]
        API_ID["identity API<br/>attach and read numeric identity"]
        API_STORE["storage API<br/>attach, load, save, clear"]
        API_TRANS["transport API<br/>attach send/receive callbacks"]
    end

    subgraph Core["Core state in caller-owned RAM"]
        CTX["diag_context<br/>opaque runtime state"]
        DTC["DTC records<br/>status bits, counters, aging"]
        LIFE["Lifecycle state<br/>reset reason and counters"]
        ID["Identity state<br/>ecosystem, product, device, firmware"]
        DIRTY["Dirty flags<br/>sections that should be saved"]
    end

    subgraph Persistence["Persistence format"]
        CAPS["Capsule encoder/decoder<br/>schema, sections, CRC, alignment"]
    end

    subgraph Platform["Platform adapters owned by the user"]
        STORE["Storage adapter<br/>flash, EEPROM, FRAM, file, RAM fake"]
        TRANS["Transport adapter<br/>CAN, UART, TCP, BLE, test harness"]
    end

    MON --> API_DTC
    CYCLE --> API_DTC
    MON --> API_LIFE
    SERVICE --> API_CTX
    SERVICE --> API_DTC
    SERVICE --> API_LIFE
    SERVICE --> API_ID
    SERVICE --> API_STORE
    SERVICE --> API_TRANS

    API_CTX --> CTX
    API_DTC --> DTC
    API_LIFE --> LIFE
    API_ID --> ID
    API_STORE --> CAPS
    API_TRANS --> TRANS

    DTC --> DIRTY
    LIFE --> DIRTY
    DIRTY --> CAPS
    CAPS --> STORE
```

### Layer Responsibilities

| Layer | Responsibility | Owned by |
|-------|----------------|----------|
| Application monitors | Detect real failures: hardware init, runtime hardware faults, protocol errors, boot issues, and application state faults. | Product firmware |
| Diagnostic service layer | Decode external commands and turn them into `diag_*` API calls. This can be UDS-like, binary, JSON, or project-specific. | Product firmware |
| Public C API | Provide a stable, transport-agnostic way to initialize the library, update DTCs, read state, attach adapters, and save/load state. | `libdiag` |
| Context/core layer | Hold enabled module state inside caller-owned memory with fixed capacities and no dynamic allocation. | `libdiag` and caller-provided memory |
| DTC module | Track registered trouble codes, UDS-style status bits, occurrence counters, clear counters, confirmation, and aging. | `libdiag` |
| Lifecycle module | Track reset reasons and reset counters according to an explicit persistence policy. | `libdiag` |
| Identity module | Store compact numeric identifiers that host tooling can map to product names, variants, and catalogs. | `libdiag` state, host-owned meaning |
| Dirty-state layer | Record which persistent sections changed so storage writes can be batched or deferred. | `libdiag` |
| Capsule layer | Serialize persistent DTC and lifecycle sections into a versioned byte format with integrity checks. | `libdiag` |
| Storage adapter | Load, save, and clear bytes from the actual medium; handle wear-leveling and erase/write rules. | Product firmware |
| Transport adapter | Send and receive diagnostic bytes over the project’s chosen link. | Product firmware |

The separation is deliberate. A DTC update should be cheap and bounded. Storage
writes happen through explicit save paths. Protocol parsing belongs above the
library. Flash, EEPROM, RTOS locks, and bus drivers stay in the application.

## What You Provide

| You provide | Why |
|-------------|-----|
| Fault monitors | Hardware limits and safety rules belong to the product. |
| Caller-owned memory | Context storage, DTC arrays, and capsule buffers are supplied by you. |
| Operation-cycle timing | You decide what a meaningful cycle means for the product. |
| Storage callbacks | Flash, EEPROM, files, RAM, and wear-leveling policy are platform concerns. |
| Transport callbacks | CAN, UART, TCP, BLE, and test harnesses all fit behind the same shape. |
| Protocol/framing layer | The library provides state that a UDS-like server or custom protocol can expose. |

## What The Library Provides

| Library surface | What it gives you |
|-----------------|-------------------|
| `diag_context` | The opaque runtime home for enabled modules. |
| DTC module | Fixed-capacity trouble codes, status bits, counters, confirmation, aging. |
| Lifecycle module | Reset reason handling and reset counter policies without write-on-boot defaults. |
| Identity module | Numeric ecosystem/product/device identity for host-side catalogs. |
| Storage module | Explicit load/save/clear adapter contract. |
| Capsule module | Versioned, CRC-protected persistence format for DTC and lifecycle sections. |
| Transport module | Minimal send/receive adapter hook for your diagnostic protocol layer. |

## First Integration

Start with RAM-only DTCs. This proves the core model without storage, transport,
or persistence:

```c
#include <diag/diag.h>

static struct diag_context_storage context_storage;
static struct diag_dtc_snapshot    dtc_records[4];

struct diag_context *ctx = NULL;
struct diag_config config = {0};

diag_init(&context_storage, &config, &ctx);

struct diag_dtc_config dtc_config = {
    .records = dtc_records,
    .capacity = 4u,
};

diag_dtc_attach(ctx, &dtc_config);
diag_dtc_register(ctx, 0x010001u, DIAG_DTC_SEVERITY_ERROR);

if (sensor_reading_is_invalid())
{
    diag_dtc_set_active(ctx, 0x010001u);
}
else
{
    diag_dtc_set_inactive(ctx, 0x010001u);
}

diag_dtc_operation_cycle(ctx);
```

That is enough to get a bounded DTC record with UDS-style status behavior and no
storage writes. Add lifecycle, identity, storage, capsule, or transport only
when the product needs them.

## Choose A Starting Example

Each example represents a product shape and an embedded tradeoff. Start with the
closest scenario:

| If your product needs... | Read this first | Why |
|--------------------------|-----------------|-----|
| A tiny link/lifetime check | `examples/basic` | Shows the smallest possible context integration. |
| A few runtime faults that reset on power cycle | `examples/sensor_node` | DTC + identity with no persistence cost. |
| Local DTC behavior only | `examples/dtc` | Focuses on registration, active state, counters, and operation cycles. |
| Reset reason/counter policy | `examples/lifecycle` | Shows reset tracking without forcing flash writes. |
| Numeric device identity only | `examples/identity` | Useful when host tooling owns names and catalogs. |
| Callback contracts | `examples/adapters` | Shows how storage and transport adapters are shaped. |
| A PC/tester reading a simulated device | `examples/diagnostic_session` | Shows the external workflow: read identity, list DTCs, clear a DTC, and persist the result. |
| Host-side DTC names and service text | `examples/catalog_reader` | Maps compact identity and DTC IDs to catalog-owned names, descriptions, and actions. |
| Read-only dashboard data from diagnostics | `examples/grafana_reader` | Runs a PC-side exporter, Prometheus, and Grafana dashboard without putting dashboard code in firmware. |
| Confirmed faults that survive restart | `examples/process_controller` | Adds capsule persistence for important confirmed state. |
| Critical thermal/reset diagnostics | `examples/industrial_oven` | Persists only important service data. |
| Separate bootloader and app diagnostics | `examples/bootloader_app_shared` | Uses separate capsule banks instead of shared raw structs. |
| A full embedded node profile | `examples/ecu_node` | Exercises identity, DTCs, lifecycle, storage, transport, and capsule. |

Each example README explains when to use that profile, what code to inspect, why
features are disabled, and what the output means.

## Quick Start

The default host presets use Clang 16 for C and C++ builds. Install `clang-16`,
`clang++-16`, `libclang-rt-16-dev`, `clang-format-14`, `cmake`, `ninja-build`,
`doxygen`, and `python3`, or use the Docker image below.

```sh
git clone https://github.com/Mrunmoy/diagnostics.git
cd diagnostics
git checkout c
./build.py all
```

If you prefer SSH, use `git@github.com:Mrunmoy/diagnostics.git`.

Useful commands:

```sh
./build.py build
./build.py test
./build.py test --preset linux-asan
./build.py all
./build.py feature-matrix
./build.py size --dtc-capacity 16 --write-alignment 16 --sections dtc,lifecycle
./build.py format --check
./build.py library
./build.py docs
./build.py clean
```

Pass CMake options after `--`:

```sh
./build.py build -- DIAG_FEATURE_DTC=ON DIAG_FEATURE_STORAGE=OFF
```

`./build.py all` is the main local gate. It runs formatting checks, debug tests,
ASAN/UBSAN tests, release installation, package-consumption smoke tests, size
reporting, and the feature matrix.

## Docker And VS Code

Build and test in Docker. The image includes Clang, the Clang sanitizer runtime,
`clang-format-14`, CMake, Ninja, Doxygen, and the Linux tools used by the
transport examples.

```sh
docker compose build
docker compose run --rm diagnostics-dev
```

Inside the devcontainer, use:

```sh
./build.py all --preset container-debug
```

VS Code users can open the repository in the Dev Containers extension and use
the provided CMake, CTest, and debug configurations.

## Repository Layout

```text
.
├── cmake/                  # CMake helper modules
├── .devcontainer/          # VS Code Dev Container definition
├── .vscode/                # Build, test, and debug tasks
├── docs/design.md          # Single design source of truth
├── examples/               # Scenario-focused reference integrations
├── include/diag/           # Public C API
├── src/                    # Library implementation
├── tests/                  # GoogleTest behavior tests
├── tools/docker/           # Docker build/test image
├── CMakeLists.txt
├── CMakePresets.json
├── docker-compose.yml
└── README.md
```

## Downstream Use

Most embedded projects should consume this repository as a submodule and keep
their concrete adapters in the application tree:

```text
application
├── third_party/generic-diagnostics
├── platform/my_flash_storage.c
├── platform/my_transport.c
└── app/diagnostic_protocol.c
```

As a submodule:

```cmake
add_subdirectory(third_party/generic-diagnostics)
target_link_libraries(app PRIVATE diag::diag)
```

As an installed CMake package:

```sh
./build.py library
```

That installs the reusable package to `build/install/diag` by default. A
downstream project can consume it without manually spelling out header or
library paths:

```cmake
find_package(diag CONFIG REQUIRED)
target_link_libraries(app PRIVATE diag::diag)
```

Configure the downstream project with:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/diagnostics/build/install/diag
cmake --build build
```

The exported `diag::diag` target carries the public include directory, C99
requirement, feature compile definitions, and compiled library. Application code
includes headers normally:

```c
#include <diag/diag.h>
```

## Branch Strategy

- `main`: intentionally empty or documentation-only landing branch.
- `c`: embedded C99 implementation.
- `cpp`: reserved for the parallel C++17 implementation.

Public behavior should eventually exist on both implementation branches, each in
the idiom of that language.

## Going Deeper

- [docs/design.md](docs/design.md) explains the architecture, capsule format,
  storage contract, protocol boundary, bootloader/application sharing model, and
  resource policy.
- `include/diag/` is the public API surface.
- `tests/` captures exact behavior contracts.
- `examples/` shows product-shaped integration profiles.
