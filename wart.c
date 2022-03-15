/*
 * wart.c -- WebAssembly Actor Runtime
 * Copyright 2022 Dale Schumacher
 */

/**
See further [https://github.com/organix/mycelia/blob/master/wart.md]
**/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>  // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>  // for PRIiPTR, PRIuPTR, PRIXPTR, etc.

#define DEBUG(x)   // include/exclude debug instrumentation
#define XDEBUG(x) x // include/exclude extra debugging

#define INT_T_32B 0  // universal Integer is 32 bits wide
#define INT_T_64B 1  // universal Integer is 64 bits wide

typedef intptr_t int_t;
typedef uintptr_t nat_t;
typedef void *ptr_t;

#define INT(n) ((int_t)(n))
#define NAT(n) ((nat_t)(n))
#define PTR(n) ((ptr_t)(n))

typedef struct cell {
    int_t       head;
    int_t       tail;
} cell_t;

#define PROC_DECL(name)  int_t name(int_t self, int_t args)

typedef PROC_DECL((*proc_t));

#define TAG_MASK    INT(0x3)
#define TAG_FIXNUM  INT(0x0)
#define TAG_PAIR    INT(0x1)
#define TAG_SYMBOL  INT(0x2)
#define TAG_ACTOR   INT(0x3)

#define MK_NUM(n)   (INT(n)<<2)
#define MK_PAIR(p)  (INT(p)|TAG_PAIR)
#define MK_SYM(n)   (INT(n)<<2|TAG_SYMBOL)
#define MK_ACTOR(p) (INT(p)|TAG_ACTOR)
#define MK_BOOL(b)  ((b) ? TRUE : FALSE)

#define IS_NUM(v)   ((v)&TAG_MASK==TAG_FIXNUM)
#define IS_PAIR(v)  ((v)&TAG_MASK==TAG_PAIR)
#define IS_SYM(v)   ((v)&TAG_MASK==TAG_SYMBOL)
#define IS_ACTOR(v) ((v)&TAG_MASK==TAG_ACTOR)

#define TO_INT(v)   (INT(v)>>2)
#define TO_NAT(v)   (NAT(v)>>2)
#define TO_PTR(v)   PTR((v)&~TAG_MASK)

void newline() {  // DO NOT MOVE -- USED TO DEFINE is_proc()
    printf("\n");
    fflush(stdout);
}

#define OK          (0)
#define UNDEF       MK_ACTOR(&a_undef)
#define UNIT        MK_ACTOR(&a_unit)
#define FALSE       MK_ACTOR(&a_false)
#define TRUE        MK_ACTOR(&a_true)
#define NIL         MK_ACTOR(&a_nil)
#define FAIL        MK_ACTOR(&a_fail)

// FORWARD DECLARATIONS
PROC_DECL(Undef);
PROC_DECL(Unit);
PROC_DECL(Boolean);
PROC_DECL(Null);
PROC_DECL(Fail);

int_t is_proc(int_t val);
void print(int_t value);
void debug_print(char *label, int_t value);
static void hexdump(char *label, int_t *addr, size_t cnt);

const cell_t a_undef = { .head = INT(Undef), .tail = UNDEF };
const cell_t a_unit = { .head = INT(Unit), .tail = UNDEF };
const cell_t a_false = { .head = INT(Boolean), .tail = 0 };
const cell_t a_true = { .head = INT(Boolean), .tail = -1 };
const cell_t a_nil = { .head = INT(Null), .tail = NIL };
const cell_t a_fail = { .head = INT(Fail), .tail = UNDEF };

int_t is_raw(int_t val) {
    // FIXME: need to find a better way to handle non-pointer values
    if (val < 1024) return TRUE;
    // FIXME: symbols are only byte-aligned, so we'll get false-positives
    if (val & 0x3) return TRUE;
    return FALSE;
}

/*
 * error handling
 */

int_t panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return UNDEF;  // not reached, but typed for easy swap with error()
}

int_t error(char *reason) {
    fprintf(stderr, "\nERROR! %s\n", reason);
    return UNDEF;
}

int_t failure(char *_file_, int _line_) {
    fprintf(stderr, "\nASSERT FAILED! %s:%d\n", _file_, _line_);
    return UNDEF;
}

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * heap memory management (cells)
 */

#define CELL_MAX (1024)
cell_t cell[CELL_MAX] = {
    { .head = CELL_MAX, .tail = 1 },  // root cell
    { .head = 0, .tail = 0 },  // end of free-list
};
// note: free-list is linked by index, not with pointers

