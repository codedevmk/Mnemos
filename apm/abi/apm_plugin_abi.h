#ifndef MNEMOS_APM_PLUGIN_ABI_H
#define MNEMOS_APM_PLUGIN_ABI_H

/*
 * The contract between the APM tracer host and an emulator plugin. Pure C, zero
 * dependencies, so the same header compiles into the host and into each engine
 * binding DLL. The host LoadLibrary's a binding, resolves apm_plugin_entry by
 * name, and drives the returned vtable. The engine proper never sees this header
 * -- only the thin binding does.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APM_PLUGIN_ABI_VERSION 1u
#define APM_PLUGIN_ENTRY_SYMBOL "apm_plugin_entry"

/* CPU register / counter ids for read_register(). D0..D7 = APM_REG_D0+n, A0..A7 = APM_REG_A0+n. */
enum apm_reg {
    APM_REG_PC = 0,     /* next-instruction program counter */
    APM_REG_INST = 1,   /* address of the instruction currently issuing a bus access */
    APM_REG_SR = 2,     /* status register */
    APM_REG_CYCLES = 3, /* total CPU cycles executed (a free-running perf counter) */
    APM_REG_D0 = 16,    /* data registers D0..D7 */
    APM_REG_A0 = 24     /* address registers A0..A7 */
};

/* A tagged guest memory bank the host may observe. */
typedef struct apm_bank_info {
    void* host_ptr;      /* host backing-store address (live, in-process) */
    uint64_t size;       /* bank size in bytes */
    uint64_t guest_base; /* base guest address this bank maps at */
    uint32_t chip;       /* engine-assigned chip id */
    uint32_t bank;       /* bank id within the chip */
    const char* name;    /* human label, e.g. "work_ram" */
} apm_bank_info;

typedef struct apm_plugin apm_plugin; /* opaque instance handle */

/*
 * The plugin's exported capability table. A binding DLL exports a single function
 *   const apm_plugin_api* apm_plugin_entry(void);
 * returning a pointer to a static instance of this struct.
 */
typedef struct apm_plugin_api {
    uint32_t abi_version;    /* must equal APM_PLUGIN_ABI_VERSION */
    const char* system_name; /* e.g. "genesis" */

    apm_plugin* (*create)(void);
    void (*destroy)(apm_plugin* self);

    /* Load a raw cartridge image and reset the machine. Returns 0 on success. */
    int (*load_rom)(apm_plugin* self, const uint8_t* data, size_t size);

    /* Advance exactly one video frame; returns the new frame index. */
    uint64_t (*run_frame)(apm_plugin* self);
    uint64_t (*frame_index)(apm_plugin* self);

    /* Read a CPU register (see enum apm_reg). */
    uint64_t (*read_register)(apm_plugin* self, int reg);

    /* Enumerate tagged memory banks. get_bank returns 0 on success. */
    int (*bank_count)(apm_plugin* self);
    int (*get_bank)(apm_plugin* self, int index, apm_bank_info* out);
} apm_plugin_api;

/* Signature of the exported entry point the host resolves by name. */
typedef const apm_plugin_api* (*apm_plugin_entry_fn)(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MNEMOS_APM_PLUGIN_ABI_H */
