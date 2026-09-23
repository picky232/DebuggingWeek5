/*
 * Challenge 04 — Double Free (심화: 두 인덱스가 같은 객체를 가리키는 별칭)
 *
 * [시나리오]
 *   직원 레코드(Rec)를 힙에 만들고, "ID 인덱스"(by_id)와 "이름 순 인덱스"(by_name)
 *   두 배열로 관리한다. 두 인덱스는 정렬만 다를 뿐, 결국 "같은 Rec 객체들"을 가리킨다
 *   (별칭, aliasing). 조회/출력 후 프로그램 끝에서 전부 정리한다.
 *
 * [왜 directory_sort_by_id 는 없나? (혼동 주의)]
 *   - 이 챌린지의 핵심은 "정렬"이 아니라 "두 인덱스가 같은 객체를 공유(별칭)해서
 *     생기는 이중 해제"다. 그 대비를 보여주려면 한쪽(by_name)만 재배치하면 충분하다.
 *   - by_id 는 삽입 순서 그대로 두는 "소유(owning) 인덱스" 역할이라 정렬이 필요 없다.
 *     (find_by_id 도 선형 탐색이라 정렬돼 있지 않아도 동작한다)
 *   - 따라서 by_id 는 실제로 "id 오름차순"이 아니라 삽입 순서일 뿐이며,
 *     directory_sort_by_id 는 의도적으로 만들지 않았다(버그 시연에 불필요).
 *
 * [기대 동작]
 *   레코드를 만들고 ID/이름으로 조회해 출력한 뒤, 누수 없이 정리하고 정상 종료.
 *
 * [증상]
 *   정리 함수가 "두 인덱스를 각각 순회하며 free" 한다. 하지만 두 인덱스는 같은
 *   Rec 객체들을 공유하므로, by_id 로 한 번, by_name 으로 또 한 번 → 같은 포인터를
 *   두 번 free. glibc 가 "double free or corruption" 으로 SIGABRT.
 *   각 인덱스가 독립된 소유권을 가진 것처럼 착각하기 쉬운 것이 함정.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=04_double_free
 *   (gdb) run                       → abort
 *   (gdb) bt                        → directory_free() 의 두 번째 free 루프
 *   (gdb) frame N ; print d->by_name[i] → 이 주소가 앞서 by_id 로 이미 free 됐는지 확인
 *   (gdb) print d->by_id[0]          
 *
 * [printf(로그)로 잡기]
 *   free 직전마다 주소를 찍어 같은 주소가 두 번 나오는지 본다:
 *     fprintf(stderr, "free rec=%p (%s)\n", (void*)r, tag);
 *   → by_id 루프와 by_name 루프에서 동일 주소가 각각 나오면 이중 해제.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 소유권은 한 곳만 갖게 한다. 예) by_id 를 "소유 인덱스"로 정하고 여기서만 해제,
 *       by_name 은 "관찰용(빌려온) 인덱스"로 두어 절대 free 하지 않는다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int   id;
    char *name;      
} Rec; // 레코드 : id, 이름

#define MAXN 16
typedef struct {
    Rec *by_id[MAXN];
    Rec *by_name[MAXN];    
    int  count;
} Directory;  // Rec를 가리키는 포인터 배열(by_id, by_name)과 개수를 관리하는 구조체

// 새로운 rec를 만드는 함수
static Rec *rec_new(int id, const char *name) {
    Rec *r = malloc(sizeof *r); // rec 구조체 바이트 크기만큼 메모리 크기를 할당해줌
    if (!r) { perror("malloc"); exit(1); }
    r->id = id;
    r->name = malloc(strlen(name) + 1); // 다른 힙 메모리 공간에 할당함 때문에 free시 각각 해줘야함
    if (!r->name) { perror("malloc"); exit(1); }
    strcpy(r->name, name);
    return r;
}

// 디렉토리에 새로운 rec를 넣는 함수
static void directory_add(Directory *d, int id, const char *name) {
    Rec *r = rec_new(id, name);
    d->by_id[d->count]   = r;
    d->by_name[d->count] = r;      /* 같은 포인터를 두 인덱스에 함께 등록 */
    d->count++;
}

/* 이름 순 인덱스를 사전순으로 정렬(포인터만 재배치, 객체는 공유 그대로) */
static void directory_sort_by_name(Directory *d) {
    for (int i = 0; i < d->count; i++) {
        for (int j = i + 1; j < d->count; j++) {
            // strcmp 는 두 문자열을 비교하는 함수로 같으면 0 첫번째 문자열이 사전식으로 앞이면 음수, 반대면 양수값이 나옴
            if (strcmp(d->by_name[i]->name, d->by_name[j]->name) > 0) { 
                Rec *t = d->by_name[i];
                d->by_name[i] = d->by_name[j];
                d->by_name[j] = t;
            }
        }
    }
}

// id로 rec찾기
static Rec *find_by_id(Directory *d, int id) {
    for (int i = 0; i < d->count; i++)
        if (d->by_id[i]->id == id) return d->by_id[i];
    return NULL;
}

// 출력
static void directory_dump(Directory *d) {
    printf("by id:  ");
    for (int i = 0; i < d->count; i++) printf("%d:%s ", d->by_id[i]->id, d->by_id[i]->name);
    printf("\nby name:");
    for (int i = 0; i < d->count; i++) printf(" %s(%d)", d->by_name[i]->name, d->by_name[i]->id);
    printf("\n");
}

// 메모리 할당 헤제
static void directory_free(Directory *d) {
    for (int i = 0; i < d->count; i++) {
        free(d->by_id[i]->name); // 문자열 힙 공간 free - 이거 안해주면 정상적인 방법으로 메모리의 주소를 못알아냄. 때문에 메모리 누수 발생함.
        free(d->by_id[i]);       // 구조체 힙 공간 free
    }
    // for (int i = 0; i < d->count; i++) {
    //     free(d->by_name[i]);               
    // }
    d->count = 0;
}

int main(void) {
    Directory dir = { .count = 0 };

    directory_add(&dir, 3, "carol");
    directory_add(&dir, 1, "alice");
    directory_add(&dir, 4, "dave");
    directory_add(&dir, 2, "bob");

    directory_sort_by_name(&dir);
    directory_dump(&dir);

    Rec *r = find_by_id(&dir, 2);
    if (r) printf("lookup id=2 -> %s\n", r->name);

    directory_free(&dir);                  
    printf("done\n");
    return 0;
}
