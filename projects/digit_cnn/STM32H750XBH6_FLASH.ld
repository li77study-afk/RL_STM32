/* FK750M6-XBH6: Flash 128 KiB, AXI display staging,
 * DTCM stack/diagnostics, SDRAM 16 MiB with two NOLOAD DMA frames. */

ENTRY(Reset_Handler)

/* 栈顶 = DTCMRAM 末尾,向下增长 */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);

_Min_Heap_Size  = 0x400;   /* 最小堆 1KB */
_Min_Stack_Size = 0x2000;   /* 最小栈 2KB */

MEMORY
{
  SDRAM (xrw) : ORIGIN = 0xC0000000, LENGTH = 16M
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  DTCMRAM  (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1   (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
}

PHDRS
{
 text PT_LOAD FLAGS(5);
 data PT_LOAD FLAGS(6);
 bss PT_LOAD FLAGS(6);
 dtcm PT_LOAD FLAGS(6);
 sdram PT_LOAD FLAGS(6);
}

SECTIONS
{
  /* 中断向量表 (必须位于 FLASH 起始处) */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH :text

  /* 代码 */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* 代码结束 */
  } >FLASH :text

  /* 常量 (字库、图像数据等) */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH :text

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH :text

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH :text

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH :text

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH :text

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH :text

  /* .data 的加载地址 (在 FLASH 中) */
  _sidata = LOADADDR(.data);

  /* 已初始化全局变量 -> RAM_D1 (AXI SRAM) */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM_D1 AT> FLASH :data

  /* 未初始化全局变量 -> RAM_D1 (Camera_Buffer 150KB 在这里) */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM_D1 AT>RAM_D1 :bss

  /* 堆 + 最小栈占位 -> DTCMRAM 底部,实际栈从 _estack 向下 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM :dtcm

  .diagnostics (NOLOAD) : { . = ALIGN(32); *(.diagnostics) *(.cnn_workspace) . = ALIGN(32); } > DTCMRAM :dtcm

  .sdram (NOLOAD) : { . = ALIGN(32); *(.sdram .sdram.*) . = ALIGN(32); } > SDRAM :sdram

  /* 丢弃未用到的库代码段 */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