int_t in_heap(int_t val) {
    return MK_BOOL(NAT(PTR(val) - PTR(cell)) < sizeof(cell));
}

int_t cell_new() {
    int_t head = cell[0].tail;
    int_t next = cell[head].tail;
    if (next) {
        // use cell from free-list
        cell[0].tail = next;
        return INT(&cell[head]);
    }
    next = head + 1;
    if (next < cell[0].head) {
        // extend top of heap
        cell[next].head = 0;
        cell[next].tail = 0;
        cell[0].tail = next;
        return INT(&cell[head]);
    }
    return panic("out of cell memory");
}

int_t cell_free(int_t val) {
    if (in_heap(val) == FALSE) panic("free() of non-heap cell");
    cell_t *p = PTR(val);
    p->head = 0;
    // link into free-list
    p->tail = cell[0].tail;
    cell[0].tail = INT(p - cell);
    return NIL;
}

int_t cons(int_t head, int_t tail) {
    int_t val = cell_new();
    if (val != UNDEF) {
        cell_t *p = PTR(val);
        p->head = head;
        p->tail = tail;
    }
    return val;
}

#define list_0  NIL
#define list_1(v1)  cons((v1), NIL)
#define list_2(v1,v2)  cons((v1), cons((v2), NIL))
#define list_3(v1,v2,v3)  cons((v1), cons((v2), cons((v3), NIL)))
#define list_4(v1,v2,v3,v4)  cons((v1), cons((v2), cons((v3), cons((v4), NIL))))
#define list_5(v1,v2,v3,v4,v5)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), NIL)))))

int_t car(int_t val) {
    if (val == NIL) return error("car() of NIL");
    if (val == UNDEF) return UNDEF;
    // FIXME: need better type checking...
    cell_t *p = PTR(val);
    return p->head;
}

int_t cdr(int_t val) {
    if (val == NIL) return error("cdr() of NIL");
    if (val == UNDEF) return UNDEF;
    // FIXME: need better type checking...
    cell_t *p = PTR(val);
    return p->tail;
}

int_t set_car(int_t val, int_t head) {
    if (in_heap(val) == FALSE) panic("set_car() of non-heap cell");
    cell_t *p = PTR(val);
    return p->head = head;
}

int_t set_cdr(int_t val, int_t tail) {
    if (in_heap(val) == FALSE) panic("set_cdr() of non-heap cell");
    cell_t *p = PTR(val);
    return p->tail = tail;
}

static int_t get_head(int_t val) {  // car() without type-checks
    cell_t *p = PTR(val);
    return p->head;
}

static int_t get_tail(int_t val) {  // cdr() without type-checks
    cell_t *p = PTR(val);
    return p->tail;
}

PROC_DECL(obj_call) {
    proc_t p = PTR(get_head(self));
    if (is_proc(INT(p)) == FALSE) error("obj_call() requires a procedure");
    return (p)(self, args);
}

int_t cell_usage() {
    int_t count = 0;
    int_t prev = 0;
    int_t next = cell[prev].tail;
    while (cell[next].tail) {
        ++count;
        prev = next;
        next = cell[prev].tail;
    }
    XDEBUG(fprintf(stderr,
        "cell usage: free=%"PRIdPTR" total=%"PRIdPTR" max=%"PRIdPTR"\n",
        count, next-1, INT(CELL_MAX)));
    return cons(count, next-1);  // cells (free, heap)
}

/*
 * interned strings (symbols)
 */

#define INTERN_MAX (1024)
char intern[INTERN_MAX] = {
    0,  // end of interned strings
};

int_t is_symbol(int_t val) {
    return MK_BOOL(NAT(PTR(val) - PTR(intern)) < sizeof(intern));
}

int_t symbol(char *s) {
    int_t j;
    int_t n = 0;
    while (s[n]) ++n;  // compute c-string length
    int_t i = 0;
    while (intern[i]) {
        int_t m = intern[i++];  // symbol length
        if (n == m) {
            for (j = 0; (j < n); ++j) {
                if (s[j] != intern[i+j]) {
                    goto next;
                }
            }
            // found it!
            return INT(&intern[i-1]);
        }
next:   i += m;
    }
    // new symbol
    intern[i++] = n;
    for (j = 0; (j < n); ++j) {
        intern[i+j] = s[j];
    }
    intern[i+j] = 0;
    return INT(&intern[i-1]);
}

