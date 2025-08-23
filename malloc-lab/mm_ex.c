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

#define PRED_PTR(bp) ((char **)(bp))     // 이전 free 포인터 자체의 저장 위치(포인터의 주소를 저장)
#define SUCC_PTR(bp) ((char **)(bp) + 1) // 다음 free 포인터 자체의 저장 위치

#define PRED(bp) (*(char **)(bp))       // pred 칸에 든 값(다른 블록의 bp)
#define SUCC(bp) (*((char **)(bp) + 1)) // succ 칸에 든 값

#define PTRSIZE ((int)sizeof(void *))          // 8
#define MIN_FREE_BLK (2 * WSIZE + 2 * PTRSIZE) // 4 + 4 + 8 + 8 = 24B

static char *heap_listp = NULL;
static char *free_listp = NULL; // 가용 리스트의 헤드
static char *rover = NULL;
static void *coalesce(void *);
static void *extend_heap(size_t);
static void *find_fit(size_t);
static void place(void *, size_t);
static void insert_free(void *bp);
static void remove_free(void *bp);

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
    free_listp = NULL; // 가용 리스트도 초기화

    void *bp_init = extend_heap(CHUNKSIZE / WSIZE);
    if (bp_init == NULL) {
        return -1;
    }
    rover = free_listp;
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

    bp = coalesce(bp); // 이웃 병합
    insert_free(bp);   // 병합 결과를 가용 리스트에 삽입
    rover = (char *)bp;
    return bp;
}

