/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"  // SPT/페이지 구조체(struct page, spt) 선언들

#include <stdint.h>  // 🅢 uintptr_t: 포인터 비교 시 정수 변환용

#include "hash.h"
#include "lib/kernel/hash.h"  // 🅢 Pintos 커널 해시 테이블 API(hash_init/hash_find/...)
#include "threads/malloc.h"
#include "vm/inspect.h"

// 🅛
#include "threads/interrupt.h"  // struct intr_frame (f->rsp 접근)
#include "threads/thread.h"     // thread_current(), struct thread
#include "threads/vaddr.h"      // is_user_vaddr, pg_round_down, PHYS_BASE

// 🅒
#include "lib/string.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/*🅢 [키->해시값] 해시테이블이 쓸 해시값을 계산 -> 해시테이블이 버킷을 선택*/
static unsigned page_hash(const struct hash_elem *e, void *aux) {
  const struct page *p = hash_entry(e, struct page, hash_elem);  // hash_elem 을 원래 page 객체로 되돌림
  return hash_bytes(&p->va, sizeof p->va);                       // 키(va)를 바이트로 섞어 '버킷 번호'를 뽑는 해시값 계산
}

/*🅢 [비교 함수] 같은 버킷 내 정렬/동일키 판정 기준(오름차순) */
static bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  const struct page *pa = hash_entry(a, struct page, hash_elem);
  const struct page *pb = hash_entry(b, struct page, hash_elem);
  return (uintptr_t)pa->va < (uintptr_t)pb->va;
}

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* 나중에 올릴 준비만 하는 PTE를 SPT에 등록*/
/*“읽을 바이트/제로 바이트”를 페이지 단위로 계산 -> 대기 페이지 등록만(실제 읽기·매핑은 page fault 때)*/
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT)  // type 이 UNINIT 이라면 PANIC

  upage = pg_round_down(upage);
  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */
    struct page *page = malloc(sizeof(*page));
    if (page == NULL) {
      goto err;
    }
    bool uninitialized = NULL;
    switch (VM_TYPE(type)) { /* uninit_new()를 이용해 "uninitialized page"로 설정 */
      case VM_ANON:
        uninit_new(page, upage, init, type, aux, anon_initializer);
        // anon_initializer()이 아니라anon_initializer인 이유는 함수 포인터 함수의 주소를 저장
        break;
      case VM_FILE:
        uninit_new(page, upage, init, type, aux, file_backed_initializer);
        break;
      default:
        break;
    }

    page->writable = writable;

    /* TODO: Insert the page into the spt. */
    if (!spt_insert_page(spt, page)) {
      free(page);
      goto err;
    }
    return true;
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* 🅢 "주소 → page 메타데이터"를 해시테이블에서 찾음*/
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
  struct page temp_page; /* 검색을 위한 임시 페이지 구조체 생성 */
  struct hash_elem *e;   /* 해시 테이블 요소를 가리키는 포인터 선언 */

  /* pg_round_down : 특정 가상 주소(va)가 속한 가상 페이지의 시작 주소를 계산해준다. */
  temp_page.va = pg_round_down(va); /* 정렬된 주소를 페이지의 가상 주소로 설정 */

  /* 임시 페이지의 hash_elem을 '검색 키'로 사용해 해시 테이블을 검색한다. */
  e = hash_find(&spt->hash, &temp_page.hash_elem);

  /* 페이지를 찾은 경우 */
  if (e == NULL) {
    /* 못 찾았으면 NULL을 반환한다. */
    return NULL;
  } else {
    /* 찾았으면, hash_elem 주소로부터 실제 page 구조체의 시작 주소를
     * 계산해서 반환한다. */
    return hash_entry(e, struct page, hash_elem);
  }
}