int_t s_quote;
int_t s_typeq;
int_t s_eval;
int_t s_apply;
int_t s_if;
int_t s_map;
int_t s_fold;
int_t s_foldr;
int_t s_bind;
int_t s_lookup;
int_t s_match;
int_t s_content;

// runtime initialization
int_t symbol_boot() {
    s_quote = symbol("quote");
    s_typeq = symbol("typeq");
    s_eval = symbol("eval");
    s_apply = symbol("apply");
    s_if = symbol("if");
    s_map = symbol("map");
    s_fold = symbol("fold");
    s_foldr = symbol("foldr");
    s_bind = symbol("bind");
    s_lookup = symbol("lookup");
    s_match = symbol("match");
    s_content = symbol("content");
    return OK;
}

/*
 * actor primitives
 */

int_t is_actor(int_t val) {
    if (is_raw(val) == TRUE) return FALSE;
    if (val == UNDEF) return FALSE;  // FIXME: is UNDEF really an actor?
    return is_proc(get_head(val));
}

int_t effect_new() {
    return cons(NIL, cons(NIL, NIL));  // empty effect
}

int_t actor_create(int_t code, int_t data) {
    return cons(code, data);
}

int_t effect_create(int_t effect, int_t new_actor) {
    ASSERT(in_heap(new_actor) == TRUE);
    ASSERT(is_actor(new_actor) == TRUE);
    int_t created = cons(new_actor, car(effect));
    if (in_heap(created) == FALSE) return UNDEF;
    set_car(effect, created);
    return effect;
}

int_t actor_send(int_t target, int_t msg) {
    ASSERT(is_actor(target) == TRUE);
    return cons(target, msg);
}

int_t effect_send(int_t effect, int_t new_event) {
    ASSERT(in_heap(new_event) == TRUE);
    int_t rest = cdr(effect);
    int_t sent = cons(new_event, car(rest));
    set_car(rest, sent);
    return effect;
}

int_t actor_become(int_t code, int_t data) {
    return cons(code, data);
}

int_t effect_become(int_t effect, int_t new_beh) {
    ASSERT(in_heap(new_beh) == TRUE);
    int_t rest = cdr(effect);
    if (cdr(rest) != NIL) return error("must only BECOME once");
    set_cdr(rest, new_beh);
    return effect;
}

/*
 * actor event dispatch
 */

static cell_t event_q = { .head = NIL, .tail = NIL };

int_t event_q_append(int_t events) {
    if (events == NIL) return OK;  // nothing to add
    ASSERT(in_heap(events) == TRUE);
    // find the end of events
    int_t tail = events;
    while (get_tail(tail) != NIL) {
        tail = get_tail(tail);
    }
    // append events on event_q
    if (event_q.head == NIL) {
        event_q.head = events;
    } else {
        set_cdr(event_q.tail, events);
    }
    event_q.tail = tail;
    return OK;
}

int_t event_q_take() {
    if (event_q.head == NIL) return UNDEF; // event queue empty
    int_t head = event_q.head;
    event_q.head = get_tail(head);
    if (event_q.head == NIL) {
        event_q.tail = NIL;  // empty queue
    }
    int_t event = get_head(head);
    head = cell_free(head);
    return event;
}

int_t apply_effect(int_t self, int_t effect) {
    if (effect == NIL) return OK;  // no effect
    if (in_heap(effect) == FALSE) return UNDEF;
    int_t actors = get_head(effect);
    if (actors == FAIL) return effect;  // error thrown
    int_t rest = get_tail(effect);
    effect = cell_free(effect);
    while (in_heap(actors) == TRUE) {  // free list, but not actors
        int_t next = get_tail(actors);
        cell_free(actors);
        actors = next;
    }
    int_t events = get_head(rest);
    int_t beh = get_tail(rest);
    rest = cell_free(rest);
    // update behavior
    if ((in_heap(beh) == TRUE) && (is_actor(self) == TRUE)) {
        set_car(self, get_head(beh));
        set_cdr(self, get_tail(beh));
        beh = cell_free(beh);
    }
    // add events to dispatch queue
    return event_q_append(events);
}

int_t event_dispatch() {
    int_t event = event_q_take();
    if (in_heap(event) == FALSE) return UNDEF;  // nothing to dispatch
    int_t target = get_head(event);
    int_t msg = get_tail(event);
    event = cell_free(event);
    // invoke actor behavior
    int_t effect = obj_call(target, msg);
    return apply_effect(target, effect);
}

