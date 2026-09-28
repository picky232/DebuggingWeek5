/*
 * Challenge 07 — Stack Use After Return (심화: 지역 배열 주소가 탈출)
 *
 * [시나리오]
 *   문자열을 줄 단위로 쪼개, 각 줄의 시작 주소들을 담은 "뷰(LineView)"를 만든다.
 *   split_lines() 가 만든 뷰를 호출자가 받아서 출력한다.
 *
 * [기대 동작]
 *   "alpha / beta / gamma" 세 줄로 쪼갠 뒤, 줄 수와 각 줄 첫 글자의 합을 출력
 *   (lines = 3, checksum = 298).
 *
 * [증상]
 *   split_lines() 는 줄 포인터들을 '지역 배열' parts[] 에 모은 뒤, 그 배열의 주소를
 *   LineView.lines 에 담아 돌려준다. 함수가 끝나면 parts[] 가 있던 스택 프레임은
 *   무효가 되고, 이어서 호출되는 warm_stack() 이 그 자리를 다른 값으로 덮는다.
 *   그 뒤 v.lines[i] 를 읽으면 '덮인 쓰레기'를 포인터로 해석해 역참조 → SIGSEGV.
 *   (v.lines 자체는 유효한 스택 주소지만, 그 안의 내용이 이미 오염됐다는 점이 함정)
 *
 * [gdb 로 잡기]
 *   make gdb NAME=07_stack_use_after_return
 *   (gdb) run                     → 크래시(SIGSEGV)
 *   (gdb) bt                      → main 의 checksum += v.lines[i][0] 지점
 *   (gdb) print v.lines           → split_lines 안 parts[] 의 (이미 무효인) 스택 주소
 *   (gdb) print v.lines[0]        → 0x4141414141414141 같은 오염된(무효) 포인터
 *   (gdb) break split_lines       → parts 주소를 확인하고, 반환 후 그 값이 어떻게 덮이는지 관찰
 *
 * [printf(로그)로 잡기]
 *   함수 안에서 parts 주소를, main 에서 v.lines 를 각각 찍어 "같은 스택 주소를
 *   함수 밖에서 쓰는지" 확인:
 *     (split_lines) fprintf(stderr, "parts=%p\n", (void*)parts);
 *     (main)        fprintf(stderr, "v.lines=%p v.lines[0]=%p\n",
 *                           (void*)v.lines, (void*)v.lines[0]);
 *   → 같은 주소를 함수 밖에서 참조하고, 그 내용이 warm_stack 이후 달라져 있으면 SAR.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 지역 배열의 주소를 밖으로 돌려주지 말라. 호출자가 소유하는 저장소(배열/힙)에
 *       결과를 채우거나, 힙에 할당해 수명을 넘기세요.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 8
typedef struct {
    char **lines;    /* 줄 포인터들의 '배열'을 가리킨다 */
    int    count;
} LineView;

/* 결과를 뷰에 채운다(포인터를 함수 경계 너머로 옮겨 -Wdangling 을 회피하는 형태) */
static void view_set(LineView *out, char **arr, int n) {
    out->lines = arr;
    out->count = n;
}

static void split_lines(LineView *out, char *text, char **parts) {
    // 말록 할당
    // char **parts = malloc(sizeof(char *) * MAX_LINES); // 힙에 메모리 할당함.

    // if(parts == NULL){
    //     out->lines = NULL;
    //     out->count = 0;
    //     return;
    // }

    int n = 0;
    /* strtok는 새로 할당하지 않고, 넘겨받은 문자열 내부의 주소를 돌려준다. 
    * 따라서, strtok은 원본 버퍼를 제자리에서 수정한다. 
    */
    for (char *ln = strtok(text, "\n"); ln && n < MAX_LINES; ln = strtok(NULL, "\n"))
        parts[n++] = ln; // part에 나눈 문자열 넣기

    view_set(out, parts, n); 

    /* TODO 상기 코드를 수정하여 결과를 호출자가 준 out 에 직접 채운다(값 반환 아님, 지역 주소 반환 아님). */       
}

/* split_lines 가 쓰던 스택 프레임을, 같은 모양(char*[8])의 지역 배열로 덮는다.
   무효가 된 parts[] 자리에 '그럴듯한 쓰레기 포인터'가 들어차게 만든다. */
static void warm_stack(void) {
    char *scratch[MAX_LINES];
    for (int i = 0; i < MAX_LINES; i++)
        scratch[i] = (char *)0x4141414141414141ULL;   /* 매핑되지 않은 주소 */
    __asm__ volatile("" :: "r"(scratch) : "memory");   /* 최적화 제거 방지 */
}

int main(void) {
    char text[] = "alpha\nbeta\ngamma";

    LineView v;
    char *storage[MAX_LINES];
    split_lines(&v, text, &storage);
    warm_stack();                        

    long checksum = 0;
    for (int i = 0; i < v.count; i++)
        checksum += (unsigned char)v.lines[i][0];

    printf("lines = %d, checksum = %ld\n", v.count, checksum);
    // free(v.lines);
    return 0;
}