/* Insert PAGE into spt with validation. */
/* 🅢 같은 키가 이미 있었으면 그 ‘기존 원소’를 돌려주고, 없었으면 NULL을 돌려줌*/
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
  int succ = false;
  /* TODO: Fill this function. */

  if (hash_insert(&spt->hash, &page->hash_elem) == NULL) {
    succ = true;
  }

  return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  hash_delete(&spt->hash, &page->hash_elem);
  vm_dealloc_page(page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim UNUSED = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */

  return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*🅕 프레임 실물 확보(+프레임 메타 생성)*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */
  void *kva = palloc_get_page(PAL_USER);  // ✅ 플래그는 PAL_USER
  if (kva == NULL) {
    PANIC("todo");
  }
  frame = malloc(sizeof(struct frame));  // 위에 성공이면 프레임구조체도 할당
  if (frame == NULL) {
    PANIC("Frame malloc failed");
  }
  frame->kva = kva;
  // frame->page->va = NULL;  // 멤버들초기화
  frame->page = NULL;
  // frame->page->frame = frame;  // page에서 frame 접근할수있게 설정
  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;  // 반환
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {
  /** Project 3-Stack Growth*/
  bool success = false;
  addr = pg_round_down(addr);
  if (vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true)) {
    success = vm_claim_page(addr);

    if (success) {
      thread_current()->stack_bottom -= PGSIZE;
    }
  }
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/** Project 3-Stack Growth*/
#define STACK_LIMIT (USER_STACK - (1 << 20))

/**
 * @brief 페이지 폴트를 처리하는 함수
 * 
 * @param f 인터럽트 프레임 구조체 포인터
 * @param addr 페이지 폴트가 발생한 가상 주소
 * @param user 유저 모드에서 발생한 폴트인지 여부
 * @param write 쓰기 접근으로 인한 폴트인지 여부
 * @param not_present 해당 페이지가 존재하지 않아서 발생한 폴트인지 여부
 * 
 * @return 페이지 폴트 처리 성공 시 true, 실패 시 false 반환
 * 
 * @details 페이지 폴트가 발생했을 때 호출되며, 다음과 같은 경우들을 처리:
 * - 스택 확장이 필요한 경우 스택을 증가시킴
 * - 페이지가 SPT에 있는 경우 해당 페이지를 물리 메모리에 로드
 * - 잘못된 메모리 접근인 경우 false를 반환
 */
/*🅛*/
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;

  /** Project 3-Anonymous Page */
  struct page *page = NULL;

  if (addr == NULL || is_kernel_vaddr(addr))
    return false;

  if (not_present) {
    /** Project 3-Stack Growth*/
    // 시스템 콜 중에는 f->rsp가 커널 주소를 가리킬 수 있으므로 thread_current()->rsp를 사용합니다.
    void *rsp = user ? f->rsp : thread_current()->rsp;

    /* 스택 확장 로직
     * addr >= rsp - 8 : push 명령어처럼 스택 포인터 바로 아래 주소에 접근할 때를 처리
     * (USER_STACK >= addr && addr >= STACK_LIMIT && addr >= rsp)
     * - addr이 최대 스택 크기(1MB) 제한인 STACK_LIMIT과 USER_STACK 사이의 유효한 범위에 있고,
     *   현재 스택 포인터 rsp보다 위쪽(높은 주소)에 있는 경우를 처리한다. (= 스택에 큰 버퍼를 잡고 접근할 때 발생할 경우를 처리)
     */
    if (addr >= STACK_LIMIT && addr < USER_STACK && (addr >= rsp - 8)) {
      vm_stack_growth(addr);
      return true;
    }
    page = spt_find_page(spt, addr);

    if (!page || (write && !page->writable))
      return false;

    return vm_do_claim_page(page);
  }
  return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
/* 🅕 va가 가리키는 가상 페이지를 ‘실제 메모리에 올려’ 지금 당장 사용 가능하게 만듦 */
bool vm_claim_page(void *va) {
  //   struct page *page = NULL;
  /* TODO: Fill this function */
  //   1. 주소  & SPT 조회
  void *upage = pg_round_down(va);                               // 주소를 페이지 시작 주소로 설정(=키)
  struct supplemental_page_table *spt = &thread_current()->spt;  // SPT 주소
  struct page *page = spt_find_page(spt, upage);                 // 존재 여부 검색

  // 2. SPT 등록 확인
  if (!page) return false;        // 없으면 -> 실패
  return vm_do_claim_page(page);  // 있으면 -> 실제 메모리에 올리기
}

/* Claim the PAGE and set up the mmu. */
/* 🅕 실제 데이터 프레임에 채우기 + mmu에 매핑 */
static bool vm_do_claim_page(struct page *page) {
  if (page == NULL) {
    return false;
  }
  /* 빈 프레임을 얻는다. */
  struct frame *frame = vm_get_frame();
  /* 프레임 할당에 실패한 경우 */
  if (frame == NULL) {
    return false;
  }
  /* 페이지와 프레임을 서로 연결한다. */
  frame->page = page;
  page->frame = frame;
  /* 페이지의 가상 주소(VA)를 프레임의 물리 주소(PA)에 매핑 */
  if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
    /* 자원은 할당받았지만 가상-물리 주소 매핑에 실패한 경우 */
    /* 할당받았던 자원들을 모두 해제한다. */
    palloc_free_page(frame->kva);
    free(frame);
    /* 페이지와 프레임의 연결을 끊어 댕글링 포인터를 방지한다. */
    page->frame = NULL;
    return false;
  }
  /* 페이지의 종류를 파악하고, 알맞은 위치에서 데이터를 읽어와 물리 프레임에 복사한다. */
  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
/*🅢 [초기화] SPT를 해시 테이블로 “사용 가능 상태”로 만듦*/
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  hash_init(&spt->hash, page_hash, page_less, NULL);
}

