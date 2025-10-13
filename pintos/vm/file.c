/* file.c: Implementation of memory backed file object (mmaped object). */

#include "round.h"
#include "stdio.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init (void) {
}

/**
 * @brief 파일 기반 페이지를 초기화하는 함수
 * 
 * @param page 초기화할 페이지 구조체
 * @param type 가상 메모리 타입 (VM_FILE이어야 함)
 * @param kva 커널 가상 주소
 * 
 * @return true 초기화 성공
 * @return false 초기화 실패
 * 
 * @details
 * 페이지를 파일 기반 페이지로 초기화하고 필요한 핸들러를 설정합니다.
 * 파일 기반 페이지는 디스크의 파일과 매핑되어 있으며,
 * 필요할 때 파일에서 내용을 읽어오거나 변경된 내용을 파일에 쓸 수 있습니다.
 */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
	page->operations = &file_ops;
	ASSERT(VM_TYPE(type) == VM_FILE);

	// 🔍 여기부터 추가
	// struct lazy_aux *aux_dbg = (struct lazy_aux *)page->uninit.aux;
	// if (aux_dbg == NULL) {
	// 	PANIC("[FILE_INIT] aux=NULL  va=%p thr=%s init=%p type=%d",
	// 		  page->va, thread_current()->name, page->uninit.init, (int)page->uninit.type);
	// } else {
	// 	printf("[FILE_INIT] aux=%p va=%p file=%p ofs=%lld rb=%zu zb=%zu thr=%s\n",
	// 		   aux_dbg, page->va, aux_dbg->file, (long long)aux_dbg->ofs,
	// 		   aux_dbg->read_bytes, aux_dbg->zero_bytes, thread_current()->name);
	// }
	// 🔍 추가 끝

	struct file_page *file_page = &page->file;

	struct lazy_aux *aux = (struct lazy_aux *)page->uninit.aux;
	ASSERT(aux != NULL);

	file_page->file = aux->file;  // do_mmap에서 넣어준 reopen()된 파일 포인터
	file_page->offset = aux->ofs;
	file_page->read_bytes = aux->read_bytes;  // 이 페이지가 실제 파일에서 읽을 바이트 수
	file_page->zero_bytes = aux->zero_bytes;  // 나머지는 0으로 채움

	return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/**
 * @brief 파일 기반 페이지를 제거하는 함수
 * 
 * @param page 제거할 페이지 구조체의 포인터
 * 
 * @details
 * 파일 기반 페이지를 제거하기 전에 페이지가 수정되었는지(dirty) 확인하고,
 * 수정되었다면 변경된 내용을 원본 파일에 기록합니다.
 * 페이지 자체의 메모리 해제는 이 함수를 호출한 쪽에서 처리합니다.
 */
static void file_backed_destroy(struct page *page) {
	struct file_page *file_page = &page->file;

	struct thread *t = thread_current();
	if (page->frame && pml4_is_dirty(t->pml4, page->va)) {
		void *kva = page->frame->kva;
		(void)file_write_at(file_page->file, kva, file_page->read_bytes, file_page->offset);
		pml4_set_dirty(t->pml4, page->va, false);
	}

	if (file_page->file) {
		file_close(file_page->file);  // **여기서 닫을 수 있게 do_mmap에서 page마다 file_reopen() 사용**
		file_page->file = NULL;
	}
}

static bool mmap_is_valid (void *addr, size_t length, off_t offset) {
	/* length가 0인가? */
	if (length == 0) {
		return false;
	}

	/* addr이 NULL이거나, 커널 주소 영역인가?
	 * addr이 페이지 정렬(page-aligned)되어 있지 않은가? (힌트: pg_round_down(addr) != addr)
	 */
	if (!addr || is_kernel_vaddr(addr) || pg_round_down(addr) != addr) {
		return false;
	}

	/* offset이 페이지 정렬되어 있지 않은가? = offset이 PGSIZE의 배수인가? */
	if (offset % PGSIZE != 0) {
		return false;
	}

	/*
	 * 매핑하려는 메모리 영역(addr부터 addr + length)이
	 * 기존에 이미 매핑된 페이지(코드, 데이터, 스택, 다른 mmap 영역 등)와 겹치는가?
	 * (힌트: spt_find_page를 사용해 페이지 단위로 순회하며 확인)
	 */
	for (void *upage = addr; upage < addr + length; upage += PGSIZE) {
		if (spt_find_page(&thread_current()->spt, upage)) {
			return false;
		}
	}

	return true;
}

/**
 * @brief 파일을 메모리에 매핑하는 함수
 * 
 * @param addr 매핑할 가상 메모리 주소 (페이지 정렬되어야 함)
 * @param length 매핑할 메모리의 크기 (바이트 단위)
 * @param writable 쓰기 가능 여부 (1: 쓰기 가능, 0: 읽기 전용)
 * @param file 매핑할 파일의 파일 객체 포인터
 * @param offset 파일 내에서 시작할 오프셋 (바이트 단위, 페이지 정렬되어야 함)
 * 
 * @return void* 매핑 성공 시 매핑된 가상 주소(addr), 실패 시 NULL
 * 
 * @details
 * 지정된 파일의 내용을 메모리에 매핑합니다. 
 * 요청된 주소 공간이 유효하지 않거나 이미 사용 중인 경우 실패합니다.
 * 지연 로딩을 사용하여 실제 파일 내용은 해당 페이지에 첫 접근 시 로드됩니다.
 */
void * do_mmap (void *addr, size_t length, int writable,
                struct file *file, off_t offset) {
	/* 주소, 길이, 오프셋의 유효성을 검사 */
	if (!mmap_is_valid(addr, length, offset)) {
		return NULL;
	}

	/* 파일의 실제 크기와 요청된 매핑 크기 중 작은 값을 읽을 바이트로 설정 */
	/* 나머지 부분은 0으로 채울 바이트로 설정 */
	size_t read_bytes = file_length(file) < length ? file_length(file) : length;
	size_t zero_bytes = ROUND_UP(length, PGSIZE) - read_bytes;

	/* 매핑을 시작할 가상 주소 설정 */
	void *current_addr = addr;

	/* 모든 페이지에 대해 매핑 작업 수행 */
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 현재 페이지에 대해 읽을 바이트와 0으로 채울 바이트 계산 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 지연 로딩에 필요한 파일 정보를 담을 보조 구조체 생성 */
		struct lazy_aux *aux = malloc(sizeof(struct lazy_aux));
		if (aux == NULL) {
			// TODO: 이전에 할당된 페이지들을 정리하는 로직 추가 필요
			// munmap_pages(addr);
			return NULL;
		}

		/* 보조 구조체에 파일 정보 설정 */
		aux->file = file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		/* 페이지를 생성하고 지연 로딩 초기화 함수 설정 */
		if (!vm_alloc_page_with_initializer(VM_FILE, current_addr, writable, lazy_load_segment, aux)) {
			free(aux);
			return NULL;
		}

		/* 다음 페이지 처리를 위한 값들 갱신 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		current_addr += PGSIZE;
		offset += page_read_bytes;
	}
	/* 매핑 성공 시 시작 주소 반환 */
	return addr;
}

/* mmap 호출시 생성한 모든 페이지들을 찾아서 SPT에서 제거하는 역할 */
void do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;

	while ((page = spt_find_page(spt, addr)) != NULL) {
		if (page_get_type(page) != VM_FILE) {
			break;
		}
		spt_remove_page(spt, page);
		addr += PGSIZE;
	}
}
