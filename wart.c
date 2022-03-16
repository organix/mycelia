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

#define PROC_DECL(name)  int_t name(int_t self, int_t arg)

typedef PROC_DECL((*proc_t));

#define TAG_MASK    INT(0x3)
#define TAG_FIXNUM  INT(0x0)
#define TAG_PAIR    INT(0x1)
#define TAG_SYMBOL  INT(0x2)
#define TAG_ACTOR   INT(0x3)

#define MK_NUM(n)   INT(NAT(n)<<2)
#define MK_PAIR(p)  INT(NAT(p)|TAG_PAIR)
#define MK_SYM(n)   INT((NAT(n)<<2)|TAG_SYMBOL)
#define MK_ACTOR(p) INT(PTR(p)+TAG_ACTOR)

#define MK_BOOL(b)  ((b) ? TRUE : FALSE)
#define MK_PROC(p)  MK_ACTOR(p)

#define IS_ADDR(v)  ((v)&1)

#define IS_NUM(v)   (((v)&TAG_MASK) == TAG_FIXNUM)
#define IS_PAIR(v)  (((v)&TAG_MASK) == TAG_PAIR)
#define IS_SYM(v)   (((v)&TAG_MASK) == TAG_SYMBOL)
#define IS_ACTOR(v) (((v)&TAG_MASK) == TAG_ACTOR)

#define IS_PROC(v)  is_proc(v)

#define TO_INT(v)   (INT(v)>>2)
#define TO_NAT(v)   (NAT(v)>>2)
#define TO_PTR(v)   PTR(NAT(v)&~TAG_MASK)

void newline() {  // DO NOT MOVE -- USED TO DEFINE is_proc()
    printf("\n");
    fflush(stdout);
}

#define OK          (0)
#define INF         MK_NUM(TO_INT(~(NAT(-1)>>1)))
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
//PROC_DECL(false_beh);
//PROC_DECL(true_beh);
PROC_DECL(Null);
PROC_DECL(Pair);
PROC_DECL(Symbol);
PROC_DECL(Fixnum);
PROC_DECL(Fail);

int is_proc(int_t val);
void print(int_t value);
void debug_print(char *label, int_t value);
static void hexdump(char *label, int_t *addr, size_t cnt);

const cell_t a_undef = { .head = MK_PROC(Undef), .tail = UNDEF };
const cell_t a_unit = { .head = MK_PROC(Unit), .tail = UNDEF };
const cell_t a_false = { .head = MK_PROC(Boolean), .tail = FALSE };
const cell_t a_true = { .head = MK_PROC(Boolean), .tail = TRUE };
const cell_t a_nil = { .head = MK_PROC(Null), .tail = NIL };
const cell_t a_fail = { .head = MK_PROC(Fail), .tail = UNDEF };

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

int in_heap(int_t val) {
    return IS_ADDR(val) && (NAT(TO_PTR(val) - PTR(cell)) < sizeof(cell));
}

static cell_t *cell_new() {
    int_t head = cell[0].tail;
    int_t next = cell[head].tail;
    if (next) {
        // use cell from free-list
        cell[0].tail = next;
        return &cell[head];
    }
    next = head + 1;
    if (next < cell[0].head) {
        // extend top of heap
        cell[next].head = 0;
        cell[next].tail = 0;
        cell[0].tail = next;
        return &cell[head];
    }
    panic("out of cell memory");
    return PTR(0);  // NOT REACHED!
}

int_t cell_free(int_t val) {
    if (!in_heap(val)) panic("free() of non-heap cell");
    cell_t *p = TO_PTR(val);
    p->head = 0;
    // link into free-list
    p->tail = cell[0].tail;
    cell[0].tail = INT(p - cell);
    return NIL;
}

int_t cons(int_t head, int_t tail) {
    cell_t *p = cell_new();
    p->head = head;
    p->tail = tail;
    return MK_PAIR(p);
}

#define list_0  NIL
#define list_1(v1)  cons((v1), NIL)
#define list_2(v1,v2)  cons((v1), cons((v2), NIL))
#define list_3(v1,v2,v3)  cons((v1), cons((v2), cons((v3), NIL)))
#define list_4(v1,v2,v3,v4)  cons((v1), cons((v2), cons((v3), cons((v4), NIL))))
#define list_5(v1,v2,v3,v4,v5)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), NIL)))))

