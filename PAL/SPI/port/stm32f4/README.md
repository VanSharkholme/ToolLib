# STM32F401 SPI PAL

Set `TOOLLIB_SPI_PORT=stm32f4` before adding ToolLib. Link the application to
`pal_spi`. The port consumes the `stm32cubemx` and `bsp` CMake targets.
Build `example/bsp_spi.c` in the board's `bsp` target and export the directory
containing `bsp_spi.h`. The BSP needs the include paths from `pal_spi_contract`
and `stm32cubemx`. The example uses the handles in the generated
`spi.h`. Adapt the example for boards with different peripherals or DMA routing.

The BSP supplies stable handles and instances, default HAL and PAL configurations,
and IRQ/DMA support per direction. `BSP_SPI_NUM` covers the highest mapped ID
plus one, including any holes. Getters return NULL or no support for holes.
Use `PAL_SPI_Init` for a reset handle or `PAL_SPI_AttachInitialized` after
CubeMX initialization. Board code owns clock setup, MSP, pins and NVIC routing.

## Interrupts

Include `pal_spi_stm32.h` in the interrupt file. Wrap the HAL handler once
for every configured SPI IRQ and every linked TX and RX DMA stream. For example,

```c
void SPI2_IRQHandler(void)
{
    PAL_SPI_STM32_BeforeSpiIRQ(&hspi2);
    HAL_SPI_IRQHandler(&hspi2);
    PAL_SPI_STM32_AfterSpiIRQ(&hspi2);
}

void DMA1_Stream3_IRQHandler(void)
{
    PAL_SPI_STM32_BeforeDmaIRQ(&hdma_spi2_rx);
    HAL_DMA_IRQHandler(&hdma_spi2_rx);
    PAL_SPI_STM32_AfterDmaIRQ(&hdma_spi2_rx);
}
```

The port defines the HAL completion, abort and error callbacks. Use the default
HAL callback bindings. Coordinate callers and interrupt priorities as required
by the PAL. Completions may run in the IRQ handler and may submit another request.

## F401 behavior

- The example maps SPI2 and SPI3, with APB1 at 42 MHz and a default 5.25 MHz limit.
  The port reads the current APB clock and selects a divisor from 2 through 256.
- Frames are 8 or 16 bits. Lengths and callback counts are bytes. A request must
  contain whole, naturally aligned frames and fit the HAL limit of 65535 frames.
  NSS is active low. With software NSS the application drives the external chip
  select GPIO as needed. F4 hardware output NSS follows peripheral enable.
- Separate-line TX-only uses the two-line hardware configuration. Attaching that
  configuration imports TX/RX capability. Shared-line TX/RX is sequential.
  In two-line master receive the port fills the receive buffer with 0xff for
  the dummy MOSI clocks. Use TransmitReceive to specify the outgoing data.
- DMA requires normal, single-buffer direct mode, fixed peripheral address,
  incrementing memory and byte/halfword alignment matching the frame. Two-line
  master DMA reception requires both streams. A failure after RX starts is
  rejected once RX has stopped. If cleanup is still pending, start returns OK
  and the abort is delivered through the completion callback.
- After master RXONLY or single-line receive the port runs HAL DeInit/Init.
  The board's MSP hooks must support repeated setup, including from IRQ context.
  On hardware, check consecutive receives and the last SCK edge with a logic analyzer.