static void *find_fit(size_t asize) {
    // 1. First-fit 방식으로 할당 블록 찾기 가용 리스트의 head부터 하나씩 순회
    // for (char *bp = free_listp; bp != NULL; bp = SUCC(bp)) {
    //     if (asize <= GET_SIZE(HDRP(bp))) {
    //         return bp;
    //     }
    // }

    // 2. Next-fit 방식으로 찾기
    if (free_listp == NULL)
        return NULL;
    char *bp = (rover ? rover : free_listp);

    for (char *p = bp; p != NULL; p = SUCC(p)) {
        if (GET_SIZE(HDRP(p)) >= asize) {
            rover = SUCC(p); // 다음 탐색은 여기서 시작
            return p;
        }
    }
    for (char *p = free_listp; p != bp; p = SUCC(p)) {
        if (GET_SIZE(HDRP(p)) >= asize) {
            rover = SUCC(p);
            return p;
        }
        if (p == NULL)
            break; // 빈 리스트 방어
    }
    return NULL; // 가능한 칸을 찾지 못했을 경우
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // asize를 놓을 bp의 크기
    char *next_before = SUCC(bp);
    remove_free(bp);                     // bp는 쓰일 가용 블록이므로 가용 리스트에서 제거
    if (csize - asize >= MIN_FREE_BLK) { // 분할 가능한 자투리가 된다면
        // 앞부분 할당 표기
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *newbp = NEXT_BLKP(bp); // 할당 후 남은 공간은 가용블록으로 분리
        PUT(HDRP(newbp), PACK(csize - asize, 0));
        PUT(FTRP(newbp), PACK(csize - asize, 0));
        newbp = coalesce(newbp); // 인접과 병합
        insert_free(newbp);      // 새로 생긴 가용 블록을 가용 리스트에 삽입
        rover = newbp;
    } else {
        // 남는 공간이 충분하지 않으면 그냥 할당(분리 x)
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        rover = (next_before ? next_before : free_listp);
        if (free_listp == NULL)
            rover = NULL;
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

// 가용 리스트 맨 앞에 삽입(LIFO)
static void insert_free(void *bp) {
    // 1. LIFO 방식
    // SUCC(bp) = free_listp;     // bp의 다음 값은 기존의 헤드 -> bp가 헤드가 되어야하기 때문 : bp -> oldhead
    // PRED(bp) = NULL;           // bp의 이전 값은 null
    // if (free_listp != NULL) {  // 헤드가 존재했다면
    //     PRED(free_listp) = bp; // 기존 헤드의 이전값은 bp가 됨 : bp -> <- oldhead
    // }
    // free_listp = bp; // freelistp -> bp -> <- oldhead

    // 2. 주소 오름차순 정렬 방식
    if (free_listp == NULL) { // 헤드로 삽입
        PRED(bp) = NULL;
        SUCC(bp) = NULL;
        free_listp = bp;
        return;
    }

    if (bp < (void *)free_listp) { // 헤드보다 앞 주소라면
        PRED(bp) = NULL;
        SUCC(bp) = free_listp;
        PRED(free_listp) = bp;
        free_listp = bp;
        return;
    }

    // 중간 혹은 마지막 삽입의 경우
    char *prev = free_listp;
    char *curr = SUCC(free_listp);
    while (curr != NULL && curr < (char *)bp) {
        prev = curr;
        curr = SUCC(curr);
    }
    PRED(bp) = prev;
    SUCC(bp) = curr;
    SUCC(prev) = bp;
    if (curr != NULL) { // 꼬리가 아닐 경우
        PRED(curr) = bp;
    }
}

// 가용 리스트에서 bp 제거
static void remove_free(void *bp) {
    char *pred = PRED(bp); // *PRED_PTR(bp) 와 같음. pred의 값
    char *succ = SUCC(bp); // 마찬가지

    if (rover == (char *)bp) {
        rover = succ ? succ : pred;
    }

    // 만약 bp의 이전값이 있다면 이전 값의 다음 값은 현재의 다음값
    // pred -> bp -> succ에서 pred -> succ가 됨
    if (pred) {
        SUCC(pred) = succ;
    } else { // 만약 이전값이 없었으면 bp가 헤드였단 의미. freelistp를 succ으로
        free_listp = succ;
    }
    // 만약 다음값이 있었다면 다음값의 이전값을 pred로 설정하여 양방향 연결
    // 말로하면 조금 햇갈리는 듯. pred -> <- succ가 되고 중간의 bp가 사라진 상황이 된 것임.
    if (succ) {
        PRED(succ) = pred;
    }

    if (free_listp == NULL)
        rover = NULL;
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
    rover = (char *)bp;
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
        if (rover == prev || rover == next)
            rover = (char *)prev;
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
        if (rover == prev)
            rover = (char *)prev;
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
        if (rover == next)
            rover = (char *)ptr;
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

    size_t oldsize = GET_SIZE(HDRP(ptr)); // 현재 블록 크기(헤더/풋터 포함)

    // 제자리 축소
    if (asize <= oldsize) { // 기존 공간보다 작은 공간으로 재할당해야하는 경우
        size_t remainder = oldsize - asize;
        if (remainder >= MIN_FREE_BLK + DSIZE) { // 남은 공간이 여유로울 때 -> 분할
            // 먼저 사용으로 분할
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            // 새로운 포인터를 계산하여 가용으로 분할
            void *newbp = NEXT_BLKP(ptr);
            PUT(HDRP(newbp), PACK(remainder, 0));
            PUT(FTRP(newbp), PACK(remainder, 0));
            newbp = coalesce(newbp); // 인접 가용 블록을 병합
            insert_free(newbp);      // 가용 리스트 삽입
        }
        return ptr;
    }

    // 제자리 확장 1 - 오른쪽 흡수 시도
    void *next_bp = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));
    // 다음 블록이 가용이고, 다음블록까지 크기를 합치면 그 자리에 할당 가능할 때
    if (!next_alloc && (oldsize + next_size) >= asize) {
        remove_free(next_bp); // 다음 블록을 제거
        size_t newsize = oldsize + next_size;
        size_t remainder = newsize - asize;
        // 마찬가지로 남은 공간을 계산하여 분할 할당 or 일반 할당
        if (remainder >= MIN_FREE_BLK) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *newbp = NEXT_BLKP(ptr);
            PUT(HDRP(newbp), PACK(remainder, 0));
            PUT(FTRP(newbp), PACK(remainder, 0));
            newbp = coalesce(newbp);
            insert_free(newbp);
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
    size_t old_payload = oldsize - (2 * WSIZE);
    size_t copysize = MIN(size, old_payload);
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);
    return newptr;
}