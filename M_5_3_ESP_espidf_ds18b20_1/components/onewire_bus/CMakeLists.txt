idf_component_register(
    SRCS "onewire_crc.c"    
    "onewire_device.c"
    "onewire_bus_impl_rmt.c"
    "onewire_bus_impl_uart.c"    
    "onewire_bus_api.c"   
    INCLUDE_DIRS "." 
    REQUIRES driver      
)
