/* Independently authored linker extension. SDK script remains external. */
INCLUDE memmap_default.ld
/* Use 16 KiB at top of main RAM, rather than the 4 KiB scratch-bank stack. */
__StackTop = ORIGIN(RAM) + LENGTH(RAM);
__StackBottom = __StackTop - 16384;
/* Bound libc allocation to the explicit 8 KiB heap, leaving a large unused guard. */
__HeapLimit = __end__ + 8192;
__StackLimit = __HeapLimit;
__stack = __StackTop;
ASSERT(__HeapLimit <= __StackBottom, "Simulation RAM/stack budget exceeded")
ASSERT(__flash_binary_end <= 0x101fe000, "Firmware overlaps reserved configuration sectors")
