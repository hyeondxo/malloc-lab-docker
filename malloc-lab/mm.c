/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h" // 연습용 힙 인터페이스 제공
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "krafton-jungle-10",
    /* First member's full name */
    "dohyeon park",
    /* First member's email address */
    ".",
    /* Second member's full name (leave blank if none) */
    ".",
    /* Second member's email address (leave blank if none) */
    "."};

#define WSIZE 4             // word 크기이자 헤더/풋터의 크기(바이트)
#define DSIZE 8             // double word 크기. 최소 블록 크기(헤더 4 + 풋터 4)와 정렬 단위로 사용
#define CHUNKSIZE (1 << 12) // 힙을 한 번에 얼만큼 확장할지의 기본값(4096B)

#define MAX(x, y) ((x) > (y) ? (x) : (y)) // 두 값의 최댓값 매크로
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) // 한 워드에 블록 전체 크기와 할당 비트를 OR로 묶어 저장
// size는 8의 배수로 저장, alloc은 하위 비트(보통 bit0) 사용(0=free, 1=alloc)

#define GET(p) (*(unsigned int *)(p))              // 주소 p의 4바이트(워드)를 읽어 unsigned int로 해석
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // 주소 p의 4바이트(워드)에 val을 쓰기

#define GET_SIZE(p) (GET(p) & ~0x7) // 주소 p가 가리키는 워드에서 하위 3비트를 0으로 마스킹해 블록 크기만 추출
#define GET_ALLOC(p) (GET(p) & 0x1) // 주소 p의 워드에서 할당 비트만 추출

// 현재 블록의 헤더 주소. 레이아웃이 [헤더][페이로드]이므로 페이로드 앞 4B가 헤더
#define HDRP(bp) ((char *)(bp) - WSIZE)
// 현재 블록의 풋터 주소.
// 블록 전체 크기만큼 앞으로 이동했다가 헤더_풋터만큼 되돌아가 마지막 워드(풋터)에 도착
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 다음 블록의 payload 포인터를 계산. (char *)(bp) - WSIZE는 현재 헤더.
// 현재 블록의 헤더에서 현재 블록 크기를 얻어 bp에 더하면 다음 블록의 payload 시작으로 이동
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
// 이전 블록의 payload 포인터를 계산.
// (char *)(bp) - DSIZE는 이전 블록의 풋터 위치 (이전 풋터 4B + 현재 헤더 4B = 8B(DSIZE))
// 그 풋터에서 이전 블록 크기를 읽어 bp에서 뒤로 빼면 이전 블록의 payload 시작점으로 이동
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
// 경계 부근 텍스트 도식
// ... [Prev Footer][Curr Header][Curr Payload(bp→)] ...
//           ^ bp-8  ^ bp-4
// PREV_BLKP:  (bp - 8)에서 size 읽고 bp - size
// NEXT_BLKP:  (bp - 4)에서 size 읽고 bp + size

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8 // 정렬 단위 = 8바이트

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 올림 정렬 : size를 8의 배수로 올림
// 비트마스크 ~0x7은 하위 3비트를 0으로 만드는 것

#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // 8로 올림 정렬한 크기

static char *heap_listp = NULL;
static char *rover = NULL; // Next-fit 탐색 시작지점
static void *coalesce(void *);
static void *extend_heap(size_t);
static void *find_fit(size_t);
static void place(void *, size_t);

/*
 * mm_init - initialize the malloc package.
 * 힙 뼈대 만들기
 */
int mm_init(void) {
    // 힙에 4워드 공간을 요구, 실패하면 -1
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) {
        return -1;
    }
    PUT(heap_listp, 0);                            // 0을 써서 정렬 패딩 (payload 정렬을 맞추기 위한 더미)
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더(크기=8, 할당=1)
    // 프롤로그 풋터(크기=8, 할당1) 프롤로그 블록은 헤더+풋터만 있는 항상 할당된 더미 블록. 경계 처리 단순화를 위함
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    // 에필로그 헤더(크기=0, 할당=1) 끝표시용 더미 헤더
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    // heap_listp를 프롤로그 블록의 payload 위치(헤더 바로 뒤)로 이동. 관례적으로 힙의 기준 포인터로 쓰임
    heap_listp += (2 * WSIZE);

    // 빈 힙을 CHUNKSIZE(4096B)만큼 더 확장하여 첫 가용 블록을 만듦. 실패시 -1
    // 1. First-fit 방식
    // if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
    //     return -1;
    // }

    // 2. Next-fit 방식
    void *bp_init = extend_heap(CHUNKSIZE / WSIZE);
    if (bp_init == NULL) {
        return -1;
    }
    // Next-fit 초기 탐색 위치를 첫 가용블록으로 설정
    rover = (char *)bp_init;
    return 0;
}
// 주소→
// [ pad(0) ][ Prologue H(8|1) ][ Prologue F(8|1) ][ Epilogue H(0|1) ]
//                ^ heap_listp (프롤로그 payload, 관례상 포인터 기준)

