
# 동적 할당기(Dynamic Memory Allocator) 구현

C 언어의 malloc/free/realloc을 직접 구현하는 과제입니다.
본 구현은 Segregated Explicit Free Lists 기반의 동적 메모리 할당기이며, 클래스별(best-fit) 탐색, 즉시 병합(coalesce), 스플린터 억제(SPLIT_LIMIT), realloc 친화 버퍼/힙 끝 특수 확장 등을 결합해 메모리 효율(util)과 처리량(thru) 사이의 균형을 노립니다.


## 과제의 의도 (Motivation)

- 힙 레이아웃, 정렬, 오버헤드를 직접 다루며 동적 메모리 할당의 본질을 이해합니다.
- 단편화(내부/외부)를 줄이기 위한 정책 설계(클래스, best-fit, 스플린터 억제, 2^k 정규화, realloc 친화)를 경험합니다.
- 성능-품질 트레이드오프(util vs thru) 튜닝을 체감합니다.


## 구현 규칙

- mm.c 이외 파일은 수정하지 않고 테스트를 통과합니다.
- make / ./mdriver로 correctness/throughput/utilization을 검증합니다.

## 구현 기능 목록 정리

- **`mm_init()`** : 프롤로그(8B)와 에필로그(0B 헤더)를 설치하고, CHUNKSIZE만큼 extend_heap 호출로 첫 가용 블록을 마련합니다.
   - -> 분리 가용 리스트 seg_free_lists[LISTS]의 헤드를 모두 NULL로 초기화합.

- **`mm_malloc(size)`** : 요청 size를 8B 정렬/오버헤드(헤더/풋터) 포함한 asize로 변환합니다.
   - -> 작은 요청은 next_pow2/round_up_for_binary로 2^k 경계에 가깝게 정규화하여 외부 단편화(클래스 확산)를 줄입니다.
   - -> find_fit(asize)로 클래스별 리스트에서 best-fit 탐색 후, place로 배치/분할합니다.
   - -> 적합 블록이 없으면 extend_heap으로 힙을 확장한 뒤 배치합니다.
   
- **`mm_free(ptr)`**
  - -> 헤더/풋터를 free로 표기하고 coalesce(좌/우/양측 즉시 병합)를 수행한 뒤, 결과 블록을 분리 가용 리스트에 insert_free합니다.
- **`mm_realloc(ptr, size)`**
  - -> 축소(asize ≤ oldsize): 잔여가 SPLIT_LIMIT 이상이면 분할하여 free로 돌려주고, 작으면 통째 유지(스플린터 억제, 내부 버퍼 효과 유지).
  - -> 확장(asize > oldsize): 우측 free 결합 → 좌측 free 결합(필요 시 memmove) → 양측 결합 순으로 제자리 확장을 우선 시도합니다.
  - -> 힙 끝(에필로그) 특수 처리: 우측이 에필로그면 extend_heap으로 필요한 만큼만 뒤로 확장하여 제자리 확장을 성사(버퍼 억제).
  - -> 모두 실패 시 새 블록을 할당 후 memcpy/원본 free.
  
- **`find_fit(asize)`**
  - -> get_class(asize)의 클래스부터 상위 클래스로 진행하며, 각 리스트를 선형 순회하여 가장 작은 적합 블록을 선택합니다(클래스 + best-fit 하이브리드).
  
- **`place(bp, asize)`**
  - -> realloc 친화 버퍼 정책: 큰 요청(asize > 512)에 대해 REALLOC_BUFFER(=128) 여유를 기회적으로 부여(단, 잔여가 SPLIT_LIMIT 이상 남을 때만). 작은 요청은 +DSIZE 수준만 고려.
  - -> 잔여가 SPLIT_LIMIT(=64B) 미만이면 분할하지 않고 통째 할당(스플린터 억제).
- **`extend_heap(words)`**
  - -> 짝수 워드 정렬로 sbrk, [새 free 블록] + [새 에필로그] 작성, 즉시 coalesce 후 리스트에 삽입.
- **`coalesce(bp)`**
  - -> Case 1~4(좌/우/양측) 병합. 병합되는 이웃 free는 먼저 remove_free로 리스트에서 제거하고 크기를 합산 후 헤더/풋터 갱신, 대표 포인터로 bp 선택.
- **`insert_free(bp) / remove_free(bp)`**
  - -> get_class(size)로 리스트 선택. 명시적 가용 리스트를 LIFO로 삽입, 삭제는 O(1) 업데이트.
- **`get_class(size)`**
  - -> 16, 32, 64, 128, … 경계(2배씩 증가)로 16개 클래스(LISTS=16)에 매핑.



