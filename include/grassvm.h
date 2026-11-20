#ifndef GRASSVM_H
#define GRASSVM_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #if defined(GRASSVM_EXPORTS)
    #define GRG_API __declspec(dllexport)
  #else
    #define GRG_API __declspec(dllimport)
  #endif
#else
  #define GRG_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GrgVM GrgVM;

GRG_API GrgVM* grg_vm_create(void);
GRG_API void grg_vm_destroy(GrgVM* vm);
GRG_API int grg_vm_load_file(GrgVM* vm, const char* path);
GRG_API int grg_vm_eval(GrgVM* vm, const char* code);
GRG_API int grg_vm_set_int(GrgVM* vm, const char* name, int64_t val);
GRG_API int grg_vm_set_str(GrgVM* vm, const char* name, const char* val);
GRG_API int grg_vm_call(GrgVM* vm, const char* func_name);
GRG_API const char* grg_vm_get_error(GrgVM* vm);

#ifdef __cplusplus
}
#endif

#endif // GRASSVM_H
