/*
 * Challenge 01 — Use After Free (심화: vtable 기반 위젯 시스템)
 *
 * [시나리오]
 *   아주 작은 GUI 흉내. 각 위젯(Widget)은 힙 객체이며 첫 멤버로 "vtable"
 *   (render/on_event 함수 포인터 묶음)을 가진다. Screen 은 위젯 포인터 배열을
 *   들고 있고, 이벤트를 나눠준 뒤(dispatch) 한 프레임을 그린다(render).
 *
 * [기대 동작]
 *   버튼/라벨/다이얼로그를 그리고, 닫기 이벤트 후 남은 위젯만 다시 그린 뒤
 *   정상 종료(0).
 *
 * [증상]
 *   닫기 이벤트 핸들러가 다이얼로그 위젯을 free() 하지만, Screen 의 포인터 배열에서
 *   그 슬롯을 제거(NULL 로)하지 않는다. 그 사이 앱이 상태 메시지 버퍼를 새로 할당하며
 *   방금 해제된 청크를 재사용해 vtable 포인터 자리를 덮어쓴다.
 *   다음 렌더 패스에서 해제된 위젯의 w->vtbl->render 를 호출 → 망가진 함수 포인터로
 *   점프 → SIGSEGV. 크래시는 render 루프에서 나지만, 원인은 멀리 떨어진 close 핸들러다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=01_use_after_free
 *   (gdb) run                         → 크래시(SIGSEGV)
 *   (gdb) bt                          → screen_render() 안 w->vtbl->render(w) 지점
 *   (gdb) print w                     → 어떤 위젯인지(주소/슬롯) 확인
 *   (gdb) print w->vtbl               → 오염돼 있음
 *   (gdb) print s->items[2]           → 이미 해제된 슬롯이 그대로 남아있음
 *   (gdb) break widget_destroy        → 누가/언제 이 위젯을 free 하는지 역추적
 *
 * [printf(로그)로 잡기]
 *   위젯 해제 시점과 렌더 시점의 vtbl 값을 각각 찍어 "해제가 사용보다 먼저"인지 확인:
 *     (destroy) fprintf(stderr, "destroy id=%d w=%p vtbl=%p\n", w->id,(void*)w,(void*)w->vtbl);
 *     (render)  fprintf(stderr, "render  id=%d w=%p vtbl=%p\n", w->id,(void*)w,(void*)w->vtbl);
 *   → 같은 주소가 destroy 후 render 에서 다시 나오고, vtbl 값이 달라져 있으면 UAF.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: "해제"와 "슬롯 정리"를 한 곳에서 같이 하세요. 위젯 자신은 Screen 을 모르므로
 *       (dialog_on_event 는 self 만 안다) 이벤트 핸들러에서는 closed 표시만 남기고,
 *       Screen 쪽에서 closed 위젯을 free 한 뒤 그 슬롯을 NULL 로 만드는 편이 자연스럽습니다.
 *       이후 dispatch/render 루프가 NULL 슬롯을 건너뛰게 하세요. "해제 = 소유 포인터 무효화".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct Widget Widget; // 구조체 정의

typedef struct {
    void (*render)(Widget *self); // 함수를 가리키는 포인터 함수 호출은 X 호출시에는 render(포인터 변수)
    void (*on_event)(Widget *self, int code); // 이벤트 발생시 실행 함수를 가리켜주는 포인터
} VTable; 

struct Widget {
    const VTable *vtbl; // Vtable 포인터
    int id; // 위젯 아이디
    int closed; // 위젯 상태 1이면 closed
    char label[24]; // 레이블 (출력되는 말)
};

#define MAX_WIDGETS 8 // 최대 위젯
typedef struct {
    Widget *items[MAX_WIDGETS]; // 위젯 리스트 (최대 8개)
    int count; // ?
} Screen;

/* ── 위젯 종류별 동작 ─────────────────────────────────────────── */
static void button_render(Widget *self) { // 버튼 이벤트, id, 명령
    printf("  [Button #%d] \"%s\"\n", self->id, self->label);
}
static void label_render(Widget *self) { // 안내말 출력됨, id, label
    printf("  Label #%d: %s\n", self->id, self->label);
}
static void dialog_render(Widget *self) { // 한번더 물어봄, id, label
    printf("  <<Dialog #%d>> %s\n", self->id, self->label);
}

static void widget_noop_event(Widget *self, int code) { (void)self; (void)code; } // 그냥 아무것도 안하는 코드

/* 다이얼로그는 이벤트 코드 1(닫기)을 받으면 스스로 정리(파괴)된다 */
static void dialog_on_event(Widget *self, int code);

// 각 위젯 종류가 어떤 함수를 사용할지 정해놓은 Vtable 객체를 만드는 코드
// Button, Label은 이벤트가 아무것도 안함.
static const VTable BUTTON_VT = { button_render, widget_noop_event }; 
static const VTable LABEL_VT  = { label_render,  widget_noop_event };
// Dialog는 이벤트가 존재함. 이벤트가 1이면 closed함
static const VTable DIALOG_VT = { dialog_render, dialog_on_event  };

