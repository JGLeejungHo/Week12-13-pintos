/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"  // SPT/페이지 구조체(struct page, spt) 선언들

#include <stdint.h>  // 🅢 uintptr_t: 포인터 비교 시 정수 변환용

#include "hash.h"
#include "lib/kernel/hash.h"  // 🅢 Pintos 커널 해시 테이블 API(hash_init/hash_find/...)
#include "threads/malloc.h"
#include "vm/inspect.h"

<<<<<<< HEAD
=======
// 🅛
#include "threads/interrupt.h"  // struct intr_frame (f->rsp 접근)
#include "threads/thread.h"     // thread_current(), struct thread
#include "threads/vaddr.h"      // is_user_vaddr, pg_round_down, PHYS_BASE

>>>>>>> dev
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
<<<<<<< HEAD
=======
/*“읽을 바이트/제로 바이트”를 페이지 단위로 계산 -> 대기 페이지 등록만(실제 읽기·매핑은 page fault 때)*/
>>>>>>> dev
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
<<<<<<< HEAD
    // struct page *page = malloc(sizeof *page);
    // if (!page) goto err;
=======
    if (!spt_insert_page(spt, page)) {
      free(page);
      goto err;
    }
    return true;
  }
err:
  return false;
}
>>>>>>> dev

    // switch (VM_TYPE(type)) {
    //   case VM_ANON:

    //   case VM_FILE:
    //   default:
    //     free(page);
    //     goto err;
    // }

    // page->writable = writable;
    // if (!spt_insert_page(spt, page)) free(page);
    // goto err;
    // return true;
  err:
    return false;
  }
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
  vm_dealloc_page(page);
  return true;
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
<<<<<<< HEAD
// 🅕
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */
  void *kva = palloc_get_page(sizeof(USERPROG));  // 사용자 풀에서 물리페이지 가져오기
=======
/*🅕 프레임 실물 확보(+프레임 메타 생성)*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */
  void *kva = palloc_get_page(PAL_USER);  // ✅ 플래그는 PAL_USER
>>>>>>> dev
  if (kva == NULL) {
    PANIC("todo");
  }
  frame = malloc(sizeof(struct frame));  // 위에 성공이면 프레임구조체도 할당
  if (frame == NULL) {
    PANIC("Frame malloc failed");
  }
  frame->kva = kva;
<<<<<<< HEAD
  frame->page->va = NULL;  // 멤버들초기화
  frame->page = NULL;
  frame->page->frame = frame;  // page에서 frame 접근할수있게 설정
=======
  // frame->page->va = NULL;  // 멤버들초기화
  frame->page = NULL;
  // frame->page->frame = frame;  // page에서 frame 접근할수있게 설정
>>>>>>> dev
  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;  // 반환
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
/*🅛*/
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
  struct page *page = NULL;
  /* TODO: Validate the fault */
  /* TODO: Your code goes here */

  // 1. 예외 처리
  if (!not_present) return false;                   // 물리 메모리 가상 주소
  if (!addr || !is_user_vaddr(addr)) return false;  // 유저 주소 유효성

  // 2. 페이지 경계 주소 → SPT 조회
  void *va = pg_round_down(addr);
  page = spt_find_page(spt, va);

  // 3. 없으면:유저 모드 한정 스택 성장 허용
  if (!page) {
    /* 스택 확장 조건 검사:
     * 1. 폴트 주소가 USER_STACK 범위 안에 있어야 함
     * 2. 폴트 주소가 현재 유저 스택 포인터보다 아래에 있어야 함 (스택은 높은 주소에서 낮은 주소로 자람)
     * 3. 너무 큰 갭(e.g., 1MB)을 건너뛴 스택 확장은 방지 (선택적)
     */
    // void *rsp = user ? f->rsp : thread_current()->rsp;
    uintptr_t rsp;
    if (user)
      rsp = f->rsp;
    else
      rsp = thread_current()->rsp;
    if (! (is_user_vaddr(addr) && (USER_STACK - (1 << 20) < addr) && (addr < USER_STACK) && (addr >= rsp - 8))) {
      return false;
    }

    if (!vm_alloc_page_with_initializer(VM_ANON, va, true, NULL, NULL)) return false;

    page = spt_find_page(spt, va);
    if (!page) return false;
  }

  // 4. 쓰기 권한 체크
  if (write && !page->writable) return false;

  // 5. 클레임(프레임 확보+로드/제로필+매핑)
  return vm_do_claim_page(page);
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

<<<<<<< HEAD
/* 🅕 Claim the PAGE and set up the mmu. */
=======
/* Claim the PAGE and set up the mmu. */
/* 🅕 실제 데이터 프레임에 채우기 + mmu에 매핑 */
>>>>>>> dev
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

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
}
