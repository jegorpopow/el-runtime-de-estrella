#include "common.h"

static StellaObjectDescriptor const stat2 = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = STELLA_DESCRIPTOR_STATIC,
    .entry_arity     = 0,
    .managed_count   = 2,
    .primitive_count = 0,
    .slot_capacity   = 2,
    .entry_code      = NULL,
    .debug_name      = "stat2"
};

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaSlot slots[2] = {{.managed = NULL}, {.managed = NULL}};
    StellaObject obj    = {.descriptor = &stat2, .gc_word = 0, .fields = slots};
    StellaValue oroot = &obj, vroot = NULL;
    stella_gc_push_root(&oroot);
    stella_gc_push_root(&vroot);
    stella_gc_write_managed(&oroot, 0, &vroot);
    return 0;
}