int_t event_loop() {
    int_t result = OK;
    while (result == OK) {
        result = event_dispatch();
    }
    return result;
}

/*
 * actor behaviors
 */

#define GET_VARS()     int_t vars = get_tail(self)
#define POP_VAR(name)  int_t name = car(vars); vars = cdr(vars)

#define POP_ARG(name)  int_t name = car(args); args = cdr(args)
#define END_ARGS()     if (args != NIL) return error("too many args")

PROC_DECL(sink_beh) {
    GET_VARS();
    XDEBUG(debug_print("sink_beh args", args));
    return vars;
}

const cell_t a_sink = { .head = INT(sink_beh), .tail = NIL };
#define SINK  INT(&a_sink)

PROC_DECL(assert_beh) {
    GET_VARS();
    XDEBUG(debug_print("assert_beh self", self));
    if (args != vars) {
        XDEBUG(debug_print("assert_beh actual", args));
        XDEBUG(debug_print("assert_beh expect", vars));
        return panic("assert_beh expect != actual");
    }
    return NIL;
}

/*
 * ground environment
 */

static PROC_DECL(Type) {
    DEBUG(debug_print("Type self", self));
    DEBUG(debug_print("Type args", args));
    int_t T = get_head(self);  // behavior proc serves as a "type" identifier
    DEBUG(debug_print("Type T", T));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_typeq) {
        POP_ARG(match_T);
        DEBUG(debug_print("Type match_T", match_T));
        END_ARGS();
        int_t effect = effect_new();
        int_t result = MK_BOOL(T == match_T);
        DEBUG(debug_print("Type result", result));
        effect = effect_send(effect, actor_send(cust, result));
        return effect;
    }
    return UNDEF;
}

static PROC_DECL(SeType) {
    DEBUG(debug_print("SeType self", self));
    DEBUG(debug_print("SeType args", args));
    int_t orig = args;
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_eval) {
        POP_ARG(_env);
        END_ARGS();
        int_t effect = effect_new();
        effect = effect_send(effect, actor_send(cust, self));
        return effect;
    }
    return Type(self, orig);  // delegate to Type
}

PROC_DECL(Undef) {
    XDEBUG(debug_print("Undef self", self));
    XDEBUG(debug_print("Undef args", args));
    return SeType(self, args);  // delegate to SeType
}

PROC_DECL(Unit) {
    XDEBUG(debug_print("Unit self", self));
    XDEBUG(debug_print("Unit args", args));
    return SeType(self, args);  // delegate to SeType
}

PROC_DECL(Boolean) {
    GET_VARS();
    XDEBUG(debug_print("Boolean self", self));
    XDEBUG(debug_print("Boolean vars", vars));
    XDEBUG(debug_print("Boolean args", args));
    int_t orig = args;
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_if) {
        POP_ARG(cnsq);
        POP_ARG(altn);
        POP_ARG(env);
        END_ARGS();
        int_t effect = effect_new();
        effect = effect_send(
            effect,
            actor_send(
                (vars ? cnsq : altn),
                list_3(cust, s_eval, env)
            )
        );
        return effect;
    }
    return SeType(self, orig);  // delegate to SeType
}

PROC_DECL(Null) {
    XDEBUG(debug_print("Null self", self));
    XDEBUG(debug_print("Null args", args));
    return SeType(self, args);  // delegate to SeType
}

PROC_DECL(Fail) {
    XDEBUG(debug_print("Fail self", self));
    XDEBUG(debug_print("Fail args", args));
    return error("FAILED");
}

/*
 * display procedures
 */

int_t is_pair(int_t val) {
    if (is_raw(val) == TRUE) return FALSE;
    if (is_symbol(val) == TRUE) return FALSE;
    if (is_proc(val) == TRUE) return FALSE;
    if (is_actor(val) == TRUE) return FALSE;
    if (val == UNDEF) return FALSE;
    return in_heap(val);
}