int_t car(int_t val) {
    if (!IS_PAIR(val)) return error("car() of non-PAIR");
    cell_t *p = TO_PTR(val);
    return p->head;
}

int_t cdr(int_t val) {
    if (!IS_PAIR(val)) return error("cdr() of non-PAIR");
    cell_t *p = TO_PTR(val);
    return p->tail;
}

int_t set_car(int_t val, int_t head) {
    if (!in_heap(val)) panic("set_car() of non-heap cell");
    cell_t *p = TO_PTR(val);
    return p->head = head;
}

int_t set_cdr(int_t val, int_t tail) {
    if (!in_heap(val)) panic("set_cdr() of non-heap cell");
    cell_t *p = TO_PTR(val);
    return p->tail = tail;
}

int_t get_code(int_t val) {
    if (!IS_ACTOR(val)) return error("car() of non-ACTOR");
    cell_t *p = TO_PTR(val);
    return p->head;
}

int_t get_data(int_t val) {
    if (!IS_ACTOR(val)) return error("cdr() of non-ACTOR");
    cell_t *p = TO_PTR(val);
    return p->tail;
}

// polymorphic dispatch procedure
PROC_DECL(obj_call) {
    int_t code = UNDEF;
    if (IS_PROC(self)) {
        code = self;
    } else if (IS_PAIR(self)) {
        code = MK_PROC(Pair);
    } else if (IS_SYM(self)) {
        code = MK_PROC(Symbol);
    } else if (IS_NUM(self)) {
        code = MK_PROC(Fixnum);
    } else if (IS_ACTOR(self)) {
        code = get_code(self);
    }
    if (!IS_PROC(code)) return error("obj_call() requires a procedure");
    proc_t p = TO_PTR(code);
    return (p)(self, arg);
}

int_t cell_usage() {
    int_t count = 0;
    int_t prev = 0;
    int_t next = cell[0].tail;
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

int is_symbol(int_t val) {
    return IS_SYM(val) && (NAT(TO_PTR(val) - PTR(intern)) < sizeof(intern));
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
            return MK_SYM(i-1);
        }
next:   i += m;
    }
    // new symbol
    intern[i++] = n;
    for (j = 0; (j < n); ++j) {
        intern[i+j] = s[j];
    }
    intern[i+j] = 0;
    return MK_SYM(i-1);
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

int_t effect_new() {
    return cons(NIL, cons(NIL, NIL));  // empty effect
}

int_t actor_create(int_t code, int_t data) {
    if (!IS_PROC(code)) return error("actor code must be a procedure");
    cell_t *p = cell_new();
    p->head = code;
    p->tail = data;
    return MK_ACTOR(p);
}

int_t effect_create(int_t effect, int_t new_actor) {
    ASSERT(IS_ACTOR(new_actor));
    ASSERT(in_heap(new_actor));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect)) {
        int_t created = cons(new_actor, car(effect));
        if (!in_heap(created)) return UNDEF;
        set_car(effect, created);
    }
    return effect;
}

int_t actor_send(int_t target, int_t msg) {
    //ASSERT(IS_ACTOR(target)); -- obj_call() polymorphic dispatch works for _ANY_ value!
    return cons(target, msg);
}

int_t effect_send(int_t effect, int_t new_event) {
    ASSERT(in_heap(new_event));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect)) {
        int_t rest = cdr(effect);
        int_t sent = cons(new_event, car(rest));
        set_car(rest, sent);
    }
    return effect;
}

int_t actor_become(int_t code, int_t data) {
    return cons(code, data);
}

int_t effect_become(int_t effect, int_t new_beh) {
    ASSERT(in_heap(new_beh));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect)) {
        int_t rest = cdr(effect);
        if (cdr(rest) != NIL) return error("must only BECOME once");
        set_cdr(rest, new_beh);
    }
    return effect;
}

/*
 * actor event dispatch
 */

static cell_t event_q = { .head = NIL, .tail = NIL };

