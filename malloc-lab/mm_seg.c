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
#define CHUNKSIZE (1 << 10) // 힙을 한 번에 얼만큼 확장할지의 기본값(4096 -> 1024B)
// 힙을 늘리는 크기인 CHUNKSIZE를 4096->1024로 축소하니 점수 2점 상향(메모리 사용 점수 48 -> 50, 전체 88 -> 90)
// 왜? - 증분을 줄이면 필요한 시점에서만 조금씩 늘려 여분 free가 작아지므로 힙의 과잉 확장이 줄어듦

#define MAX(x, y) ((x) > (y) ? (x) : (y)) // 두 값의 최댓값 매크로
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define LISTS 16          // 리스트 개수
#define REALLOC_BUFFER 16 // Realloc 최적화용 버퍼 크기
#define SPLIT_LIMIT 64    // 분할 최솟값
/**
 * 64B 미만 잔여는 분할하지 않기 위함. 재사용성이 매우 낮은 자투리 free 블록의 생성을 억제하게 됨
 * -> 쓸모없는 free 조각들이 가용 리스트에 쌓이지 않아 외부 단편화가 감소됨
 * -> 결과적으로 힙의 최대 총량이 감소하여 util 점수의 상승
 */

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

#define PRED_PTR(bp) ((char **)(bp))     // 이전 free 포인터 자체의 저장 위치(포인터의 주소를 저장)
#define SUCC_PTR(bp) ((char **)(bp) + 1) // 다음 free 포인터 자체의 저장 위치

#define PRED(bp) (*(char **)(bp))       // pred 칸에 든 값(다른 블록의 bp)
#define SUCC(bp) (*((char **)(bp) + 1)) // succ 칸에 든 값

#define PTRSIZE ((int)sizeof(void *))              // 8
#define MIN_FREE_BLK ((2 * WSIZE) + (2 * PTRSIZE)) // 4 + 4 + 8 + 8 = 24B

static char *heap_listp = NULL;
static char *seg_free_lists[LISTS]; // 크기대별로 head 포인터를 보관

static void *coalesce(void *);
static void *extend_heap(size_t);
static void *find_fit(size_t);
static void place(void *, size_t);
static void insert_free(void *bp);
static void remove_free(void *bp);
static int get_class(size_t size); // 크기에 맞는 클래스 인덱스를 계산 (범위 기반 segregared fits)

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

    // 모든 크기 클래스의 헤드 포인터를 NULL로 초기화
    for (int i = 0; i < LISTS; i++) {
        seg_free_lists[i] = NULL;
    }

    void *bp_init = extend_heap(CHUNKSIZE / WSIZE);
    if (bp_init == NULL) {
        return -1;
    }
    return 0;
}

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

    bp = coalesce(bp); // 이웃 병합
    insert_free(bp);   // 병합 결과를 가용 리스트에 삽입
    return bp;
}

static void *find_fit(size_t asize) {
    // 2. Best-fit 방식
    int idx = get_class(asize);
    char *best_bp = NULL;
    size_t best_size = (size_t)-1;
    for (int i = idx; i < LISTS; i++) {
        for (char *bp = seg_free_lists[i]; bp != NULL; bp = SUCC(bp)) {
            size_t csize = GET_SIZE(HDRP(bp));
            if (csize >= asize && csize < best_size) {
                best_size = csize;
                best_bp = bp;
                if (csize == asize) {
                    return best_bp;
                }
            }
        }
    }
    return best_bp; // 가능한 칸을 찾지 못했을 경우
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // asize를 놓을 bp의 크기
    remove_free(bp);                   // bp는 쓰일 가용 블록이므로 가용 리스트에서 제거

    size_t want = asize + REALLOC_BUFFER;
    // 기본 요청 크기에 버퍼를 붙이지. 단 버퍼까지 붙이고도 잔여가 free 최소 크기 이상 남아야 함
    if (csize >= want && (csize - want) >= SPLIT_LIMIT) { // 버퍼까지 줄 수 있을 때
        // 앞부분 할당 표기
        PUT(HDRP(bp), PACK(want, 1));
        PUT(FTRP(bp), PACK(want, 1));
        void *newbp = NEXT_BLKP(bp);
        size_t remained = csize - want;
        PUT(HDRP(newbp), PACK(remained, 0));
        PUT(FTRP(newbp), PACK(remained, 0));
        newbp = coalesce(newbp);
        insert_free(newbp);
    } else if (csize - asize >= SPLIT_LIMIT) { // 버퍼까지 주는 것은 무리
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *newbp = NEXT_BLKP(bp);
        size_t remained = csize - asize;
        PUT(HDRP(newbp), PACK(remained, 0));
        PUT(FTRP(newbp), PACK(remained, 0));
        newbp = coalesce(newbp);
        insert_free(newbp);
    } else { // 잔여가 너무 작아 통째로 할당
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 * 맞는 가용 블록 찾아 배치
 */
void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size_t asize; // 조정된 블록 크기 (헤더/풋터 오버헤드 + 8바이트 정렬을 반영한 실제 크기)
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
    if (asize < MIN_FREE_BLK) {
        asize = MIN_FREE_BLK;
    }
    // 가용 리스트에서 맞는 블록 찾기
    char *bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize); // 헤더와 풋터를 할당으로 표기, 남는다면 분할
        return bp;
    }
    size_t extendsize; // 맞는 블록이 없을 때 힙을 얼마나 키워야할 지. 바이트 단위
    // 맞는 블록이 없을 경우 -> 힙을 늘려 새 가용블록 만들기
    extendsize = MAX(asize, CHUNKSIZE); // 최소 CHUNKSIZE 이상 늘려야함 너무 자주 sbrk하지 않도록
    // 인자 단위가 워드이므로 워드사이즈로 나누어주기
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