// 힙을 확장
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; // 짝수 워드로 만들기
    if ((long)(bp = mem_sbrk(size)) == -1) { // 힙을 뒤로 size만큼 확장하고 새로 얻은 영역의 시작 주소를 bp로 받음
        return NULL;
    }
    // 방금 얻은 새 영역 전체를 하나의 가용 블록으로 표기 (헤더, 풋터에 size, alloc=0 기록)
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 새로운 에필로그 해더

    // First-fit 방식
    // return coalesce(bp); // 이전 블록이 가용이라면 병합해서 더 큰 가용블록을 만들어 반환
    // Next-fit 방식
    bp = coalesce(bp);  // 이웃 병합
    rover = (char *)bp; // 다음 탐색 갱신
    return bp;
}

static void *find_fit(size_t asize) {
    /* First-fit 방식*/
    // void *bp;
    // for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    //     // 할당되지 않았고 들어갈 크기가 된다면 그 bp를 반환 (할당 가능하단 뜻)
    //     if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
    //         return bp;
    //     }
    // }

    if (rover == NULL) {
        rover = heap_listp;
    }
    // 2. Next-fit 방식
    void *bp;
    // rover부터 시작해서 힙의 끝까지 탐색
    for (bp = rover; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
            rover = (char *)bp;
            return bp;
        }
    }
    // 힙의 시작점부터 rover까지 탐색
    for (bp = heap_listp; bp != rover; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
            rover = (char *)bp;
            return bp;
        }
    }
    return NULL; // 맞는 칸이 없음
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // asize를 놓을 bp의 크기
    if (csize - asize >= 2 * DSIZE) {  // 분할 가능한 자투리가 된다면
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *newbp = NEXT_BLKP(bp); // 할당 후 남은 공간은 가용블록으로 분리
        PUT(HDRP(newbp), PACK(csize - asize, 0));
        PUT(FTRP(newbp), PACK(csize - asize, 0));
        rover = (char *)newbp; // 다음 탐색은 남은 가용블록부터 시작
    } else {
        // 남는 공간이 충분하지 않으면 그냥 할당(분리 x)
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        rover = (char *)NEXT_BLKP(bp);
    }
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 * 맞는 가용 블록 찾아 배치
 */
void *mm_malloc(size_t size) {
    size_t asize;      // 조정된 블록 크기 (헤더/풋터 오버헤드 + 8바이트 정렬을 반영한 실제 크기)
    size_t extendsize; // 맞는 블록이 없을 때 힙을 얼마나 키워야할 지. 바이트 단위
    char *bp;          // 가용 블록의 payload 포인터(반환주소)

    if (size == 0) {
        return NULL;
    }

    // asize 계산 = 오버헤드 포함 + 8바이트 정렬
    if (size <= DSIZE) {
        asize = 2 * DSIZE;
        // 최소 블록 크기는 2 * DSIZE(8)이 됨. 16보다 작으면 그냥 16으로 만들기
        // -> 헤더 4 + 풋터 4 + 최소 payload 8(정렬 보장, 분할 시 잔여 최소 보장)
    } else {
        // 더블 워드보다 크다면 8의 배수로 맞춰주기
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
        // size=13 → size+8=21 → +7=28 → /8=3 → *8=24 → asize=24B
        // (헤더/풋터 포함 + 정렬)
    }

    // 가용 리스트에서 맞는 블록 찾기
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize); // 헤더와 풋터를 할당으로 표기, 남는다면 분할
        return bp;
    }

    // 맞는 블록이 없을 경우 -> 힙을 늘려 새 가용블록 만들기
    extendsize = MAX(asize, CHUNKSIZE); // 최소 CHUNKSIZE 이상 늘려야함 너무 자주 sbrk하지 않도록
    // 인자 단위가 워드이므로 워드사이즈로 나누어주기
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 * 메모리 사용 여부 표시를 변경하고 병합해서 재사용
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr)); // 현재 블록의 헤더에서 블록 전체 크기를 얻음

    // 헤더/풋터에 기록 후 가용=0으로 표시 -> 헤더 풋터 둘 다 동일한 (size|0)을 가짐
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    void *bp = coalesce(ptr); // 바로 좌우 가용 블록과 병합을 시도(즉시 병합)
    rover = (char *)bp;
}