int_t event_q_append(int_t events) {
    if (events == NIL) return OK;  // nothing to add
    ASSERT(in_heap(events));
    // find the end of events
    int_t tail = events;
    while (cdr(tail) != NIL) {
        tail = cdr(tail);
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
    event_q.head = cdr(head);
    if (event_q.head == NIL) {
        event_q.tail = NIL;  // empty queue
    }
    int_t event = car(head);
    head = cell_free(head);
    return event;
}

int_t apply_effect(int_t self, int_t effect) {
    DEBUG(debug_print("apply_effect self", self));
    DEBUG(debug_print("apply_effect effect", effect));
    if (effect == NIL) return OK;  // no effect
    if (!IS_PAIR(effect)) {
        XDEBUG(debug_print("apply_effect non-PAIR", effect));
        return UNDEF;
    }
    int_t actors = car(effect);
    if (actors == FAIL) {
        XDEBUG(debug_print("apply_effect error", effect));
        return effect;  // error thrown
    }
    // unchain actors
    DEBUG(debug_print("apply_effect actors", actors));
    int_t rest = cdr(effect);
    effect = cell_free(effect);
    while (IS_PAIR(actors)) {  // free list, but not actors
        int_t next = cdr(actors);
        cell_free(actors);
        actors = next;
    }
    int_t events = car(rest);
    int_t beh = cdr(rest);
    rest = cell_free(rest);
    // update behavior
    DEBUG(debug_print("apply_effect beh", beh));
    if (IS_PAIR(beh) && IS_ACTOR(self)) {
        cell_t *p = TO_PTR(self);
        p->head = car(beh);
        p->tail = cdr(beh);
        beh = cell_free(beh);
    }
    // add events to dispatch queue
    DEBUG(debug_print("apply_effect events", events));
    return event_q_append(events);
}

int_t event_dispatch() {
    int_t event = event_q_take();
    if (!IS_PAIR(event)) return UNDEF;  // nothing to dispatch
    int_t target = car(event);
    DEBUG(debug_print("event_dispatch target", target));
    int_t msg = cdr(event);
    DEBUG(debug_print("event_dispatch msg", msg));
    event = cell_free(event);
    // invoke actor behavior
    int_t effect = obj_call(target, msg);
    DEBUG(debug_print("event_dispatch effect", effect));
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

#define GET_VARS()     int_t vars = get_data(self)
#define POP_VAR(name)  int_t name = car(vars); vars = cdr(vars)
#define TAIL_VAR(name) int_t name = vars

#define GET_ARGS()     int_t args = arg
#define POP_ARG(name)  int_t name = car(args); args = cdr(args)
#define TAIL_ARG(name) int_t name = args
#define END_ARGS()     if (args != NIL) return error("too many args")

PROC_DECL(sink_beh) {
    DEBUG(debug_print("sink_beh self", self));
    XDEBUG(debug_print("sink_beh arg", arg));
    GET_VARS();  // effect
    DEBUG(debug_print("sink_beh vars", vars));
    TAIL_VAR(effect);
    return effect;
}
const cell_t a_sink = { .head = MK_PROC(sink_beh), .tail = NIL };
#define SINK  MK_ACTOR(&a_sink)

PROC_DECL(tag_beh) {
    XDEBUG(debug_print("tag_beh self", self));
    GET_VARS();  // cust
    XDEBUG(debug_print("tag_beh vars", vars));
    TAIL_VAR(cust);
    GET_ARGS();  // msg
    XDEBUG(debug_print("tag_beh args", args));
    TAIL_ARG(msg);
    int_t effect = NIL;

    effect = effect_send(effect,
        actor_send(cust, cons(self, msg)));

    return effect;
}

static PROC_DECL(join_h_beh) {
    XDEBUG(debug_print("join_h_beh self", self));
    GET_VARS();  // (cust head . k_tail)
    XDEBUG(debug_print("join_h_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(head);
    TAIL_VAR(k_tail);
    GET_ARGS();  // (tag . tail)
    XDEBUG(debug_print("join_h_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(tail);
    int_t effect = NIL;

    if (tag == k_tail) {
        effect = effect_send(effect,
            actor_send(cust, cons(head, tail)));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}
static PROC_DECL(join_t_beh) {
    XDEBUG(debug_print("join_t_beh self", self));
    GET_VARS();  // (cust k_head . tail)
    XDEBUG(debug_print("join_t_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(k_head);
    TAIL_VAR(tail);
    GET_ARGS();  // (tag . head)
    XDEBUG(debug_print("join_t_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(head);
    int_t effect = NIL;

    if (tag == k_head) {
        effect = effect_send(effect,
            actor_send(cust, cons(head, tail)));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}
PROC_DECL(join_beh) {
    XDEBUG(debug_print("join_beh self", self));
    GET_VARS();  // (cust k_head . k_tail)
    XDEBUG(debug_print("join_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(k_head);
    TAIL_VAR(k_tail);
    GET_ARGS();  // (tag . value)
    XDEBUG(debug_print("join_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(value);
    int_t effect = NIL;

    if (tag == k_head) {
        effect = effect_become(effect,
            actor_become(MK_PROC(join_h_beh), cons(cust, cons(value, k_tail))));
    } else if (tag == k_tail) {
        effect = effect_become(effect,
            actor_become(MK_PROC(join_t_beh), cons(cust, cons(k_head, value))));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}

PROC_DECL(fork_beh) {
    XDEBUG(debug_print("fork_beh self", self));
    GET_VARS();  // (cust head . tail)
    XDEBUG(debug_print("fork_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(head);
    TAIL_VAR(tail);
    GET_ARGS();  // (h_req . t_req)
    XDEBUG(debug_print("fork_beh args", args));
    POP_ARG(h_req);
    TAIL_ARG(t_req);
    int_t effect = NIL;

    int_t k_head = actor_create(MK_PROC(tag_beh), self);
    effect = effect_create(effect, k_head);

    int_t k_tail = actor_create(MK_PROC(tag_beh), self);
    effect = effect_create(effect, k_tail);

    effect = effect_send(effect,
        actor_send(head, cons(k_head, h_req)));

    effect = effect_send(effect,
        actor_send(tail, cons(k_tail, t_req)));

    effect = effect_become(effect,
        actor_become(MK_PROC(join_beh), cons(cust, cons(k_head, k_tail))));

    return effect;
}

PROC_DECL(assert_beh) {
    XDEBUG(debug_print("assert_beh self", self));
    GET_VARS();  // expect
    DEBUG(debug_print("assert_beh vars", vars));
    TAIL_VAR(expect);
    GET_ARGS();  // actual
    DEBUG(debug_print("assert_beh args", args));
    TAIL_ARG(actual);
    if (expect != actual) {
        XDEBUG(debug_print("assert_beh expect", expect));
        XDEBUG(debug_print("assert_beh actual", actual));
        return panic("assert_beh expect != actual");
    }
    return NIL;
}

/*
 * ground environment
 */

static PROC_DECL(Type) {
    DEBUG(debug_print("Type self", self));
    int_t T = get_code(self);  // behavior proc serves as a "type" identifier
    DEBUG(debug_print("Type T", T));
    GET_ARGS();
    DEBUG(debug_print("Type args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_typeq) {  // (cust 'typeq T)
        POP_ARG(Tq);
        XDEBUG(debug_print("Type T?", Tq));
        END_ARGS();
        int_t value = MK_BOOL(T == Tq);
        XDEBUG(debug_print("Type value", value));
        effect = effect_send(effect, actor_send(cust, value));
        return effect;
    }
    XDEBUG(debug_print("Type NOT UNDERSTOOD", arg));
    return effect_send(effect, actor_send(cust, UNDEF));
}

static PROC_DECL(SeType) {
    DEBUG(debug_print("SeType self", self));
    GET_ARGS();
    DEBUG(debug_print("SeType args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(_env);
        END_ARGS();
        XDEBUG(debug_print("SeType value", self));
        effect = effect_send(effect, actor_send(cust, self));
        return effect;
    }
    return Type(self, arg);  // delegate to Type
}

PROC_DECL(Undef) {
    XDEBUG(debug_print("Undef self", self));
    GET_ARGS();
    XDEBUG(debug_print("Undef args", args));
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Unit) {
    XDEBUG(debug_print("Unit self", self));
    GET_ARGS();
    XDEBUG(debug_print("Unit args", args));
    return SeType(self, arg);  // delegate to SeType
}

static PROC_DECL(Appl_k_args) {
    XDEBUG(debug_print("Appl_k_args self", self));
    GET_VARS();  // (cust oper env)
    XDEBUG(debug_print("Appl_k_args vars", vars));
    POP_VAR(cust);
    POP_VAR(oper);
    POP_VAR(env);
    GET_ARGS();  // opnd
    XDEBUG(debug_print("Appl_k_args args", args));
    TAIL_ARG(opnd);
    int_t effect = NIL;
    effect = effect_send(effect,
        actor_send(oper, list_4(cust, s_apply, opnd, env)));
    return effect;
}
PROC_DECL(Appl) {
    XDEBUG(debug_print("Appl self", self));
    GET_VARS();  // oper
    XDEBUG(debug_print("Appl vars", vars));
    TAIL_VAR(oper);
    GET_ARGS();
    XDEBUG(debug_print("Appl args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        int_t k_args = actor_create(MK_PROC(Appl_k_args), list_3(cust, oper, env));
        effect = effect_create(effect, k_args);
        effect = effect_send(effect,
            actor_send(opnd, list_4(k_args, s_map, s_eval, env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Oper_list) {  // (list . values)
    XDEBUG(debug_print("Oper_list self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_list args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        XDEBUG(debug_print("Oper_list value", opnd));
        effect = effect_send(effect, actor_send(cust, opnd));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_list = { .head = MK_PROC(Appl), .tail = MK_PROC(Oper_list) };

PROC_DECL(Oper_quote) {  // (quote expr)
    XDEBUG(debug_print("Oper_quote self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_quote args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        int_t expr = car(opnd);
        if (cdr(opnd) != NIL) {
            effect = effect_send(effect,
                actor_send(cust, error("expected 1 argument")));
        } else {
            XDEBUG(debug_print("Oper_quote value", expr));
            effect = effect_send(effect, actor_send(cust, expr));
        }
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_quote = { .head = MK_PROC(Oper_quote), .tail = UNDEF };

PROC_DECL(Boolean) {
    XDEBUG(debug_print("Boolean self", self));
    GET_VARS();  // bval
    XDEBUG(debug_print("Boolean vars", vars));
    TAIL_VAR(bval); // FIXME: should be #t/#f delegate
    GET_ARGS();
    XDEBUG(debug_print("Boolean args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_if) {  // (cust 'if cnsq altn env)
        POP_ARG(cnsq);
        POP_ARG(altn);
        POP_ARG(env);
        END_ARGS();
        effect = effect_send(effect,
            actor_send(
                (bval ? cnsq : altn),
                list_3(cust, s_eval, env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Null) {
    XDEBUG(debug_print("Null self", self));
    GET_ARGS();
    XDEBUG(debug_print("Null args", args));
    return SeType(self, arg);  // delegate to SeType
}

static PROC_DECL(Pair_k_apply) {
    XDEBUG(debug_print("Pair_k_apply self", self));
    GET_VARS();  // (cust opnd env)
    XDEBUG(debug_print("Pair_k_apply vars", vars));
    POP_VAR(cust);
    POP_VAR(opnd);
    POP_VAR(env);
    GET_ARGS();  // oper
    XDEBUG(debug_print("Pair_k_apply args", args));
    TAIL_ARG(oper);
    int_t effect = NIL;
    effect = effect_send(effect,
        actor_send(oper, list_4(cust, s_apply, opnd, env)));
    return effect;
}
PROC_DECL(Pair) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Pair self", self));
    GET_ARGS();
    XDEBUG(debug_print("Pair args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(env);
        END_ARGS();
        int_t k_apply = actor_create(MK_PROC(Pair_k_apply), list_3(cust, cdr(self), env));
        effect = effect_create(effect, k_apply);
        effect = effect_send(effect,
            actor_send(car(self), list_3(k_apply, s_eval, env)));
        return effect;
    }
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}

PROC_DECL(Symbol) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Symbol self", self));
    GET_ARGS();
    XDEBUG(debug_print("Symbol args", args));
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}

PROC_DECL(Fixnum) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Fixnum self", self));
    GET_ARGS();
    XDEBUG(debug_print("Fixnum args", args));
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Fail) {
    XDEBUG(debug_print("Fail self", self));
    GET_ARGS();
    XDEBUG(debug_print("Fail args", args));
    return error("FAILED");
}

/*
 * display procedures
 */

void print(int_t value) {
    if (IS_PROC(value)) {
        proc_t p = TO_PTR(value);
        printf("#proc-%"PRIxPTR"", NAT(p));
    } else if (IS_NUM(value)) {
        printf("%+"PRIdPTR"", TO_INT(value));
    } else if (IS_SYM(value)) {
        char *s = &intern[TO_NAT(value)];
        printf("%.*s", (int)(*s), (s + 1));
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
    } else if (IS_ACTOR(value)) {
        cell_t *p = TO_PTR(value);
        printf("#actor-%"PRIxPTR"", NAT(p));
    } else if (IS_PAIR(value)) {
        char *s = "(";
        while (IS_PAIR(value)) {
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
        printf("#unknown?-%"PRIxPTR"", value);
    }
    XDEBUG(fflush(stdout));
}

void debug_print(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " 16#%"PRIxPTR"", value);
    //fprintf(stderr, " %"PRIdPTR"", value);
    if (in_heap(value)) fprintf(stderr, " HEAP");
    //if (IS_ADDR(value)) fprintf(stderr, " ADDR");
    if (IS_PROC(value)) fprintf(stderr, " PROC");
    if (IS_NUM(value)) fprintf(stderr, " NUM");
    if (IS_PAIR(value)) fprintf(stderr, " PAIR");
    if (IS_SYM(value)) fprintf(stderr, " SYM");
    if (IS_ACTOR(value)) fprintf(stderr, " ACTOR");
    if (IS_ADDR(value) && !IS_PROC(value)) {
        cell_t *p = TO_PTR(value);
        fprintf(stderr, " <%"PRIxPTR",%"PRIxPTR">", p->head, p->tail);
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
    XDEBUG(fprintf(stderr, "--test_values--\n"));
    XDEBUG(debug_print("test_values OK", OK));
    XDEBUG(debug_print("test_values INF", INF));
    XDEBUG(debug_print("test_values FALSE", FALSE));
    XDEBUG(debug_print("test_values TRUE", TRUE));
    XDEBUG(debug_print("test_values NIL", NIL));
    XDEBUG(debug_print("test_values UNIT", UNIT));
    XDEBUG(debug_print("test_values FAIL", FAIL));
    XDEBUG(debug_print("test_values UNDEF", UNDEF));
    XDEBUG(debug_print("test_values Undef", MK_PROC(Undef)));
    XDEBUG(debug_print("test_values s_quote", s_quote));
    XDEBUG(debug_print("test_values s_match", s_match));
    XDEBUG(debug_print("test_values SINK", SINK));
    return OK;
}

int_t test_cells() {
    XDEBUG(fprintf(stderr, "--test_cells--\n"));
    int_t v, v0, v1, v2;

    v = cons(TRUE, FALSE);
    ASSERT(in_heap(v));
    XDEBUG(debug_print("test_cells cons v", v));
    XDEBUG(debug_print("test_cells cons car(v)", car(v)));
    XDEBUG(debug_print("test_cells cons cdr(v)", cdr(v)));
    ASSERT(car(v) == TRUE);
    ASSERT(cdr(v) == FALSE);

    v0 = cons(v, NIL);
    XDEBUG(debug_print("test_cells cons v0", v0));
    ASSERT(in_heap(v0));

    v1 = list_3(MK_NUM(-1), MK_NUM(2), MK_NUM(3));
    //v1 = list_3(s_quote, s_eval, s_apply);
    XDEBUG(debug_print("test_cells cons v1", v1));
    ASSERT(in_heap(v1));

    v2 = cell_free(v0);
    XDEBUG(debug_print("test_cells free v0", v2));
    ASSERT(v2 == NIL);

    //v2 = actor_create(MK_PROC(Fail), v1);
    v2 = actor_create(MK_PROC(sink_beh), v1);
    XDEBUG(debug_print("test_cells cons v2", v2));
    ASSERT(in_heap(v2));
    ASSERT(TO_PTR(v2) == TO_PTR(v0));  // re-used cell?
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
    XDEBUG(fprintf(stderr, "--test_actors--\n"));
    int_t effect = NIL; //effect_new();
    DEBUG(debug_print("test_actors effect", effect));
    int_t a = actor_create(MK_PROC(sink_beh), NIL);
    DEBUG(debug_print("test_actors actor_create", a));
    effect = effect_create(effect, a);
    DEBUG(debug_print("test_actors effect_create", effect));
    int_t m = list_3(SINK, s_eval, NIL);
    DEBUG(debug_print("test_actors message", m));
    int_t e = actor_send(a, m);
    DEBUG(debug_print("test_actors actor_send", e));
    effect = effect_send(effect, e);
    DEBUG(debug_print("test_actors effect_send", effect));
    int_t x = apply_effect(UNDEF, effect);
    DEBUG(debug_print("test_actors apply_effect", x));
    int_t r = event_dispatch();
    XDEBUG(debug_print("test_actors event_dispatch", r));
    if (r != OK) return r;  // early exit on failure...

#if 1
    effect = NIL; //effect_new();
    // UNIT is self-evaluating
    a = actor_create(MK_PROC(assert_beh), UNIT);
    effect = effect_create(effect, a);
    m = list_3(a, s_eval, NIL);
    XDEBUG(debug_print("test_actors m_1", m));
    e = actor_send(UNIT, m);
    effect = effect_send(effect, e);
    // UNIT has Unit type
    a = actor_create(MK_PROC(assert_beh), TRUE);
    effect = effect_create(effect, a);
    m = list_3(a, s_typeq, MK_PROC(Unit));
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

int_t test_eval() {
    XDEBUG(fprintf(stderr, "--test_eval--\n"));
    int_t cust;
    int_t expr;
    int_t env = NIL;  // FIXME: should be the "empty" environment
    int_t result;

    int_t effect = NIL;
    int_t s_foo = symbol("foo");
    cust = actor_create(MK_PROC(assert_beh), s_foo);
    effect = effect_create(effect, cust);
    //expr = list_2(s_quote, s_foo);  // (quote foo)
    expr = list_2(MK_ACTOR(&a_quote), s_foo);  // (<quote> foo)
    effect = effect_send(effect,
        actor_send(expr, list_3(cust, s_eval, env)));

    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    result = event_loop();
    XDEBUG(debug_print("test_eval event_loop", result));

    return OK;
}

int_t unit_tests() {
    if (test_values() != OK) return UNDEF;
    if (test_cells() != OK) return UNDEF;
    if (test_actors() != OK) return UNDEF;
    if (test_eval() != OK) return UNDEF;
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

    XDEBUG(hexdump("UNDEF", TO_PTR(UNDEF), 12));
    ASSERT(IS_ACTOR(UNDEF));

    ASSERT(UNIT != UNDEF);
    ASSERT(IS_ACTOR(UNIT));
    ASSERT(IS_PROC(get_code(UNIT)));

    fprintf(stderr, "   cell = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(cell), NAT(sizeof(cell)));
    fprintf(stderr, " intern = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(intern), NAT(sizeof(intern)));
    ASSERT((NAT(cell) & 0x7) == 0x0);

    fprintf(stderr, "s_quote = %"PRIxPTR"\n", s_quote);
    fprintf(stderr, "s_match = %"PRIxPTR"\n", s_match);
    ASSERT(IS_SYM(s_match));

#if 1
    result = unit_tests();
    XDEBUG(debug_print("result", result));
#endif

#if 0
    cell_t *p = TO_PTR(UNDEF);
    p->tail = NIL;  // FIXME: should not be able to assign to `const`
#endif

    return (result == OK ? 0 : 1);
}

int is_proc(int_t val) {
    return IS_ACTOR(val) && (TO_PTR(val) >= PTR(newline)) && (TO_PTR(val) <= PTR(main));
}
