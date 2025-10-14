/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "bitmap.h"
#include "threads/mmu.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */

static struct bitmap *swap_table; /* free/used swap slot을 관리한다. */
static struct lock swap_lock;     /* 스왑 테이블 접근 동기화를 위한 락 */
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;

static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = disk_get(1,1);
  if (swap_disk == NULL){
    PANIC("No swap disk found!");
  }
  swap_table = bitmap_create(disk_size(swap_disk) / SECTORS_PER_PAGE);
  if (swap_table == NULL)
    PANIC("Failed to create swap bitmap!");
  lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
  anon_page->swap_slot = BITMAP_ERROR;

  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {  // 디스크->메모리
  struct anon_page *anon_page = &page->anon;

  size_t swap_index = anon_page->swap_slot;
  if (!bitmap_test(swap_table, swap_index)) {
    return false;
  }
  if (swap_index == BITMAP_ERROR) {
    PANIC("swap_in index is ERROR");
    return false;
  }

  for (int i = 0; i < 8; i++) {
    disk_read(swap_disk, swap_index * 8 + i, kva + i * DISK_SECTOR_SIZE);
  }

  page->frame->kva = kva;

  lock_acquire(&swap_lock);
  bitmap_set(swap_table, swap_index, false);
  lock_release(&swap_lock);

  return true;
}

static bool anon_swap_out(struct page *page) {  // 메모리->디스크
  /* 스왑 디스크나 스왑 테이블이 초기화되지 않은 경우 실패 반환 */
  if (swap_disk == NULL || swap_table == NULL) {
    return false;
  }
  /* 익명 페이지 정보 획득 */
  struct anon_page *anon_page = &page->anon;
  lock_acquire(&swap_lock);
  /* 스왑 테이블에서 빈 슬롯 찾아서 할당 */
  size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
  lock_release(&swap_lock);
  /* 빈 슬롯을 찾지 못한 경우 실패 반환 */
  if (swap_index == BITMAP_ERROR) {
    return false;
  }
  anon_page->swap_slot = swap_index;
  /* 페이지 내용을 스왑 디스크에 섹터 단위로 기록 */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    disk_write(swap_disk, swap_index * SECTORS_PER_PAGE + i, page->frame->kva + i * DISK_SECTOR_SIZE);
  }

  /* 페이지에 할당된 스왑 슬롯 번호 저장 및 프레임 정보 초기화 */
  page->frame->page = NULL;
  page->frame = NULL; /* 물리 프레임에서 제거됨을 표시 */

  pml4_clear_page(thread_current()->pml4, page->va);
  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
/* 익명 페이지를 삭제하는 함수
 * - 페이지가 스왑 아웃된 경우 해당 스왑 슬롯을 해제함
 * - 페이지 구조체는 호출자에 의해 해제됨
 * @param page 삭제할 익명 페이지 구조체
 */
static void anon_destroy(struct page *page) {
  /* 스왑 디스크나 스왑 테이블이 초기화되지 않은 경우 early return */
  if (swap_disk == NULL || swap_table == NULL) {
    return;
  }

  /* 페이지의 익명 페이지 정보 획득 */
  struct anon_page *anon_page = &page->anon;

  /* 페이지가 스왑 슬롯을 점유하고 있는 경우에만 해제 작업을 수행 */
  if (anon_page->swap_slot != BITMAP_ERROR) {
    /* 스왑 테이블 접근을 위한 락 획득 */
    lock_acquire(&swap_lock);
    /* 비트맵에서 해당 슬롯을 사용 가능 상태로 표시 */
    bitmap_set(swap_table, anon_page->swap_slot, false);
    lock_release(&swap_lock);
  }
}
