#pragma once
#include "runtime_base.h"
#ifdef __SWITCH__


/* Interface: module */


#include <elf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <switch.h>


struct GrnyxLinker;

typedef enum GrnyxModuleState {
    GRNYX_MODULE_EMPTY = 0,
    GRNYX_MODULE_LOADED,
    GRNYX_MODULE_LAYOUT_READY,
    GRNYX_MODULE_RESERVED,
    GRNYX_MODULE_DYNAMIC_PARSED,
    GRNYX_MODULE_RELOCATING,
    GRNYX_MODULE_RELOCATED,
    GRNYX_MODULE_MAPPED,
    GRNYX_MODULE_CONSTRUCTED,
    GRNYX_MODULE_FINALIZED,
    GRNYX_MODULE_FAILED
} GrnyxModuleState;

typedef struct GrnyxRelocStats {
    size_t relative;
    size_t glob_dat;
    size_t jump_slot;
    size_t abs64;
    size_t relr;
    size_t tls_dtpmod;
    size_t tls_dtprel;
    size_t tls_tprel;
    size_t tlsdesc;
    size_t irelative;
    size_t ifunc;
    size_t weak_zero;
    size_t unresolved;
    size_t unsupported;
} GrnyxRelocStats;

typedef struct GrnyxTlsInfo {
    bool present;
    uint64_t vaddr;
    size_t file_size;
    size_t mem_size;
    size_t alignment;
    size_t module_id;
    ptrdiff_t static_offset;
    bool registered;
} GrnyxTlsInfo;

typedef struct GrnyxDynamicInfo {
    /*
     * Keep ELF virtual addresses as the canonical dynamic symbol/string-table
     * locations. Readers select the writable backing view before mapping and
     * the runtime alias after Horizon code-memory mapping.
     */
    uint64_t strtab_vaddr;
    size_t strsz;

    uint64_t symtab_vaddr;
    size_t syment;
    size_t symbol_count;

    Elf64_Rela *rela;
    size_t rela_count;

    /* Android packed RELA (APS2) is decoded at dynamic-parse time into owned
     * Elf64_Rela entries so the normal relocation engine remains canonical. */
    Elf64_Rela *android_rela;
    size_t android_rela_count;

    Elf64_Rela *jmprel;
    size_t jmprel_count;

    Elf64_Xword *relr;
    size_t relr_count;
    bool relr_is_android;

    uint64_t init_vaddr;
    uint64_t init_array_vaddr;
    size_t init_array_count;
    uint64_t fini_vaddr;
    uint64_t fini_array_vaddr;
    size_t fini_array_count;

    uint64_t hash_vaddr;
    uint64_t gnu_hash_vaddr;

    uint64_t versym_vaddr;
    uint64_t verneed_vaddr;
    size_t verneed_count;
    uint64_t verdef_vaddr;
    size_t verdef_count;

    uint32_t *needed_name_offsets;
    size_t needed_count;
    uint32_t soname_offset;

    /* Dynamic-linker policy bits. Android/Bionic uses DT_FLAGS_1/DF_1_GLOBAL
     * to build the process-wide global lookup group, and DT_SYMBOLIC or
     * DT_FLAGS/DF_SYMBOLIC to request local-first binding for definitions in
     * this DSO. Keep the raw flag words so the linker can reproduce that
     * policy without title-specific exceptions. */
    uint64_t flags;
    uint64_t flags_1;
    bool symbolic;
} GrnyxDynamicInfo;

typedef struct GrnyxModule {
    char path[0x200];

    void *file_image;
    size_t file_size;

    Elf64_Ehdr *ehdr;
    Elf64_Phdr *phdrs;
    Elf64_Shdr *shdrs;

    uint64_t min_vaddr;
    uint64_t max_vaddr;
    size_t image_size;

    void *backing;
    void *runtime_base;
    uintptr_t load_bias;
    struct GrnyxVmRange *reservation;
    bool mapped;
    bool post_map_relocations_applied;

    struct GrnyxLinker *linker;

    GrnyxDynamicInfo dynamic;
    GrnyxTlsInfo tls;
    GrnyxRelocStats reloc_stats;
    GrnyxModuleState state;
} GrnyxModule;

