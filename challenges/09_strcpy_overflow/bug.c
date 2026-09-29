/*
 * Challenge 09 — strcpy 힙 오버플로 (심화: join 크기계산 off-by-one)
 *
 * [시나리오]
 *   여러 조각(parts)을 구분자 없이 이어 붙여 하나의 문자열을 만드는 join().
 *   필요한 크기를 먼저 계산(joined_size)해 malloc 한 뒤, 각 조각을 순서대로 복사한다.
 *
 * [기대 동작]
 *   모든 조각을 이어 붙인 결과 길이를 출력하고 정상 종료.
 *
 * [증상]
 *   크기 계산 함수 joined_size() 의 루프가 `i < n - 1` 이라, "마지막 조각"의 길이를
 *   더하지 않는다. 그런데 실제 복사 루프는 `i < n` 으로 마지막 조각까지 복사한다.
 *   마지막 조각이 크면(여기서는 큰 본문), 할당량보다 훨씬 많이 써서 힙을 크게 넘어간다.
 *   → 힙 메타데이터 손상(이후 free 에서 abort) 또는 매핑 밖 접근으로 SIGSEGV.
 *   크래시는 strcpy/free 에서 나지만, 원인은 "크기 계산의 off-by-one"이다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=09_strcpy_overflow
 *   (gdb) run                       → 크래시(SIGSEGV 또는 abort)
 *   (gdb) bt                        → join 의 strcpy 또는 free 근처
 *   (gdb) break joined_size ; run    → 반환값(need)과 실제 필요한 총합을 비교
 *   (gdb) print need                → 마지막 조각 길이가 빠져 need 가 부족함을 확인
 *
 * [printf(로그)로 잡기]
 *   계산한 크기와 실제로 복사한 바이트를 비교 출력:
 *     fprintf(stderr, "alloc=%zu copied=%zu\n", need, off);
 *   → copied 가 alloc 을 넘어서면 그 초과분이 힙을 침범한 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 크기 계산 루프를 `i < n` 으로 고쳐 모든 조각 길이와 종료 문자('\0') 자리를
 *       빠짐없이 더한다. "계산 루프와 복사 루프의 범위를 반드시 일치"시킨다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 필요한 총 바이트 수 = 모든 조각 길이 합 + 종료 문자 1 */
static size_t joined_size(const char *const *parts, int n) {
    size_t total = 1;                        /* '\0' 자리 */
    for (int i = 0; i < n; i++) { // n-1까지 반복하던 코드를 수정하여 해결함   
        total += strlen(parts[i]);
    }
    // printf("total:%d\n", total);
    return total;
}

static char *join(const char *const *parts, int n) { // 문자열의 문자를 수정 X, 문자열의 포인터 자체도 바꾸기 X
    size_t need = joined_size(parts, n);
    char *out = malloc(need);                /* 마지막 조각 길이만큼 부족하게 할당됨 */
    if (!out) { perror("malloc"); exit(1); }

    size_t off = 0;
    for (int i = 0; i < n; i++) {            /* 복사는 마지막 조각까지 전부 → 오버플로 */
        strcpy(out + off, parts[i]);
        off += strlen(parts[i]);
    }
    out[off] = '\0';
    return out;
}

int main(void) {
    
    static char body[200000];
    memset(body, 'x', sizeof body - 1);
    // fprintf(stdout, "body = %p, %p\n", *body, body);
    body[sizeof body - 1] = '\0';

    const char *parts[] = { "GET ", "/index.html", " HTTP/1.1\r\n\r\n", body };
    int n = (int)(sizeof(parts) / sizeof(parts[0]));
    // printf("n = %d\nsizeof parts = %d\nsizeof parts[0] = %d\n", n, (int)(sizeof parts), (int)(sizeof parts[0]));

    char *msg = join(parts, n);              /* 복사 중 힙 오버플로 → 크래시 */

    printf("joined length = %zu\n", strlen(msg));
    free(msg);
    return 0;
}
