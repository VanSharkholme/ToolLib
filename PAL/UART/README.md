# UART PAL

`include/pal_uart.h` is the application-facing API. Modules receive a physical
`PalUartId` from board configuration. IDs need not be contiguous, and must be
unique across USART, UART and LPUART instances. Receive APIs accept `to_idle`;
pass `false` for fixed-length reception.

## Common state and platform responsibilities

PAL owns binding (`UNBOUND`, `OWNED`, `ATTACHED`), availability and the last
successfully applied portable configuration. The selected Port supplies stable,
zero-initialized `PalUartDevice` storage and an opaque hardware context through
`PAL_UART_Port_Find`. Storage may be embedded in the Port's device table; PAL
does not allocate memory or assume that IDs are array indexes.

The Port implements the internal contract in `port/pal_uart_port.h`. It maps
physical resources, converts configurations, calls the vendor driver and reports
actual transfer counts. Board defaults, clock sources, pins, MSP, FIFO settings,
DMA linkage and IRQ routing belong to the Port/BSP or generated code. Platform
checks include wired directions, driver state, length limits, DMA memory/cache
requirements and actual DMA bindings. There is no public capability-query API.

PAL decides which operations require idle TX/RX directions; the Port reports
actual outstanding transfers through `PAL_UART_Port_CheckIdle`. Failed hardware
configuration must not masquerade as an outstanding transfer and block recovery.
An unimplemented operation or unbound DMA mode returns `UNSUPPORTED`.

## Lifecycle and failure handling

`PAL_UART_Init(id)` uses board-defined defaults and takes `OWNED` binding only
on success. The Port rejects externally initialized hardware with `BUSY` and
cleans up partial hardware initialization on failure. Use `AttachInitialized`
after external/vendor initialization instead; it validates an idle asynchronous
UART and imports its current configuration without reinitializing hardware.
The binding grants PAL exclusive use of that vendor handle until release.

`Reconfigure` changes baud rate, payload data bits (excluding parity), parity,
stop bits and TX/RX directions. Vendor-specific fields are preserved. It requires
a bound, idle device, including when recovering an unavailable device. The Port
checks resources and builds a candidate before modifying running configuration.
It reports `modified=false` for rejection before a write, and sets it to true
before the first vendor-handle configuration or hardware write. Failure before
modification preserves prior availability; failure afterwards makes the device
unavailable and retains its binding and last successful configuration. Transfers
then return `INVALID_STATE` until successful reconfiguration or release. PAL
does not retry or roll back automatically.

`DeInit` releases only Init-owned instances and performs hardware teardown.
`Detach` releases only attached instances and leaves hardware/configuration
unchanged. Release checks ID, binding, ownership, then outstanding transfers.
The wrong release path returns `INVALID_STATE`; an unbound instance returns
`NOT_INITIALIZED`. Availability failure does not prevent the correct release.
Neither initialization path permits a second binding. Detach clears only PAL's
state; reattachment imports the still-existing hardware settings.

## Transfers and caller synchronization

PAL and its Ports provide no locking, scheduling or reentrancy guarantee. The
caller coordinates tasks, interrupts, callbacks and access to shared vendor
resources. Do not bypass PAL to operate a bound handle. Whether an operation is
usable in a particular ISR also depends on the underlying driver's constraints;
blocking calls may depend on a tick that cannot advance in that ISR.

Blocking calls return actual byte counts, including partial counts on timeout.
The output is zeroed before validation. A transfer in one direction does not
prevent a transfer in the other direction. A normal transfer timeout does not
invalidate the device.

PAL performs common argument, binding, availability, direction and idle checks
and records asynchronous requests. The Port handles pending buffers, hardware
completion and cancellation. Accepted requests complete exactly
once, including cancel; rejected requests never invoke the callback. Release
the request before invoking user code so callbacks can submit work again.
Callbacks may run in interrupt context or before `Start*` returns. Buffers must
remain valid until completion; DMA constraints remain platform-specific.

For `Read` and `StartRx`, `to_idle=false` requires the requested length for normal
completion. With `to_idle=true`, `length` is buffer capacity: filling the buffer
or detecting IDLE after at least one byte completes reception successfully.
IDLE before any data does not complete a request. The actual count is returned
through `transferred`; IDLE itself is neither a timeout nor an error. The blocking
timeout covers the whole read; `StartRx` does not configure a receive timeout.
Async reception remains one-shot and must be submitted again after completion;
DMA half-transfer events do not complete a request. Continuous/circular DMA is
not part of this interface. Ports without idle reception return `UNSUPPORTED`.

## Builds and tests

The header-only `pal_uart_contract` target is shared by PAL, Ports and BSPs;
Ports do not link back to PAL's implementation. The host Port is selected by
default for native builds. Cross builds default to `TOOLLIB_UART_PORT=none`
until the application supplies a hardware Port target. The STM32H5 implementation
in `Port/stm32/UART` provides blocking TX/RX, IRQ/normal-DMA transfers and cancel.
It selects the corresponding HAL ReceiveToIdle operation when `to_idle=true`.
Board resources and UART/DMA interrupt routing remain application responsibilities.

The host fake keeps simulated hardware state separate from PAL state and does
not wait in real time. It has no IDLE event model: both receive APIs reject
`to_idle=true` with `UNSUPPORTED` before consuming queued data or next-start fault
injection, and without creating a pending request or invoking a callback.
Tests cover that rejection behavior, sparse IDs, lifecycle ownership, pre-write and
post-write reconfiguration failures, recovery, per-direction busy checks, byte
counts and the retained asynchronous behavior. `PAL_UART_Host_Reset` resets both
hardware and embedded common state between tests. Existing reconfiguration error
injection rejects before modification; `SetNextModifiedReconfigureResult` injects
a failure after applying the simulated hardware settings.
