/*
 * Challenge 06 — NULL Pointer Dereference (심화: HTTP 헤더 파서)
 *
 * [시나리오]
 *   "Key: Value" 형식의 헤더 블록을 줄 단위로 파싱한다. 각 줄에서 ':' 를 찾아
 *   그 자리를 '\0' 로 끊어 key/value 로 나눈 뒤 목록에 저장한다.
 *
 * [기대 동작]
 *   모든 헤더를 key/value 로 나눠 저장하고 개수와 내용을 출력.
 *
 * [증상]
 *   대부분의 줄에는 ':' 가 있지만, 한 줄("Connection")에는 ':' 가 없다.
 *   strchr(line, ':') 이 그 줄에서 NULL 을 돌려주는데, 이를 검사하지 않고
 *   `*colon = '\0'` 로 곧장 쓴다 → NULL 주소에 쓰기 → SIGSEGV.
 *   여러 줄을 도는 루프 안에 묻혀 있어, "어느 줄에서" 죽는지 gdb 로 짚어야 한다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=06_null_deref
 *   (gdb) run                       → 크래시(SIGSEGV)
 *   (gdb) bt                        → parse_headers 의 *colon = '\0' 지점
 *   (gdb) print colon               → colon == 0x0 (strchr 이 NULL 반환)
 *   (gdb) print line                → ':' 가 없는 그 줄("Connection")을 확인
 *
 * [printf(로그)로 잡기]
 *   각 줄에서 strchr 결과를 찍어 NULL 인 줄을 찾는다:
 *     fprintf(stderr, "line=[%s] colon=%p\n", line, (void*)colon);
 *   → colon 이 (nil) 로 찍힌 줄이 원인.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: strchr 의 NULL 반환을 검사하라. ':' 없는 줄은 건너뛰거나 오류로 처리한다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HEADERS 32
typedef struct {
    char *keys[MAX_HEADERS];
    char *vals[MAX_HEADERS];
    int   count;
} Headers;

static char *skip_ws(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void parse_headers(char *text, Headers *h) {
    // strtok 는 문자열 배열을 특정 문자열 기준으로 잘라서 앞에 있는 문자를 반환해주는 함수 
    // strtok(NULL, "기준") 이렇게 쓰면 자르던 문자열에서 이어서 다음 토큰을 찾아라
    for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        char *colon = strchr(line, ':');   
        if (colon == NULL) {
            continue;
        }
        *colon = '\0';
        char *key = line;
        char *val = skip_ws(colon + 1);

        if (h->count < MAX_HEADERS) {
            h->keys[h->count] = key;
            h->vals[h->count] = val;
            h->count++;
        }
    }
}

int main(void) {

    char raw[] =
        "Host: example.com\n"
        "Accept: */*\n"
        "Connection\n"                     
        "User-Agent: memdbg-cli\n";

    Headers h = { .count = 0 };
    parse_headers(raw, &h);                

    printf("parsed %d headers\n", h.count);
    for (int i = 0; i < h.count; i++)
        printf("  %s = %s\n", h.keys[i], h.vals[i]);
    return 0;
}
