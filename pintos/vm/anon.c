/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "bitmap.h"
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
  swap_disk = NULL;
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
/* 페이지를 스왑 디스크로 내보내는 함수
 * - 스왑 테이블에서 빈 슬롯을 찾아 할당
 * - 페이지 내용을 스왑 디스크에 기록
 * - 페이지 상태 업데이트
 * @param page 스왑 아웃할 페이지 구조체
 * @return 스왑 아웃 성공 시 true, 실패 시 false 반환
 */
static bool anon_swap_out(struct page *page) {
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

  /* 페이지 내용을 스왑 디스크에 섹터 단위로 기록 */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    disk_write(swap_disk, swap_index * SECTORS_PER_PAGE + i, page->frame->kva + i * DISK_SECTOR_SIZE);
  }

  /* 페이지에 할당된 스왑 슬롯 번호 저장 및 프레임 정보 초기화 */
  anon_page->swap_slot = swap_index;
  page->frame = NULL; /* 물리 프레임에서 제거됨을 표시 */

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

  /* 스왑 슬롯 해제 작업 수행
   * 1. 스왑 테이블 접근을 위한 락 획득
   * 2. 비트맵에서 해당 슬롯을 사용 가능 상태로 표시
   * 3. 락 해제
   */
  lock_acquire(&swap_lock);
  bitmap_set(swap_table, anon_page->swap_slot, false);
  lock_release(&swap_lock);
}
