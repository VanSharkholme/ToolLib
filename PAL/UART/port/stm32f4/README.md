# STM32F401 UART PAL

Set `TOOLLIB_UART_PORT=stm32f4` before adding ToolLib. Link the application to
`pal_uart`. The port consumes the `stm32cubemx` and `bsp` CMake targets.
Build `example/bsp_uart.c` in the board's `bsp` target and export the directory
containing `bsp_uart.h`. The BSP needs the include paths from `pal_uart_contract`
and `stm32cubemx`. The example uses the handles in the generated
`usart.h`. Adapt the example for boards with different peripherals or DMA routing.

The BSP supplies stable handles and instances, default HAL and PAL configurations,
and IRQ/DMA support per direction. `BSP_UART_NUM` covers the highest mapped ID
plus one, including any holes. Getters return NULL or no support for holes.
Use `PAL_UART_Init` for a reset handle or `PAL_UART_AttachInitialized` after
CubeMX initialization. Board code owns clock setup, MSP, pins and NVIC routing.

## Interrupts

Include `pal_uart_stm32.h` in the interrupt file. Wrap the HAL handler once
for every configured UART IRQ and every linked TX and RX DMA stream. For example,

```c
void USART6_IRQHandler(void)
{
    PAL_UART_STM32_BeforeUartIRQ(&huart6);
    HAL_UART_IRQHandler(&huart6);
    PAL_UART_STM32_AfterUartIRQ(&huart6);
}

void DMA2_Stream1_IRQHandler(void)
{
    PAL_UART_STM32_BeforeDmaIRQ(&hdma_usart6_rx);
    HAL_DMA_IRQHandler(&hdma_usart6_rx);
    PAL_UART_STM32_AfterDmaIRQ(&hdma_usart6_rx);
}
```

The port defines the HAL completion, abort and error callbacks. Use the default
HAL callback bindings. Coordinate callers and interrupt priorities as required
by the PAL. Completions may run in the IRQ handler and may submit another request.

## F401 behavior

- The example maps USART1, USART2 and USART6 with 115200 baud, eight payload
  bits, one stop bit and normal TX/RX DMA. Supported formats are 8N, 7E/O
  and 8E/O with one or two stop bits. The limit is 65535 bytes per operation.
- DMA uses normal, single-buffer direct mode, byte-width accesses, a fixed
  peripheral address and incrementing memory. The USART IRQ is needed even
  for DMA TX completion. In 7-bit DMA reception the port masks parity from
  received bytes before delivering the callback.
- Receive-to-IDLE completes on a full buffer or IDLE after data. DMA half
  transfer only indicates progress. F401 has no programmable receiver timeout.
  Check actual IDLE timing, final TX completion and cancellation followed by
  immediate buffer reuse on the target.
