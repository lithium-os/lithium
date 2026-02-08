ENTRY(_start)

SECTIONS {
    . = 0xFFFFFFFF80000000;
    
    .limine_requests : {
        KEEP(*(.limine_requests))
    }
    
    .text : {
        *(.text)
    }
    
    .rodata : {
        *(.rodata)
    }
    
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
    }
}