## 알고리즘 설계 포인트

1) 클래스 + best-fit 혼합
   - 클래스(대역폭)로 먼저 외부 탐색 공간을 줄이고, 리스트 내부에서는 best-fit로 내부 단편화를 최소화합니다.

2) 스플린터 억제(SPLIT_LIMIT)
   - 분할 잔여가 64B 미만이면 free로 내놓지 않습니다. <br>
→ 재사용성 없는 자투리를 애초에 만들지 않음 → 가용 리스트 품질 유지 → 외부 단편화 완화 → util↑

3) 2^k 정규화
   - 작은 요청을 2^k 경계로 붙이고(next_pow2, round_up_for_binary) 클래스 분산을 줄여 재사용률↑.
   - round_up_for_binary(asize)는 “다음 2^k와 차이가 SPLIT_LIMIT 미만”일 때만 상향 → 과한 상향을 방지.

4) realloc 친화 정책
   - 큰 요청에서만 REALLOC_BUFFER를 기회적으로 제공(분할 가능 조건에서만).
   - 반면, 힙 끝(에필로그)에서는 정확치(want=asize)만 확장하여 과할당 누적을 막습니다.<br>
→ trace 특성상 realloc2(조금씩 커지는 패턴)에서 util 크게 개선.

5) 즉시 병합(coalesce-immediate)
   - free 시 즉시 병합하여 큰 free 블록을 유지 → 대형 요청/제자리 확장에 유리.
   - 병합 시 이웃 free는 먼저 리스트에서 제거해 이중 링크 보존.

**시간/공간 복잡도(개략)**
- mm_malloc: O(#classes + 리스트 선형) ≈ 클래스는 상수(16), 평균 짧은 선형
- mm_free: O(1) (병합 시 remove/insert O(1))
- mm_realloc: 제자리 확장 성공 시 O(1), 좌측 흡수 이동 시 O(n) (payload 이동), 실패 시 malloc+memcpy+free

## 빌드/테스트

make           # 빌드
./mdriver      # correctness/util/thru 종합 평가
또는
make clean && make && ./mdriver

	•	결과 표의 의미:
	•	util: (총 payload) / (peak heap). 클수록 메모리 효율↑
	•	thru: Kops(천 연산/초). 클수록 처리량↑
	•	Perf index: 가중 합(예: 50 util + 40 thru)



## 느낀 점

직접 할당기를 만들며 동적 메모리의 핵심은 “형식과 불변식”임을 체감했다. HDRP/FTRP/NEXT_BLKP/PREV_BLKP 같은 블록 산술은 사소해 보이지만, 한 번만 어긋나도 이웃 판별·병합·리스트 무결성이 연쇄적으로 무너진다. 또한 프롤로그/에필로그의 의미(경계 표식), MIN_FREE_BLK 형식 보장, 8B 정렬처럼 “작은 규칙”이 전체 안정성을 좌우한다는 사실도 절실히 배웠다.

단편화 제어가 성능의 절반 이상을 결정했다. SPLIT_LIMIT로 스플린터를 억제하고, 작은 요청을 2^k 정규화해 클래스 재사용률을 높였더니 외부 단편화가 눈에 띄게 준 것을 확인할 수 있었다. 반면, 무분별한 버퍼는 내부 단편화를 키워 util을 떨어뜨렸기에 결국 분리 가용 리스트 + 클래스 내 best-fit에 조건부 버퍼를 더해 트레이스별 성향(작게·자주 vs 덩어리)과 균형을 잡는 게 관건이었던 것 같다.

특히 realloc에서는 제자리 확장의 가치가 컸다. 우→좌→양측 병합을 시도하고, 힙 끝(에필로그)에서는 정확치만 확장하자 복사 비용과 피크 힙이 동시에 줄어 util이 크게 개선되는 것을 확인할 수 있었다. 이때도 핵심은 순서와 불변식—coalesce 전 remove_free, 분할 시 형식 최소치 준수였던 것 같다.

마지막으로 상수 튜닝(CHUNKSIZE, SPLIT_LIMIT, 버퍼 크기) 는 만능 스위치가 아니라 정책을 드러내는 파라미터임을 깨달았다. 수치를 바꾸기보다 “왜 이 상황에서 이 정책이 필요한가”를 추론하고, trace별 증상(피크 힙↑, 스플린터↑, 이동 재할당↑) 과 연결해 조정했을 때 점수가 가장 안정적으로 오르는 것을 확인할 수 있었다.

## 참고문헌
[CS:APP(3판) Chapter 9.9 – Dynamic Memory Allocation](https://product.kyobobook.co.kr/detail/S000001868716)



