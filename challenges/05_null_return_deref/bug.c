/*
 * Challenge 05 — NULL 반환 미확인 역참조 (심화: 설정 템플릿 확장기)
 *
 * [시나리오]
 *   key=value 설정 저장소(Config)와, "${key}" 자리표시자를 실제 값으로 치환하는
 *   템플릿 확장기 expand() 를 만든다. 예: "http://${host}:${port}/${path}".
 *
 * [기대 동작]
 *   템플릿의 모든 ${key} 를 설정값으로 치환한 최종 문자열을 출력.
 *
 * [증상]
 *   cfg_get() 은 키가 없으면 NULL 을 돌려준다. expand() 는 이 반환값을 검사하지 않고
 *   곧장 strlen()/memcpy() 에 넘긴다. 템플릿에 설정에 없는 키(${path})가 섞여 있으면
 *   그 순간 v==NULL 이 되어 strlen(NULL) 에서 SIGSEGV.
 *   크래시는 strlen(libc) 안에서 나지만, 원인은 "검사 없이 흘려보낸 NULL 반환값"이다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=05_null_return_deref
 *   (gdb) run                     → 크래시(SIGSEGV)
 *   (gdb) bt                      → strlen ← expand ← main
 *   (gdb) frame 1 ; print key      → 어떤 키를 찾다가 죽었는지(예: "path")
 *   (gdb) print v                  → v == 0x0 (cfg_get 이 NULL 을 돌려줬음)
 *   (gdb) break expand             → 각 ${key} 마다 cfg_get 결과를 살펴 NULL 을 잡기
 *
 * [printf(로그)로 잡기]
 *   치환 직전 키와 조회 결과 포인터를 함께 찍는다:
 *     fprintf(stderr, "expand key=%s v=%p\n", key, (void*)v);
 *   → v 가 (nil) 로 찍힌 키가 원인.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: cfg_get() 의 NULL 반환을 반드시 검사하라. 없는 키는 기본값("")으로 대체하거나
 *       명시적 오류로 처리한다("사용 전에 검사" 원칙).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KV 16 // 최대 크기 16
typedef struct {
    const char *keys[MAX_KV];
    const char *vals[MAX_KV];
    int n;
} Config;

// Config 구조체 세팅
static void cfg_set(Config *c, const char *k, const char *v) {
    if (c->n < MAX_KV) {
        c->keys[c->n] = k; 
        c->vals[c->n] = v;
        c->n++; // 배열 인덱스 증가
    }
}

// Config 안 특정 값 가져오기
static const char *cfg_get(const Config *c, const char *k) {
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->keys[i], k) == 0) // c->keys[i]와 k가 같으면 0
            return c->vals[i];
    return NULL;                       /* 없는 키 → NULL */
}

static void expand(const Config *c, const char *tmpl, char *out, size_t outcap) {
    size_t o = 0;
    for (const char *p = tmpl; *p; ) { // 문자열 배열을 포인터로 잡고 +1씩 하며 각 문자들 순회
        if (p[0] == '$' && p[1] == '{') {
            const char *end = strchr(p, '}'); // p가 가리키는 문자열에서 }를 찾아서 그 주소를 반환해라
            if (!end) break;
            char key[32];
            size_t kl = (size_t)(end - (p + 2)); // ${} 사이에 문자열 길이를 구하는 식
            if (kl >= sizeof key) kl = sizeof key - 1; // 31바이트보다 긴 문자열의 경우에는 잘라냄
            memcpy(key, p + 2, kl); // memcpy(dest, src, n) src주소부터 n바이트를 읽어서 dest주소에 n바이트를 복사
            key[kl] = '\0'; // 널 문자 (문자열이 여기서 끝남)

            const char *v = cfg_get(c, key); // 키값이 있으면 키와 같은 인덱스를 가진val 반환, 없으면 NULL 반환
            if(!v){ // 사용전 빈값으로 반환
                v = "";
            }
            fprintf(stderr, "expand key=%s v=%p\n", key, (void*)v);
            size_t vl = strlen(v); // 반환한 문자열 길이
            if (o + vl < outcap) { memcpy(out + o, v, vl); o += vl; } 
            // out배열에 + o크기를 더한 인덱스 위치의 포인터에 v문자를 vl바이트 만큼 추가, 바이트 크기만큼 더하기(복사해서 넣엇으니까)
            p = end + 1;// 갱신
        } else {
            // out 배열에 포인터 p메모리 안 데이터 넣고 p++
            if (o + 1 < outcap) out[o++] = *p; 
            p++;
        }
    }
    out[o] = '\0';
}

int main(void) {
    /* [Thinking Point]
     * "{ .n = 0 }" 은 멤버 이름을 콕 집어 초기화하는 '지정 초기화자(designated initializer)'다.
     *   tip 1. 초기화자에 하나라도 값을 주면, 명시하지 않은 나머지 멤버는 전부 0 으로
     *          채워진다. 즉 keys[], vals[] 배열도 모두 NULL 로 초기화된다.
     *   tip 2. 만약 그냥 "Config cfg;" 로만 뒀다면 지역 변수라 n·keys·vals 가 쓰레기 값이다.
     *   생각해보기: n 이 쓰레기 값이면 cfg_set/cfg_get 에서 무슨 일이 벌어질까?
     *               */
    Config cfg = { .n = 0 };
    cfg_set(&cfg, "host", "example.com");
    cfg_set(&cfg, "port", "8080");

    /* [Thinking Point]
     * "${host}" 처럼 ${...} 로 감싼 부분은 expand 함수가 설정값으로 치환하는 'placeholder' 다.
     *   tip 1. 이 문자열 자체는 그냥 상수 텍스트일 뿐, 컴파일러가 ${...} 를 해석하지 않는다.
     *          실제 치환은 런타임에 expand 함수 안에서 키를 찾아 값을 끼워넣는 방식으로 일어난다.
     *   tip 2. cfg_get("path") 는 등록되지 않은 키라 NULL 을 돌려준다.
     *   생각해보기: 설정에 없는 키(${path})를 만나면 expand() 는 어떤 값을 받게 되고,
     *               그 값을 검사 없이 strlen/복사에 쓰면 무슨 일이 벌어질까?
     *               (힌트: "값이 없다"는 NULL 이지 빈 문자열 ""이 아니다) */
    const char *tmpl = "http://${host}:${port}/${path}/index.html";
    char out[256];

    expand(&cfg, tmpl, out, sizeof out);   /* ${path} 치환 시 NULL 역참조 → 크래시 */

    printf("url = %s\n", out);
    return 0;
}