void grnyx_module_init(GrnyxModule *module);
void grnyx_module_unload(GrnyxModule *module);

void *grnyx_module_runtime_address(const GrnyxModule *module, uint64_t elf_vaddr);
void *grnyx_module_backing_address(const GrnyxModule *module, uint64_t elf_vaddr);
bool grnyx_module_backing_contains(const GrnyxModule *module, uint64_t elf_vaddr, size_t size);
bool grnyx_module_runtime_contains(const GrnyxModule *module, uintptr_t runtime_address, size_t size);


/* Interface: dynamic */


#include <stdbool.h>



GrnyxResult grnyx_dynamic_parse(GrnyxModule *module);
GrnyxResult grnyx_dynamic_run_constructors(GrnyxModule *module);
GrnyxResult grnyx_dynamic_run_destructors(GrnyxModule *module);

/* Enable verbose AArch64 constructor tracing around real-APK bring-up.
 * Kept off for the retained Stage 1-12 fixtures so diagnostics do not alter
 * their normal output or timing. */
void grnyx_dynamic_set_constructor_trace(bool enabled);

GrnyxResult grnyx_dynamic_get_needed(const GrnyxModule *module, size_t index, const char **out_name);
GrnyxResult grnyx_dynamic_get_soname(const GrnyxModule *module, const char **out_name);


/* Interface: symbols */


#include <elf.h>
#include <stdint.h>



typedef struct GrnyxSymbolVersion {
    const char *name;
    const char *provider; /* DT_VERNEED vn_file for imported versions. */
    uint32_t hash;
    bool hidden;
} GrnyxSymbolVersion;

typedef struct GrnyxResolvedSymbol {
    uintptr_t address;
    const char *name;
    GrnyxSymbolVersion version;
    const GrnyxModule *provider;
    unsigned type;
    bool weak_zero;
} GrnyxResolvedSymbol;

GrnyxResult grnyx_symbol_get(const GrnyxModule *module, size_t symbol_index,
                             const Elf64_Sym **out_symbol, const char **out_name);
GrnyxResult grnyx_symbol_resolve(GrnyxModule *module, size_t symbol_index,
                                 GrnyxResolvedSymbol *out);
/* Re-resolve a relocation symbol after mapping without recording a second
 * compatibility bind or relocation failure statistic. Used by the deferred
 * IFUNC pass, which revisits already-accounted relocations. */
GrnyxResult grnyx_symbol_resolve_post_map(GrnyxModule *module, size_t symbol_index,
                                          GrnyxResolvedSymbol *out);
GrnyxResult grnyx_symbol_probe(const GrnyxModule *module, size_t symbol_index,
                               const char **out_name, bool *out_weak_zero);

GrnyxResult grnyx_symbol_version(const GrnyxModule *module, size_t symbol_index,
                                   GrnyxSymbolVersion *out_version);
GrnyxResult grnyx_symbol_find_defined_versioned_ex(const GrnyxModule *module, const char *name,
                                                   const GrnyxSymbolVersion *required_version,
                                                   uintptr_t *out_address, unsigned *out_type);
GrnyxResult grnyx_symbol_find_defined_versioned(const GrnyxModule *module, const char *name,
                                                const GrnyxSymbolVersion *required_version,
                                                uintptr_t *out_address);
GrnyxResult grnyx_symbol_find_tls_defined_versioned(const GrnyxModule *module, const char *name,
                                                    const GrnyxSymbolVersion *required_version,
                                                    size_t *out_offset);
GrnyxResult grnyx_symbol_resolve_tls(const GrnyxModule *requester, size_t symbol_index,
                                     const GrnyxModule **out_provider, size_t *out_offset,
                                     GrnyxSymbolVersion *out_version);
GrnyxResult grnyx_symbol_find_defined(const GrnyxModule *module, const char *name,
                                      uintptr_t *out_address);


/* Interface: linker */


