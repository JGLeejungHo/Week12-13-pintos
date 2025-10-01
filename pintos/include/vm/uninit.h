#ifndef VM_UNINIT_H
#define VM_UNINIT_H
// #include "vm/vm.h"

// 🅒
#include <stdbool.h>  // bool
#include <stddef.h>   // size_t

// #include "vm/vm.h"
/* ✅ enum 정의를 여기 둔다 (전방선언 말고 ‘정의’) */
enum vm_type {
  VM_UNINIT = 0,
  VM_ANON = 1,
  VM_FILE = 2,
  VM_PAGE_CACHE = 3,
  VM_MARKER_0 = (1 << 3),
  VM_MARKER_1 = (1 << 4),
  VM_MARKER_END = (1 << 31),
};

/* 편의 매크로도 같이 두면 좋음 */
#define VM_TYPE(type) ((type) & 7)

struct page;
// enum vm_type;

typedef bool vm_initializer(struct page *, void *aux);

/* Uninitlialized page. The type for implementing the
 * "Lazy loading". */
struct uninit_page {
  /* Initiate the contets of the page */
  vm_initializer *init;
  enum vm_type type;
  void *aux;
  /* Initiate the struct page and maps the pa to the va */
  bool (*page_initializer)(struct page *, enum vm_type, void *kva);
};

void uninit_new(struct page *page, void *va, vm_initializer *init,
                enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