// 클래스 별 LIFO 삽입
static void insert_free(void *bp) {
    int idx = get_class(GET_SIZE(HDRP(bp)));
    PRED(bp) = NULL;
    SUCC(bp) = seg_free_lists[idx];
    if (seg_free_lists[idx] != NULL) {
        PRED(seg_free_lists[idx]) = (char *)bp;
    }
    seg_free_lists[idx] = (char *)bp;
}

// 가용 리스트에서 bp 제거
static void remove_free(void *bp) {
    int idx = get_class(GET_SIZE(HDRP(bp)));
    char *pred = PRED(bp); // *PRED_PTR(bp) 와 같음. pred의 값
    char *succ = SUCC(bp); // 마찬가지

    if (pred) {
        SUCC(pred) = succ;
    } else {
        seg_free_lists[idx] = succ;
    }
    if (succ) {
        PRED(succ) = pred;
    }
    // 자기 포인터는 끊어두기
    PRED(bp) = NULL;
    SUCC(bp) = NULL;
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
    insert_free(bp);          // 가용 리스트에 추가
}

static void *coalesce(void *ptr) {
    // 이전 블록의 풋터에서 할당 비트를 읽어 왼쪽 이웃의 상태를 알아냄
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); // 오른쪽 이웃의 상태 알아냄
    size_t size = GET_SIZE(HDRP(ptr));                   // 현재 블록의 크기

    if (!prev_alloc && !next_alloc) { // Case 4 : 이전 다음 둘 다 할당 x
        // 이전과 다음 bp 구하기
        void *prev = PREV_BLKP(ptr);
        void *next = NEXT_BLKP(ptr);
        // 둘 다 가용 리스트에서 제거
        remove_free(prev);
        remove_free(next);
        // size에 이전과 다음 크기만큼 더함
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(next));
        // 헤더는 이전으로, 풋터는 다음으로 설정, alloc=0
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));
        // bp는 prev가 됨
        ptr = prev;
    } else if (!prev_alloc && next_alloc) { // Case 3 : 이전만 할당 x
        // 이전 bp만 구함
        void *prev = PREV_BLKP(ptr);
        // 이전 bp 가용 리스트에서 제거
        remove_free(prev);
        // size에 이전 크기만큼만 더함
        size += GET_SIZE(HDRP(prev));
        // 헤더를 이전으로 설정, 풋터는 원래
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
        // 헤더는 이전이 됨
        ptr = prev;
    } else if (prev_alloc && !next_alloc) { // Case 2 : 다음만 할당 x
        void *next = NEXT_BLKP(ptr);
        remove_free(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));
    } else {
    } // Case 1 : 둘 다 할당되어있음
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 * 가능하면 제자리, 안 되면 이동
 */
