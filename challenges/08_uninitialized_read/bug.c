/*
 * Challenge 08 — 미초기화 포인터 읽기 (심화: 더티 힙 재사용)
 *
 * [시나리오]
 *   희소 행렬(sparse matrix)을 "행 포인터 표"로 표현한다. 
 *   희소 행렬(Sparse Matrix)은 대부분의 원소가 0인 행렬을 말한다.
 * 예시 : 
 * 0 0 0 0 5
 * 0 0 3 0 0
 * 0 0 0 0 0
 * 7 0 0 0 0
 * 0 0 0 9 0
 *
 * rows[i] 는 i 번째 행
 * 배열을 가리키며, 실제 데이터가 있는 행만 malloc 해서 연결한다.
 *
 * [기대 동작]
 *   채운 행만 안전하게 합산해 출력.
 *
 * [증상]
 *   행 포인터 표를 malloc 으로 잡는데, malloc 은 메모리를 0 으로 초기화하지 않는다.
 *   게다가 이 표는 방금 free 된(=쓰레기로 채워진) 청크를 재사용하므로, 채우지 않은
 *   칸은 NULL 이 아니라 0xABAB.. 같은 '그럴듯한 쓰레기 포인터'가 된다.
 *   합산 루프가 채우지 않은 행까지 rows[i][j] 로 역참조하면 무효 주소 접근 → SIGSEGV.
 *   ("NULL 이면 걸러지겠지" 라는 방심이 깨지는 지점 — 쓰레기는 NULL 이 아니다)
 *
 * [gdb 로 잡기]
 *   make gdb NAME=08_uninitialized_read
 *   (gdb) run                       → 크래시(SIGSEGV)
 *   (gdb) bt                        → row_sum 의 rows[i][j] 지점
 *   (gdb) print i                   → 어느 행에서 죽었는지
 *   (gdb) print rows[i]             → 0xabab.. 등 초기화되지 않은 쓰레기 포인터
 *
 * [printf(로그)로 잡기]
 *   각 행 포인터를 접근 전에 찍어 초기화 여부를 확인:
 *     fprintf(stderr, "rows[%d]=%p\n", i, (void*)rows[i]);
 *   → 채우지 않은 행이 (nil) 이 아니라 쓰레기 주소로 찍히면 그게 원인.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 포인터 표는 calloc 으로 0(=NULL) 초기화하거나, 명시적으로 NULL 로 채운 뒤
 *       "NULL 이 아닌 행만" 접근하세요. (초기화되지 않은 값 = 쓰레기, NULL 아님)
 *
 * [환경 의존성 — 반드시 제공된 Docker(리눅스+glibc) 안에서 실행]
 *   이 실습이 "방금 free 한 청크를 곧바로 재사용해 크래시한다"고 장담할 수 있는 이유는
 *   실행 환경을 리눅스 + glibc(ptmalloc2)로 고정했기 때문이다.
 *   - glibc 2.26+ 의 tcache 는 작은 블록을 free 하면 크기별 통(bin)에 LIFO(스택)로
 *     넣어두고, "같은 크기"를 다시 malloc 하면 방금 넣은 블록을 그대로 되돌려준다.
 *     → dirty_heap() 가 0xAB 로 더럽혀 free 한 256B 청크를, 직후 make_matrix() 의
 *       malloc(256B) 이 거의 결정적으로 다시 받는다(그 사이 같은 크기 할당이 없으므로).
 *   - 반면 C 표준이 보장하는 것은 "malloc 값은 불특정(쓰레기)"뿐이다. tcache/fastbin
 *     같은 재사용 세부는 glibc 전용 구현이며, macOS(libmalloc)·Windows(HeapAlloc)·
 *     musl 등 다른 할당기에서는 동작이 달라 크래시가 다르게 나거나 우연히 안 날 수 있다.
 *   → 그래서 결과의 일관성을 위해 이 코드는 반드시 제공된 리눅스/glibc Docker 에서
 *     실행한다. (교훈 자체 "미초기화 = NULL 아닌 쓰레기" 는 OS 무관하게 항상 참)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROWS 32
#define COLS 4

/* 힙을 '더럽혀' 두어, 이후 같은 크기 할당이 쓰레기 값을 물려받게 만든다.
   (실무에서 흔한 '이전에 쓰고 free 한 청크의 잔여물' 상황을 재현) */
static void dirty_heap(void) {
    void *scratch = malloc(ROWS * sizeof(int *));
    if (scratch) {
        memset(scratch, 0xAB, ROWS * sizeof(int *));
        free(scratch);              /* glibc tcache 로 반환 → 같은 크기 malloc 이 이 블록을
                                       LIFO 로 되돌려받는다(리눅스+glibc 고정이라 결정적). */
    }
}

static int **make_matrix(void) {

    // malloc에서 calloc으로 변경한 이유는 malloc은 힙메모리를 할당만 해주는 것이여서 쓰레기값이 그대로 남아있지만 calloc은 할당한 메모리를 0으로 초기화 해주기 때문에 calloc을 사용함
    // calloc을 사용하는 방법은 calloc(원소 개수, 원소 하나의 크기) 로 지정해서 선언해주면 됨.
    int **rows = calloc(ROWS, sizeof(int *)); // int * 포인터들을 가리킬 포인터여서
    if (!rows) { perror("malloc"); exit(1); }

    for (int i = 0; i < ROWS; i += 2) {
        int *r = malloc(COLS * sizeof(int));
        for (int j = 0; j < COLS; j++) r[j] = i * COLS + j;
        rows[i] = r;
    }
    return rows;
}

static long row_sum(int **rows, int nrows) {
    long total = 0;
    for (int i = 0; i < nrows; i++) {
        for (int j = 0; j < COLS; j++) {
            if(!rows[i]) continue;
            fprintf(stderr, "rows[%d]=%p\n", i, (void*)rows[i]);
            total += rows[i][j];
        }
    }
    return total;
}

int main(void) {
    dirty_heap();

    int **rows = make_matrix();
    printf("summing %dx%d matrix...\n", ROWS, COLS);

    long s = row_sum(rows, ROWS);     

    printf("sum = %ld\n", s);

    for (int i = 0; i < ROWS; i += 2) free(rows[i]);
    free(rows);
    return 0;
}
