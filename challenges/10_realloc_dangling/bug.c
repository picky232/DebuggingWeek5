/*
 * Challenge 10 — realloc 후 옛 포인터 사용 (심화: undo 스냅샷 댕글링)
 *
 * [시나리오]
 *   정수 편집 버퍼 EditBuffer. 내용이 커지면 eb_grow() 가 realloc 으로 버퍼를 키운다.
 *   "실행 취소(undo)"를 위해 eb_snapshot() 이 현재 상태를 undo[] 에 저장한다.
 *
 * [기대 동작]
 *   스냅샷을 찍고 값을 많이 추가한 뒤, 정리(eb_free)에서 누수 없이 해제하고 정상 종료.
 *
 * [증상]
 *   eb_snapshot() 이 저장하는 것은 "그 시점의 data 포인터(원시 주소)"다. 이후 eb_grow()
 *   가 realloc 으로 버퍼를 옮기면(주소 변경), 저장해 둔 스냅샷 포인터는 '이미 해제된
 *   옛 블록'을 가리키게 된다(댕글링). 정리 시 eb_free() 는 현재 data 를 해제한 뒤
 *   undo[] 의 옛 포인터들도 free 하는데, 그 블록들은 realloc 이 이미 해제한 것이라
 *   → double free / invalid pointer 로 glibc abort(SIGABRT).
 *
 * [gdb 로 잡기]
 *   make gdb NAME=10_realloc_dangling
 *   (gdb) run                       → abort
 *   (gdb) bt                        → eb_free 의 free(e->undo[i]) 지점
 *   (gdb) print e->undo[i]          → 이 주소가 현재 data 와 다른 '옛' 주소임을 확인
 *   (gdb) break eb_grow             → realloc 전후 e->data 주소가 바뀌는지 관찰
 *
 * [printf(로그)로 잡기]
 *   grow 에서 realloc 전후 주소를, 스냅샷/해제 시 저장/해제 주소를 찍어 대조:
 *     (grow)     fprintf(stderr, "grow old=%p new=%p\n", (void*)old, (void*)e->data); // old 추가 후 확인
 *     (snapshot) fprintf(stderr, "snap  save=%p\n", (void*)e->data);
 *     (free)     fprintf(stderr, "free  undo[%d]=%p\n", i, (void*)e->undo[i]);
 *   → snapshot 이 저장한 주소가 grow 에서 이동해 이미 해제된 뒤, free 에서 다시
 *     그 주소를 해제하면 이중 해제.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 스냅샷은 "원시 버퍼 포인터"가 아니라 내용의 '복사본'을 따로 소유해야 한다.
 *       (예: 스냅샷 시 malloc+memcpy 로 별도 버퍼를 만들고, 그 복사본만 해제)
 *       realloc 이후에는 옛 포인터를 절대 사용/해제하지 말 것.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UNDO 8
typedef struct {
    int   *data;
    size_t len, cap; // 메모리크기, 객체의 크기, 개수 등을 표현하기 위해 만들어진 부호 없는 정수 타입
    int   *clipboard; // 
    int   *undo[MAX_UNDO]; // 스냅샷 저장
    int    undo_n; // undo 개수 
} EditBuffer;

// 최초 정의 EditBuffer를 힙 메모리에 할당 + 최초값 정의함
static void eb_init(EditBuffer *e) {
    e->cap = 4;
    e->len = 0;
    e->undo_n = 0;
    e->data = malloc(e->cap * sizeof(int));
    if (!e->data) { perror("malloc"); exit(1); }
    /* data 바로 뒤에 놓이는 별도 할당. data 가 힙 맨 끝(top)이 아니게 되어
       이후 realloc 이 제자리 확장 대신 '이동'을 택하게 만든다(→ 옛 블록 해제). */
    e->clipboard = malloc(e->cap * sizeof(int)); // 따로 realloc전에 원본 저장
    if (!e->clipboard) { perror("malloc"); exit(1); }
}

static void eb_snapshot(EditBuffer *e) {
    int *new = malloc(e->cap * sizeof(int));
    for(int i=0; i<e->len; i++){
        new[i] = e->data[i];
    }
    if (e->undo_n < MAX_UNDO){
        e->undo[e->undo_n++] = new; // 현재 버전 기억 (스냅샷)
    }
    // e->undo배열에 undo_n(idx)위치에 현재 malloc한 힙 메모리 크기 원본 데이터 저장
}

static void eb_grow(EditBuffer *e, size_t need) {
    size_t nc = e->cap; 
    while (nc < need) nc *= 2;
    int *p = realloc(e->data, nc * sizeof(int)); // realloc으로 힙 공간 크기 늘림 2배
    if (!p) { perror("realloc"); free(e->data); exit(1); }
    e->data = p; // 늘린 공간으로 변경
    e->cap = nc; // 늘린 크기로 변경
}

static void eb_push(EditBuffer *e, int v) {
    if (e->len == e->cap) eb_grow(e, e->len + 1); // len이 cap(크기)와 같으면 힙공간 크기 키움 eb_grow()
    e->data[e->len++] = v;
}

static void eb_free(EditBuffer *e) {
    free(e->data);
    free(e->clipboard);
    // undo에는 복사본이 아닌 e->data라는 realloc된 이미 free된 포인터가 지정되어있음
    for (int i = 0; i < e->undo_n; i++) {
        free(e->undo[i]);
    }
    e->undo_n = 0;
    e->data = NULL;
}

int main(void) {
    EditBuffer e;
    eb_init(&e);

    for (int i = 0; i < 3; i++) eb_push(&e, i);

    eb_snapshot(&e);                 

    for (int i = 0; i < 4000; i++) eb_push(&e, i);     

    printf("len=%zu cap=%zu head=%d tail=%d\n",
           e.len, e.cap, e.data[0], e.data[e.len - 1]);

    eb_free(&e);                     
    printf("done\n");
    return 0;
}