void print(int_t value) {
    if (value == OK) {
        printf("#ok");
    } else if (is_symbol(value) == TRUE) {
        char *s = PTR(value);
        printf("%.*s", (int)(*s), (s + 1));
    } else if (is_raw(value) == TRUE) {
        printf("%+"PRIdPTR"", value);
    } else if (is_proc(value) == TRUE) {
        printf("#proc-%"PRIxPTR"", value);
    } else if (value == UNDEF) {
        printf("#undefined");
    } else if (value == UNIT) {
        printf("#unit");
    } else if (value == FALSE) {
        printf("#f");
    } else if (value == TRUE) {
        printf("#t");
    } else if (value == NIL) {
        printf("()");
    } else if (value == FAIL) {
        printf("#fail");
    } else if (is_actor(value) == TRUE) {
        printf("#actor-%"PRIxPTR"", value);
    } else if (is_pair(value) == TRUE) {
        char *s = "(";
        while (is_pair(value) == TRUE) {
            printf("%s", s);
            XDEBUG(fflush(stdout));
            print(car(value));
            s = " ";
            value = cdr(value);
        };
        if (value != NIL) {
            printf(" . ");
            XDEBUG(fflush(stdout));
            print(value);
        }
        printf(")");
    } else {
        printf("#unknown-%"PRIxPTR"", value);
    }
    XDEBUG(fflush(stdout));
}

void debug_print(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " 16#%"PRIxPTR"", value);
    //fprintf(stderr, " %"PRIdPTR"", value);
    if (is_raw(value) == TRUE) {
        fprintf(stderr, " RAW");
    }
    if (is_symbol(value) == TRUE) {
        fprintf(stderr, " SYM");
    }
    if (is_proc(value) == TRUE) {
        fprintf(stderr, " PROC");
    }
    if (is_actor(value) == TRUE) {
        fprintf(stderr, " ACTOR");
    }
    if (is_pair(value) == TRUE) {
        fprintf(stderr, " <%"PRIxPTR",%"PRIxPTR">",
            get_head(value), get_tail(value));
    }
    //fprintf(stderr, "\n");
    fprintf(stderr, " ");
    fflush(stderr);
    print(value);
    newline();
}

#if INT_T_32B
static void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %04"PRIxPTR"..", label, (NAT(addr) >> 16));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x7) == 0x0) {
            fprintf(stderr, "\n..%04"PRIxPTR":", (NAT(addr) & 0xFFFF));
        }
        fprintf(stderr, " %08"PRIXPTR"", NAT(*addr++) & 0xFFFFFFFF);
    }
    fprintf(stderr, "\n");
}
#endif // INT_T_32B

#if INT_T_64B
static void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %08"PRIxPTR"..", label, (NAT(addr) >> 32));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x3) == 0x0) {
            fprintf(stderr, "\n..%08"PRIxPTR":", (NAT(addr) & 0xFFFFFFFF));
        }
        fprintf(stderr, " %016"PRIXPTR"", NAT(*addr++));
    }
    fprintf(stderr, "\n");
}
#endif // INT_T_64B

/*
 * unit tests
 */

int_t test_values() {
    XDEBUG(debug_print("test_values FALSE", FALSE));
    XDEBUG(debug_print("test_values TRUE", TRUE));
    XDEBUG(debug_print("test_values NIL", NIL));
    XDEBUG(debug_print("test_values UNIT", UNIT));
    XDEBUG(debug_print("test_values FAIL", FAIL));
    XDEBUG(debug_print("test_values UNDEF", UNDEF));
    XDEBUG(debug_print("test_values s_quote", s_quote));
    XDEBUG(debug_print("test_values s_match", s_match));
    return OK;
}

int_t test_cells() {
    int_t v, v0, v1, v2;
    int_t n;
    cell_t c;

    v = cons(TRUE, FALSE);
    ASSERT(in_heap(v) == TRUE);
    XDEBUG(debug_print("test_cells cons v", v));
    XDEBUG(debug_print("test_cells cons car(v)", car(v)));
    XDEBUG(debug_print("test_cells cons cdr(v)", cdr(v)));
    ASSERT(car(v) == TRUE);
    ASSERT(cdr(v) == FALSE);

    v0 = cons(v, NIL);
    XDEBUG(debug_print("test_cells cons v0", v0));
    ASSERT(in_heap(v0) == TRUE);

    //v1 = list_3(-1, 2, 3);
    v1 = list_3(s_quote, s_eval, s_apply);
    XDEBUG(debug_print("test_cells cons v1", v1));
    ASSERT(in_heap(v1) == TRUE);

    v2 = cell_free(v0);
    XDEBUG(debug_print("test_cells free v0", v2));
    ASSERT(v2 == NIL);

    v2 = cons(INT(Fail), v1);
    XDEBUG(debug_print("test_cells cons v2", v2));
    ASSERT(in_heap(v2) == TRUE);
    ASSERT(PTR(v2) == PTR(v0));  // re-used cell?
    v1 = obj_call(v2, v);

    v = cell_free(v);
    v2 = cell_free(v2);
    ASSERT(v2 == NIL);

    XDEBUG(hexdump("cell", PTR(cell), 16));
    int_t usage = cell_usage();
    ASSERT(car(usage) == 2);
    ASSERT(cdr(usage) == 5);
    usage = cell_free(usage);

    return OK;
}