#include <stdbool.h>
#include <stddef.h>




typedef struct GrnyxLinkerNode GrnyxLinkerNode;

typedef struct GrnyxLinker {
    GrnyxLinkerNode **nodes;
    size_t node_count;
    size_t node_capacity;
    size_t compat_bind_count;
    char library_dir[0x200];
} GrnyxLinker;

void grnyx_linker_init(GrnyxLinker *linker, const char *library_dir);
void grnyx_linker_unload(GrnyxLinker *linker);

/* Load one root module plus its complete DT_NEEDED graph. */
GrnyxResult grnyx_linker_load(GrnyxLinker *linker, const char *path, GrnyxModule **out_root);

/* Stage 14 Java Activity bootstrap may know the complete native root set before
 * Java executes the corresponding System.loadLibrary() calls. Discover the
 * union graph and reserve guest addresses only. Relocation, mapping,
 * .init/.init_array and JNI_OnLoad remain tied to the actual load operation so
 * unrelated RTLD_LOCAL roots cannot leak into one another's symbol scope. */
GrnyxResult grnyx_linker_prepare_roots(GrnyxLinker *linker,
                                       const char *const *paths,
                                       size_t path_count,
                                       GrnyxModule **out_primary_root);

/* Run one already-mapped module's dependency constructors exactly once. Safe to
 * call repeatedly; dependencies are constructed before the requested module.
 * A constructor failure is latched: later calls return it without re-running. */
GrnyxResult grnyx_linker_construct_module(GrnyxLinker *linker, const GrnyxModule *module);

/* True once the module's DT_INIT/DT_INIT_ARRAY have completed, i.e. once the
 * module is "loaded" in the Android-observable dlopen() sense. A module that
 * prepare_roots() mapped with deferred constructors reports false. */
bool grnyx_linker_module_constructed(const GrnyxLinker *linker, const GrnyxModule *module);

/* Eager convenience path retained for NativeActivity and host tests: prepare the
 * complete graph, then construct every requested root in dependency order. */
GrnyxResult grnyx_linker_load_roots(GrnyxLinker *linker,
                                    const char *const *paths,
                                    size_t path_count,
                                    GrnyxModule **out_primary_root);

/* Android System.loadLibrary() may request another root after the initial Java
 * bootstrap graph is already live. Add one lib*.so root to the existing graph
 * without relocating/reconstructing modules that are already loaded. */
GrnyxResult grnyx_linker_load_library(GrnyxLinker *linker,
                                      const char *soname,
                                      GrnyxModule **out_module,
                                      bool *out_newly_loaded);

/* Resolve one undefined guest symbol against the loaded dependency scope. */
GrnyxResult grnyx_linker_resolve_import_versioned(const GrnyxModule *requester,
                                                  const char *name,
                                                  const GrnyxSymbolVersion *version,
                                                  uintptr_t *out_address,
                                                  const GrnyxModule **out_provider);
GrnyxResult grnyx_linker_resolve_import(const GrnyxModule *requester,
                                        const char *name,
                                        uintptr_t *out_address,
                                        const GrnyxModule **out_provider);

/* Side-effect-free import lookup used by relocation preflight. Unlike the
 * normal resolver it does not increment compatibility bind counters or emit
 * per-bind success logs. */
GrnyxResult grnyx_linker_resolve_tls_import_versioned(const GrnyxModule *requester,
                                                      const char *name,
                                                      const GrnyxSymbolVersion *version,
                                                      const GrnyxModule **out_provider,
                                                      size_t *out_offset);

GrnyxResult grnyx_linker_probe_import_versioned(const GrnyxModule *requester,
                                                const char *name,
                                                const GrnyxSymbolVersion *version,
                                                uintptr_t *out_address,
                                                const GrnyxModule **out_provider);
GrnyxResult grnyx_linker_probe_import(const GrnyxModule *requester,
                                      const char *name,
                                      uintptr_t *out_address,
                                      const GrnyxModule **out_provider);

/* dlsym(handle, name) searches the handle's local dependency group, not just
 * the root object. Version is NULL for ordinary dlsym and non-NULL for future
 * dlvsym-style lookups. */
