/*
 * memlib.c - a module that simulates the memory system.  Needed because it
 *            allows us to interleave calls from the student's malloc package
 *            with the system's malloc package in libc.
 * 메모리 시스템을 시뮬레이션하는 모듈,
 * 학생이 만든 malloc 패키지와 시스템의 libc malloc을 섞어서 호출할 수 있기 해주기 때문에 필요함
 */
#include <assert.h>   // assert 매크로 선언
#include <errno.h>    // 전역 변수 errno, 에러코드 ENOMEM 사용을 위해 포함
#include <stdio.h>    // fprintf 등 표준 입출력 함수 선언
#include <stdlib.h>   // malloc, free, exit 등 선언
#include <string.h>   // 문자열/메모리 유틸 선언
#include <sys/mman.h> // 메모리 매핑 관련 선언
#include <unistd.h>   // getpagesize 선언 등

#include "config.h" // 설정값(ex MAX_HEAP) 정의를 사용하기 위해 포함
#include "memlib.h" // 이 모듈의 공개 인터페이스

/* private variables */
// char *을 쓰는 이유? 바이트 단위 포인터 연산이 정확해야하기 때문
static char *mem_start_brk; /* points to first byte of heap */ // 시뮬레이션 힙 시작주소(첫 바이트)를 가리킴
static char *mem_brk; /* points to last byte of heap */        // 시뮬레이션 힙의 끝 다음 주소를 가리킴
static char *mem_max_addr; /* largest legal heap address */    // 시뮬레이션 힙에서 허용가능한 최대 주소의 다음주소

/*
 * mem_init - initialize the memory system model
 * 메모리 시스템 시뮬레이터를 초기화
 */
void mem_init(void) {
    /* allocate the storage we will use to model the available VM */
    // MAX_HEAP 바이트만큼 큰 버퍼를 할당하여 가짜 힙 전체로 사용
    if ((mem_start_brk = (char *)malloc(MAX_HEAP)) == NULL) {
        fprintf(stderr, "mem_init_vm: malloc error\n");
        exit(1);
    }
    // 가짜 힙의 합법적 범위 끝의 다음 주소를 설정
    mem_max_addr = mem_start_brk + MAX_HEAP; /* max legal heap address */
    // 초기에는 빈 힙이므로 브레이크 = 시작 주소
    mem_brk = mem_start_brk; /* heap is empty initially */
}

/*
 * mem_deinit - free the storage used by the memory system model
 * 가짜 힙 버퍼를 반환 (프로그램 종료 시점 정리용)
 */
void mem_deinit(void) { free(mem_start_brk); }

/*
 * mem_reset_brk - reset the simulated brk pointer to make an empty heap
 * 시뮬레이션 힙을 빈 상태로 재설정 (내부 포인터를 되돌림)
 */
void mem_reset_brk() { mem_brk = mem_start_brk; }

/*
 * mem_sbrk - simple model of the sbrk function. Extends the heap
 *    by incr bytes and returns the start address of the new area. In
 *    this model, the heap cannot be shrunk.
 * UNIX sbrk의 단순 모델. 힙을 incr 만큼 늘리고, 늘리기 전 주소(새 영역의 시작)를 반환
 * 동작 도식
 * 확장 전:
[mem_start_brk ... old_brk=mem_brk)          mem_max_addr
                    ^ (끝 다음)

요청: mem_sbrk(incr)

확장 후:
[mem_start_brk ... old_brk ...... mem_brk)    mem_max_addr
                    <---- incr ---->
반환값 = old_brk
 */
void *mem_sbrk(int incr) {
    char *old_brk = mem_brk; // 확장 전의 brk(끝 다음 주소) 저장

    if ((incr < 0) || ((mem_brk + incr) > mem_max_addr)) { // 올바른 범위의 확장이라면
        errno = ENOMEM;
        fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
        return (void *)-1;
    }
    mem_brk += incr;        // 힙을 확장
    return (void *)old_brk; // 새로 할당된 영역의 시작(확장 전 주소) 반환
}

/*
 * mem_heap_lo - return address of the first heap byte
 * 힙의 첫 바이트 주소 반환
 */
void *mem_heap_lo() { return (void *)mem_start_brk; }

/*
 * mem_heap_hi - return address of last heap byte
 * 현재 힙의 마지막 유효 바이트 주소 반환 - mem_brk는 끝 다음주소이므로 마지막 유효 주소는 mem_brk-1
 */
void *mem_heap_hi() { return (void *)(mem_brk - 1); }

/*
 * mem_heapsize() - returns the heap size in bytes
 * 현재 힙의 전체 크기(바이트)
 */
size_t mem_heapsize() { return (size_t)(mem_brk - mem_start_brk); }

/*
 * mem_pagesize() - returns the page size of the system
 * 시스템 페이지의 크기 - unistd.h 내장
 */
size_t mem_pagesize() { return (size_t)getpagesize(); }