void *mm_realloc(void *ptr, size_t size) {
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
    if (asize < MIN_FREE_BLK) {
        asize = MIN_FREE_BLK;
    }
    size_t want = asize + REALLOC_BUFFER;

    size_t oldsize = GET_SIZE(HDRP(ptr)); // 현재 블록 크기(헤더/풋터 포함)
    size_t old_payload = oldsize - (2 * WSIZE);

    // 제자리 축소
    if (asize <= oldsize) {
        size_t keep = oldsize - asize;

        // 버퍼까지 붙여 분할 가능하면 asize+buffer로 축소 + 잔여 free
        if (oldsize >= want && (oldsize - want) >= SPLIT_LIMIT) {
            PUT(HDRP(ptr), PACK(want, 1));
            PUT(FTRP(ptr), PACK(want, 1));

            void *newbp = NEXT_BLKP(ptr);
            size_t remainder = oldsize - want;
            PUT(HDRP(newbp), PACK(remainder, 0));
            PUT(FTRP(newbp), PACK(remainder, 0));
            newbp = coalesce(newbp);
            insert_free(newbp);
        }
        // 버퍼까지는 무리지만 정상 분할은 가능한 경우 - asize만 주고 분할
        else if (keep >= SPLIT_LIMIT) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));

            void *newbp = NEXT_BLKP(ptr);
            PUT(HDRP(newbp), PACK(keep, 0));
            PUT(FTRP(newbp), PACK(keep, 0));
            newbp = coalesce(newbp);
            insert_free(newbp);
        }
        // [C] 잔여가 너무 작으면: 통째로 유지(내부 버퍼 효과 유지)
        return ptr;
    }

    // 제자리 확장 1 - 오른쪽 흡수 시도
    void *next_bp = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));
    // 다음 블록이 가용이고, 다음블록까지 크기를 합치면 그 자리에 할당 가능할 때
    if (!next_alloc && (oldsize + next_size) >= asize) {
        remove_free(next_bp);
        size_t total = oldsize + next_size;

        // 버퍼까지 주고도 잔여가 free 최소크기 이상 남으면 want 할당
        size_t alloc = (total >= want && (total - want) >= SPLIT_LIMIT) ? want : total;
        size_t remainder = total - alloc;

        PUT(HDRP(ptr), PACK(alloc, 1));
        PUT(FTRP(ptr), PACK(alloc, 1));

        if (remainder >= SPLIT_LIMIT) {
            void *nbp = NEXT_BLKP(ptr);
            PUT(HDRP(nbp), PACK(remainder, 0));
            PUT(FTRP(nbp), PACK(remainder, 0));
            nbp = coalesce(nbp);
            insert_free(nbp);
        }
        return ptr;
    }

    // 제자리 확장 2 - 왼쪽 흡수 시도
    void *prev_bp = PREV_BLKP(ptr);
    size_t prev_alloc = GET_ALLOC(HDRP(prev_bp));
    size_t prev_size = GET_SIZE(HDRP(prev_bp));
    if (!prev_alloc && (prev_size + oldsize) >= asize) {
        remove_free(prev_bp);
        size_t total = prev_size + oldsize;

        void *newptr = prev_bp; // 왼쪽으로 확장
        memmove(newptr, ptr, old_payload);

        size_t alloc = (total >= want && (total - want) >= SPLIT_LIMIT) ? want : total;
        size_t remainder = total - alloc;

        PUT(HDRP(newptr), PACK(alloc, 1));
        PUT(FTRP(newptr), PACK(alloc, 1));

        if (remainder >= SPLIT_LIMIT) {
            void *nbp = NEXT_BLKP(newptr);
            PUT(HDRP(nbp), PACK(remainder, 0));
            PUT(FTRP(nbp), PACK(remainder, 0));
            nbp = coalesce(nbp);
            insert_free(nbp);
        }
        return newptr;
    }

    // 제자리 확장 3 - 왼쪽 오른쪽 둘 다 흡수 시도
    if (!prev_alloc && !next_alloc && (prev_size + oldsize + next_size) >= asize) {
        remove_free(prev_bp);
        remove_free(next_bp);
        size_t total = prev_size + oldsize + next_size;

        void *newptr = prev_bp;
        memmove(newptr, ptr, old_payload);

        size_t alloc = (total >= want && (total - want) >= SPLIT_LIMIT) ? want : total;
        size_t remainder = total - alloc;

        PUT(HDRP(newptr), PACK(alloc, 1));
        PUT(FTRP(newptr), PACK(alloc, 1));

        if (remainder >= SPLIT_LIMIT) {
            void *nbp = NEXT_BLKP(newptr);
            PUT(HDRP(nbp), PACK(remainder, 0));
            PUT(FTRP(nbp), PACK(remainder, 0));
            nbp = coalesce(nbp);
            insert_free(nbp);
        }
        return newptr;
    }

    // 제자리 확장에 실패 한 것이므로 완전히 새로운 블록을 할당 후 기존은 free
    void *newptr = mm_malloc(size + REALLOC_BUFFER);
    if (newptr == NULL) {
        return NULL;
    }
    size_t copysize = MIN(size, old_payload);
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);
    return newptr;
}

// 크기 -> 클래스 인덱스 매핑
// 기준 임계값은 16B, 그 다음은 32, 64, 128 ... (2배씩 증가)
// 예) size=24 -> idx=1(32B 클래스), size=80 -> idx=3(128B 클래스)
static int get_class(size_t size) {
    int idx = 0;
    size_t bound = 16; // 첫 크기

    while (idx < LISTS - 1 && size > bound) {
        bound <<= 1; // 두 배로 증가
        idx++;
    }
    return idx;
}