static void *coalesce(void *ptr) {
    // 이전 블록의 풋터에서 할당 비트를 읽어 왼쪽 이웃의 상태를 알아냄
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); // 오른쪽 이웃의 상태 알아냄
    size_t size = GET_SIZE(HDRP(ptr));                   // 현재 블록의 크기

    // Case 1 : 병합 필요 없음 -> 그대로 반환
    if (prev_alloc && next_alloc) {
        return ptr;
    } else if (prev_alloc && !next_alloc) {
        // Case 2 : 현재 + 오른쪽을 합쳐 하나의 가용블록으로 만들기
        // 새 헤더는 현재 위치, 새 풋터는 오른쪽의 풋터가 됨
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        // Case 3 : 왼쪽 + 현재를 병합
        // 새 헤더는 왼쪽 헤더, 풋터는 현재 풋터가 됨
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr); // bp를 새 블록의 payload(왼쪽 블록)으로 이동
    } else {
        // Case 4 : 왼쪽 + 현재 + 오른쪽 블록을 병합
        // 새 헤더는 왼쪽 헤더, 새 풋터는 오른쪽 풋터가 됨
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 * 가능하면 제자리, 안 되면 이동
 */
void *mm_realloc(void *ptr, size_t size) {
    // void *oldptr = ptr;
    // void *newptr;
    // size_t copySize;

    // newptr = mm_malloc(size);
    // if (newptr == NULL)
    //     return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    // /**
    //  * copySize 계산 로직의 문제점
    //  * 1. 오프셋 불일치
    //  * 올바른 헤더는 bp부터 4를 빼야한다. 하지만 SIZE_T_SIZE는 8을 빼게되고 그렇다면 이전 블록의 풋터 위치로 가게됨
    //  * 그리고 8바이트를 통째로 읽으면 이전 풋터 4B + 현재 헤더 4B가 한 번에 읽혀버림 -> 유효한 크기가 아님
    //  * 만약 32비트 환경에서 우연히 4비트를 빼게 되어 헤더의 위치가 같아져도 다음 오류가 발생
    //  * 2. 값 내용 불일치
    //  * 헤더 워드에는 (size|alloc_bit)가 존재함. 그런데 이 워드를 size_t로 읽게되면
    //  * 할당 비트가 섞여 정확한 크기만을 뽑아낼 수 없게 됨
    //  * 예를 들어 실제 크기가 24라면 헤더 워드는 24|1이 되고, size_t만큼 읽으면 25가 됨.
    //  * 이걸 그대로 복사 길이로 써서는 안됨
    //  * 또한 헤더에 저장된 크기 24는 전체 크기 + 헤더와 풋터(8B)이지만 우리가 복사해야하는 값은 payload의 크기인 16임.
    //  * 따라서 올바른 이전 블록의 크기는 GET_SIZE(HDRP(oldptr))를 통해 마스킹 로직으로 크기만을 뽑아야 하고
    //  * 원본 payload의 정확한 크기는 MIN(old_payload, size)가 되어야 함.
    //  */
    // if (size < copySize)
    //     copySize = size;
    // memcpy(newptr, oldptr, copySize);
    // mm_free(oldptr);
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    // asize 계산(헤더/풋터 포함 + 8바이트 정렬, 최소 16바이트)
    size_t asize;
    if (size <= DSIZE) {
        asize = 2 * DSIZE;
    } else {
        asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);
    }

    size_t oldsize = GET_SIZE(HDRP(ptr)); // 현재 블록 크기(헤더/풋터 포함)

    // 제자리 축소
    if (asize <= oldsize) { // 기존 공간보다 작은 공간으로 재할당해야하는 경우
        size_t remainder = oldsize - asize;
        if (remainder >= 2 * DSIZE) { // 남은 공간이 여유로울 때 -> 분할
            // 먼저 사용으로 분할
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            // 새로운 포인터를 계산하여 가용으로 분할
            void *newbp = NEXT_BLKP(ptr);
            PUT(HDRP(newbp), PACK(remainder, 0));
            PUT(FTRP(newbp), PACK(remainder, 0));
            coalesce(newbp); // 인접 가용 블록을 병합
            rover = (char *)newbp;
        }
        return ptr;
    }

    // 제자리 확장
    void *next_bp = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));
    // 다음 블록이 가용이고, 다음블록까지 크기를 합치면 그 자리에 할당 가능할 때
    if (!next_alloc && (oldsize + next_size) >= asize) {
        size_t newsize = oldsize + next_size;
        size_t remainder = newsize - asize;
        // 마찬가지로 남은 공간을 계산하여 분할 할당 or 일반 할당
        if (remainder >= 2 * DSIZE) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *newbp = NEXT_BLKP(ptr);
            PUT(HDRP(newbp), PACK(remainder, 0));
            PUT(FTRP(newbp), PACK(remainder, 0));
            newbp = coalesce(newbp);
            rover = (char *)newbp;
        } else { // 공간 없으면 그냥 합친 크기만큼 할당
            PUT(HDRP(ptr), PACK(newsize, 1));
            PUT(FTRP(ptr), PACK(newsize, 1));
        }
        return ptr;
    }
    // 제자리 확장에 실패 한 것이므로 완전히 새로운 블록을 할당 후 기존은 free
    void *newptr = mm_malloc(size);
    if (newptr == NULL) {
        return NULL;
    }

    size_t old_payload = GET_SIZE(HDRP(ptr)) - 2 * WSIZE;
    size_t copysize = MIN(size, old_payload);
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);
    return newptr;
}