// /* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
  struct hash_iterator i;
  hash_first(&i, &src->hash);

  while (hash_next(&i)) {
    struct page *s_page = hash_entry(hash_cur(&i), struct page, hash_elem);

    if (s_page->operations->type == VM_UNINIT) {
      struct uninit_page *u = &s_page->uninit;

      // 자식을 위한 lazy_aux 복사본 생성
      struct lazy_aux *new_aux = malloc(sizeof(struct lazy_aux));
      if (!new_aux) {
        return false;
      }
      memcpy(new_aux, u->aux, sizeof(struct lazy_aux));
      new_aux->file = file_reopen(new_aux->file); // 자식만의 파일 핸들 생성

      if (!vm_alloc_page_with_initializer(s_page->uninit.type, s_page->va, s_page->writable, u->init, new_aux)) {
        return false;
      }
    } else { /* Already loaded page */
      if (!vm_alloc_page(page_get_type(s_page), s_page->va, s_page->writable)) {
        return false;
      }
      struct page *dpage = spt_find_page(dst, s_page->va);
      if (!dpage) {
        return false;
      }
      struct frame *dframe = vm_get_frame();
      if (!dframe) {
        return false;
      }
      dframe->page = dpage;
      dpage->frame = dframe;
      memcpy(dframe->kva, s_page->frame->kva, PGSIZE);
      if (!pml4_set_page(thread_current()->pml4, dpage->va, dframe->kva, dpage->writable)) {
        return false;
      }
    }
  }
  return true;
}

/* [1] 해시 원소(=페이지) 하나를 “어떻게” 지울지 알려주는 콜백 */
static void spt_destructor(struct hash_elem *e, void *aux UNUSED) {
  struct page *page = hash_entry(e, struct page, hash_elem);
  destroy(page);  // 내부에서 page->operations->destroy(page) 호출되게 구현
}

/* [2] 스레드가 들고 있는 SPT 전체를 안전하게 정리 */
/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */

  if (!spt || !spt->hash.buckets || !spt->hash.bucket_cnt) return;  // 예외처리(SPT 자체가 없음, 버킷 메모리 없음)

  hash_clear(&spt->hash, spt_destructor);

  // struct hash_iterator i;
  // hash_first(&i, &spt->hash);

  // while (hash_next(&i)) {
  //   struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);
  //   destroy(page);
  // }

  // hash_clear(&spt->hash, NULL);
}

bool lazy_load_segment(struct page *page, void *aux) {
  /* void * 포인터를 원래의 구조체 포인터로 사용하도록 형 변환하기 */
  struct lazy_aux *args = (struct lazy_aux *) aux;

  /* 어느 파일의 어디서부터(offset) 읽어야할지를 정한다. (=커서 옮기기) */
  file_seek(args->file, args->ofs);

  /* file에서 read_bytes만큼 데이터를 읽어서 물리 메모리(kva)에 넣는다. (=로딩) */
  if (file_read(args->file, page->frame->kva, args->read_bytes) != (int) args->read_bytes) {
    free(args);
    return false;
  }

  /* 페이지에 남는 공간이 있다면 0으로 채워넣는다. (=초기화) */
  /* read_bytes만큼 채우고 남은 공간의 시작 주소에서 zero_bytes만큼 0으로 채운다. */
  memset(page->frame->kva + args->read_bytes, 0, args->zero_bytes);

  free(args);
  return true;
}
