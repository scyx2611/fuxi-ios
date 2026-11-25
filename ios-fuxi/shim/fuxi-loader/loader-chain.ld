/* loader.ld plus an embedded Linux Image (chain.c): the Image starts on the next
 * 2 MiB boundary after the XNU payload and runs in place, so the image size in the
 * ARM64 header (_image_end) covers its whole footprint, BSS included
 * (--defsym LINUX_IMAGE_SIZE=<image_size from the Linux header>). */
ENTRY(_head)
SECTIONS {
 . = 0;
 .text : { *(.text.head) *(.text*) }
 .rodata : { *(.rodata*) }
 .gpu_fw : { *(.gpu_fw) }
 .data : { *(.data*) }
 . = ALIGN(16384);
 _bss_start = .;
 .bss : { *(.bss*) *(COMMON) }
 . = ALIGN(16);
 _bss_end = .;
 . += 0x8000;
 _stack_top = .;
 . += 0x8000;
 _secondary_stack_top = .;
 _payload_start = .;
 .payload : { *(.payload) }
 _payload_end = .;
 . = ALIGN(8);
 .sensor_pack : { *(.sensor_pack) }
 . = ALIGN(0x200000);
 _linux_start = .;
 .linux : { KEEP(*(.linux)) }
 _linux_end = .;
 . = _linux_start + LINUX_IMAGE_SIZE;
 _image_end = .;
 _end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