static Widget *widget_new(const VTable *vt, int id, const char *label) {
    /* [Thinking Point]
    *   w 에 아직 아무 값도 넣지 않았는데, sizeof *w 로 *w 를 써도 괜찮은 이유는?
    *   tip 1. sizeof 는 피연산자를 '실행(역참조)'하지 않고 '타입'만 본다.
    *          → *w 의 타입(Widget)만 필요할 뿐, w 를 실제로 따라가지 않는다.
    *   tip 2. 그래서 sizeof *w 는 (VLA 제외) 컴파일 타임에 sizeof(Widget) 상수로 치환된다.
    *   생각해보기: sizeof(Widget) 대신 sizeof *w 로 쓰면 어떤 장점이 있을까?
    */
    Widget *w = malloc(sizeof *w); // malloc 할당 malloc(sizeof *w)랑 malloc(Widget)이랑 같은 뜻 *w가 Widget타입이란걸 컴파일러가 이미 알고있음.
    if (!w) { perror("malloc"); exit(1); }
    w->vtbl = vt;
    w->id = id;
    w->closed = 0;
    strncpy(w->label, label, sizeof(w->label) - 1);
    w->label[sizeof(w->label) - 1] = '\0';
    return w;
}

static void widget_destroy(Widget *w) { // 위젯을 free해주는 함수
    // w = NULL; // NULL로 초기화 -> 잘못된 방법인 이유
    /*
        Widget을 아예 해제하지 않는 코드가 됨. C의 함수 인자는 값 복사(call by value)임.
        w가 NULL이 됬음으로 free(NULL)을 하게됨 때문에 메모리를 해제되지 않음. 
        때문에 댕글링 포인터였던 w(dialog가 있는 items[2])가 정상적으로 접근이 가능해서 정상적인 프로그램처럼 보인것임.
    */
    free(w);
}

/* ── Screen ──────────────────────────────────────────────────── */
// 스크린안 items 배열안에 위젯 앞에서부터 채워줌.
static void screen_add(Screen *s, Widget *w) {
    if (s->count < MAX_WIDGETS) s->items[s->count++] = w;
}

// 이벤트 넣어줌 다른 위젯은 이벤트에 1넣어줘도 동작 X dialog만 free해주는 이벤트임 - 단 문제에서는 free는 되어있지만 다른곳에서 사용하려해서 세그먼트 폴트 발생함
static void screen_dispatch(Screen *s, int code) {
    for (int i = 0; i < s->count; i++) {
        Widget *w = s->items[i];
        if(s->items[i]==NULL){ // 스크린 items배열에서 NULL값이 있으면 건너뜀
            continue;
        }
        w->vtbl->on_event(w, code);
    }
}

// 렌더링 하는 코드
static void screen_render(Screen *s) {
    for (int i = 0; i < s->count; i++) {
        Widget *w = s->items[i];
        if(w!=NULL && w->closed==1){ // items[i]의 위젯이가 NULL이 아니고 closed = 1이라면
            s->items[i] = NULL; // 지정해제해주기
            free(w); // 말록 해제
        }
        if(s->items[i]==NULL){ // NULL 이면 무시하고 건너뜀
            continue;
        }
        w->vtbl->render(w);  // 문제의 코드 free()로 참조함
    }
}

static void dialog_on_event(Widget *self, int code) {
    if (code == 1) {
        self->closed = 1;
        // widget_destroy(self); event에서는 free하지 않음 screen 쪽에서 closed=1인 위젯을 free하고 그 슬롯을 null로 바꿈
    }
}

static char *app_build_status(const char *text) {
    char *msg = malloc(sizeof(Widget)); // 위젯크기만큼 메모리할당 이유 : dialog도 위젯이기때문에 방금 해제된 공간이 다시 재사용될 가능성이 높음
    if (!msg) exit(1); // 말록 실패시 오류상태 1로 종료

    /* [테스트용 연출] 재사용한 메모리를 0xAB 로 '일부러' 덮어써서 오염시킨다.
     * 실무라면 다른 기능이 우연히 이 자리를 덮어쓰겠지만, 여기서는 UAF 크래시를
     * 매번 똑같이(결정적으로) 재현하기 위해 인위적으로 채운다. 
     * glibc(리눅스) 환경 (tcache)에서만 유효하다. 환경&상황에 따라 msg는 새로운 주소로 할당될 수 있다.
     */
    memset(msg, 0xAB, sizeof(Widget)); // memset() 메모리를 특정 바이트값으로 채우는 함수 memset(시작주소, 채울바이트값, 몇바이트)
    snprintf(msg, sizeof(Widget), "STATUS: %s", text); // 0xAB로 덮어씌운 msg에서 text문자열 길이만큼 다시 덮어서씀 그리고 문자열주소를 반환함.
    return msg;
}

int main(void) {
    Screen s = { .count = 0 };

    screen_add(&s, widget_new(&LABEL_VT,  10, "Welcome"));
    screen_add(&s, widget_new(&BUTTON_VT, 11, "OK"));
    screen_add(&s, widget_new(&DIALOG_VT, 12, "Are you sure?"));  /* items[2] */
    screen_add(&s, widget_new(&BUTTON_VT, 13, "Cancel"));

    printf("frame 1:\n");
    screen_render(&s);
    screen_dispatch(&s, 1);

    /* TODO 닫힌(closed) 위젯을 여기서 정리(free + 해당 슬롯 NULL)할 필요가 있음 */

    char *status = app_build_status("dialog closed"); // 다이얼로그 
    printf("%s\n", status);

    printf("frame 2:\n");
    screen_render(&s); // 문제의 코드

    free(status);
    for (int i = 0; i < s.count; i++) free(s.items[i]);
    return 0;
}