int_t test_actors() {
    int_t effect = effect_new();
    DEBUG(debug_print("test_actors new effect", effect));
    int_t a = actor_create(INT(sink_beh), NIL);
    DEBUG(debug_print("test_actors a", a));
    effect = effect_create(effect, a);
    DEBUG(debug_print("test_actors create effect", effect));
    int_t m = list_3(SINK, s_eval, NIL);
    DEBUG(debug_print("test_actors m", m));
    int_t e = actor_send(a, m);
    DEBUG(debug_print("test_actors e", e));
    effect = effect_send(effect, e);
    DEBUG(debug_print("test_actors send effect", effect));
    int_t x = apply_effect(UNDEF, effect);
    DEBUG(debug_print("test_actors apply effect", x));
    int_t r = event_dispatch();
    XDEBUG(debug_print("test_actors event_dispatch", r));

#if 1
    effect = effect_new();
    // UNIT is self-evaluating
    a = actor_create(INT(assert_beh), UNIT);
    effect = effect_create(effect, a);
    m = list_3(a, s_eval, NIL);
    XDEBUG(debug_print("test_actors m_1", m));
    e = actor_send(UNIT, m);
    effect = effect_send(effect, e);
    // UNIT has Unit type
    a = actor_create(INT(assert_beh), TRUE);
    effect = effect_create(effect, a);
    m = list_3(a, s_typeq, INT(Unit));
    XDEBUG(debug_print("test_actors m_2", m));
    e = actor_send(UNIT, m);
    effect = effect_send(effect, e);
    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    r = event_loop();
    XDEBUG(debug_print("test_actors event_loop", r));
#endif

    return OK;
}

int_t unit_tests() {
    if (test_values() != OK) return UNDEF;
    if (test_cells() != OK) return UNDEF;
    if (test_actors() != OK) return UNDEF;
    int_t usage = cell_usage();
    usage = cell_free(usage);
    return OK;
}

/*
 * bootstrap
 */

int_t actor_boot() {
    if (symbol_boot() != OK) return UNDEF;
    return OK;
}

int main(int argc, char const *argv[])
{
    int_t result = actor_boot();
    if (result != OK) panic("actor_boot() failed");

    fprintf(stderr, "newline = %"PRIxPTR"\n", INT(newline));
    fprintf(stderr, "  Undef = %"PRIxPTR"\n", INT(Undef));
    fprintf(stderr, "   Unit = %"PRIxPTR"\n", INT(Unit));
    fprintf(stderr, "   main = %"PRIxPTR"\n", INT(main));
    fprintf(stderr, "is_proc = %"PRIxPTR"\n", INT(is_proc));
    fprintf(stderr, "  UNDEF = %"PRIxPTR"\n", UNDEF);
    fprintf(stderr, "   UNIT = %"PRIxPTR"\n", UNIT);
    ASSERT(INT(newline) < INT(main));

    ASSERT(is_proc(INT(Undef)) == TRUE);
    //ASSERT(is_actor(UNDEF) == TRUE);

    ASSERT(is_proc(INT(Unit)) == TRUE);
    ASSERT(is_raw(UNIT) == FALSE);
    ASSERT(UNIT != UNDEF);
    ASSERT(is_proc(car(UNIT)) == TRUE);
    ASSERT(is_actor(UNIT) == TRUE);

    fprintf(stderr, "   cell = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(cell), NAT(sizeof(cell)));
    fprintf(stderr, " intern = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(intern), NAT(sizeof(intern)));
    ASSERT((NAT(cell) & 0x7) == 0x0);

    fprintf(stderr, "s_quote = %"PRIxPTR"\n", s_quote);
    fprintf(stderr, "s_match = %"PRIxPTR"\n", s_match);
    ASSERT(is_symbol(s_match) == TRUE);

#if 1
    result = unit_tests();
    XDEBUG(debug_print("result", result));
#endif

#if 0
    cell_t *p = PTR(UNDEF);
    p->tail = NIL;  // FIXME: should not be able to assign to `const`
#endif

    return (result == OK ? 0 : 1);
}

int_t is_proc(int_t val) {
    return MK_BOOL((val >= INT(newline)) && (val <= INT(main)));
}
