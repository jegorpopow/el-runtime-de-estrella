#include <stdio.h>
#include <stdlib.h>
#include <stella/gc.h>
#include <stella/runtime.h>

static StellaObjectDescriptor const data2 = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = 0,
    .entry_arity     = 0,
    .managed_count   = 2,
    .primitive_count = 0,
    .slot_capacity   = 2,
    .entry_code      = NULL,
    .debug_name      = "data2"
};

static StellaObjectDescriptor const data1 = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = 0,
    .entry_arity     = 0,
    .managed_count   = 1,
    .primitive_count = 0,
    .slot_capacity   = 1,
    .entry_code      = NULL,
    .debug_name      = "data1"
};

static StellaObjectDescriptor const upd2 = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = STELLA_DESCRIPTOR_UPDATABLE,
    .entry_arity     = 0,
    .managed_count   = 2,
    .primitive_count = 0,
    .slot_capacity   = 2,
    .entry_code      = NULL,
    .debug_name      = "upd2"
};

static StellaGcConfig config(size_t bytes) {
    return (StellaGcConfig){.abi_version    = STELLA_C_GC_ABI_VERSION,
                            .max_heap_bytes = bytes};
}