GrnyxResult grnyx_linker_find_in_handle(const GrnyxLinker *linker,
                                        const GrnyxModule *module,
                                        const char *name,
                                        const GrnyxSymbolVersion *version,
                                        uintptr_t *out_address,
                                        const GrnyxModule **out_provider);
GrnyxResult grnyx_linker_find_global(const GrnyxLinker *linker,
                                     const char *name,
                                     const GrnyxSymbolVersion *version,
                                     uintptr_t *out_address,
                                     const GrnyxModule **out_provider);

/* Stable dlopen handle lifetime. Java/System.loadLibrary roots are pinned;
 * native dlopen references are counted and dlclose runs eligible destructors. */
GrnyxResult grnyx_linker_acquire_module(GrnyxLinker *linker, const GrnyxModule *module);
GrnyxResult grnyx_linker_release_module(GrnyxLinker *linker, const GrnyxModule *module);
GrnyxResult grnyx_linker_pin_module(GrnyxLinker *linker, const GrnyxModule *module);
bool grnyx_linker_module_handle_live(const GrnyxLinker *linker, const GrnyxModule *module);

/* Resolve only against Bionic's process-wide guest global group. Used when a
 * relocation references a default-visibility symbol defined by the requester:
 * such a definition is preemptible unless the DSO requested symbolic binding
 * or the symbol is STV_PROTECTED. */
GrnyxResult grnyx_linker_probe_global_versioned(const GrnyxModule *requester,
                                                const char *name,
                                                const GrnyxSymbolVersion *version,
                                                uintptr_t *out_address,
                                                const GrnyxModule **out_provider);
GrnyxResult grnyx_linker_probe_global(const GrnyxModule *requester,
                                      const char *name,
                                      uintptr_t *out_address,
                                      const GrnyxModule **out_provider);

/* dlopen(..., RTLD_GLOBAL) promotes an already-loaded local root so its symbols
 * are visible to later loads. DF_1_GLOBAL modules are promoted automatically. */
GrnyxResult grnyx_linker_promote_global(GrnyxLinker *linker,
                                        const GrnyxModule *module,
                                        const char *reason);

size_t grnyx_linker_module_count(const GrnyxLinker *linker);
size_t grnyx_linker_compat_bind_count(const GrnyxLinker *linker);
const GrnyxModule *grnyx_linker_module_at(const GrnyxLinker *linker, size_t index);


/* Interface: loader */



/* Generic single-module loader retained for Stage 1/2 regression tests. */
GrnyxResult grnyx_loader_load(const char *path, GrnyxModule *module);

/* Stage 1 regression path: succeeds only when the guest requires no relocations. */
GrnyxResult grnyx_loader_load_stage1(const char *path, GrnyxModule *module);

GrnyxResult grnyx_loader_find_export(const GrnyxModule *module, const char *name, void **out_address);

/* Stage 3 linker building blocks. These do not run constructors implicitly. */
GrnyxResult grnyx_loader_prepare(const char *path, GrnyxModule *module);
/* Header-only classification for speculative Java native-library candidates. */
GrnyxResult grnyx_loader_probe_shared_object(const char *path, bool *out_shared);
GrnyxResult grnyx_loader_relocate(GrnyxModule *module);
GrnyxResult grnyx_loader_patch_system_registers(GrnyxModule *module);
GrnyxResult grnyx_loader_map(GrnyxModule *module);
GrnyxResult grnyx_loader_resolve_ifuncs(GrnyxModule *module);
GrnyxResult grnyx_loader_construct(GrnyxModule *module);


/* Interface: relocations */



GrnyxResult grnyx_apply_relr(GrnyxModule *module);
GrnyxResult grnyx_preflight_relocations(GrnyxModule *module, size_t *out_unique_missing);
GrnyxResult grnyx_apply_relocations(GrnyxModule *module);
GrnyxResult grnyx_apply_post_map_relocations(GrnyxModule *module);


#endif /* __SWITCH__ */
