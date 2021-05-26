/*
 * sexpr.c -- LISP/Scheme S-expressions (ala John McCarthy)
 */
#include "sexpr.h"
#include "serial.h"

#define DEBUG(x)   /* debug logging */
#define TRACE(x)   /* trace logging */

// static actors
extern ACTOR a_nil;
extern ACTOR a_true;
extern ACTOR a_false;
extern ACTOR a_inert;
extern ACTOR a_no_bind;
extern ACTOR a_empty_env;
extern ACTOR a_kernel_err;
extern ACTOR a_exit;

// static combiners
extern ACTOR ap_list;
extern ACTOR ap_boolean_p;
extern ACTOR ap_symbol_p;
extern ACTOR ap_inert_p;
extern ACTOR ap_pair_p;
extern ACTOR ap_null_p;
extern ACTOR ap_eq_p;
extern ACTOR op_define;
extern ACTOR op_vau;
extern ACTOR ap_wrap;
extern ACTOR ap_unwrap;
extern ACTOR op_sequence;
extern ACTOR op_lambda;
extern ACTOR ap_dump_bytes;
extern ACTOR ap_dump_words;
extern ACTOR ap_load_words;
extern ACTOR ap_store_words;
extern ACTOR ap_address_of;
extern ACTOR ap_sponsor_reserve;
extern ACTOR ap_sponsor_release;
extern ACTOR ap_sponsor_enqueue;

// static behaviors
extern ACTOR b_binding;
extern ACTOR b_scope;
extern ACTOR b_symbol;
extern ACTOR b_pair;
extern ACTOR b_number;
extern ACTOR b_appl;
extern ACTOR b_oper;

// asm utilities
extern void* reserve();  // allocate 32-byte block
extern void release(void* block);  // free reserved block
extern struct example_4 *create_4(ACTOR* behavior, u32 r4, u32 r5, u32 r6);
extern struct example_5 *create_5(ACTOR* behavior);

struct example_4 {
    u32         code_00;
    u32         r4_04;
    u32         r5_08;
    u32         r6_0c;
    u32         r7_10;
    u32         r8_14;
    u32         r9_18;
    ACTOR*      beh_1c;
};

struct example_5 {
    u32         code_00;
    u32         data_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
    ACTOR*      beh_1c;
};

int
object_p(ACTOR* x)
{
    // [FIXME] make this more robust...
//    return (((u32)x & 0x1f) == 0x0);  // 32-byte alignment
    return (((u32)x & 0x3) == 0x0);  // 32-bit alignment
}

int
boolean_p(ACTOR* x)
{
    return (x == &a_true) || (x == &a_false);
}

int
eq_p(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return (x == y);
}

int
equal_p(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return eq_p(x, y);
}

int
symbol_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_symbol);
}

int
inert_p(ACTOR* x)
{
    return (x == &a_inert);
}

int
ignore_p(ACTOR* x)
{
    return (x == &a_no_bind);
}

int
pair_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_pair);
}

int
null_p(ACTOR* x)
{
    return (x == &a_nil);
}

int
list_p(ACTOR* x)
{
    // FIXME: "list?" is supposed to fail on an improper list (dotted tail)
    // FIXME: Kernel defines "finite-list?" and "countable-list?"
    return null_p(x) || pair_p(x);
}

int
environment_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_scope) || (a->beh_1c == &b_binding);
}

int
number_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_number);
}

int
integer_p(ACTOR* x)
{
    return number_p(x);  // [FIXME] currently only int32 is supported
}

int
applicative_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_appl);  // FIXME: fails on "exit" (hard-coded)
}

int
operative_p(ACTOR* x)
{
    return object_p(x);  // FIXME: how do we check for operatives? some are hard-coded...
}

int
combiner_p(ACTOR* x)
{
    return applicative_p(x) || operative_p(x);
}

struct sym_24b {  // symbol data is offset 0x04 into a cache-line
    u32         data_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
};

static int
eq_24b(struct sym_24b* x, struct sym_24b* y)
{
    return (x->data_04 == y->data_04)
        && (x->data_08 == y->data_08)
        && (x->data_0c == y->data_0c)
        && (x->data_10 == y->data_10)
        && (x->data_14 == y->data_14)
        && (x->data_18 == y->data_18);
}

static struct example_5* sym_table[256];
static struct example_5** next_sym = sym_table;

ACTOR*
sym_search(struct sym_24b* name)  // search for interned symbol
{
    struct example_5** sp = sym_table;

    while (sp < next_sym) {
        if (eq_24b(name, (struct sym_24b*)&((*sp)->data_04))) {
            return (ACTOR*)(*sp);
        }
        ++sp;
    }
    return NULL;  // not found
}

ACTOR*
symbol(struct sym_24b* name)
{
    // search for interned symbol
    ACTOR* x = sym_search(name);
    if (x != NULL) {
        TRACE(puts("sym:found\n"));
        return x;  // symbol found, return it
    }
    if (((void *)(*next_sym) - (void *)(sym_table)) < sizeof(sym_table)) {
        TRACE(puts("sym:overflow\n"));
        return NULL;  // fail -- symbol table overflow!
    }
    // create a new symbol
    struct example_5 *a = create_5(&b_symbol);
    if (a) {
        a->data_04 = name->data_04;
        a->data_08 = name->data_08;
        a->data_0c = name->data_0c;
        a->data_10 = name->data_10;
        a->data_14 = name->data_14;
        a->data_18 = name->data_18;
        *next_sym++ = a;  // intern symbol in table
        TRACE(puts("sym:created\n"));
        return (ACTOR*)a;  // return new symbol
    }
    TRACE(puts("sym:fail\n"));
    return NULL;  // fail
}

ACTOR*
number(int n)
{
    // FIXME: consider a memo-table for small integers
    DEBUG(puts("number(0x"));
    DEBUG(serial_hex32((u32)n));
    DEBUG(puts(")="));
    struct example_5 *x = create_5(&b_number);
    x->data_04 = (u32)n;
    DEBUG(puts("0x"));
    DEBUG(serial_hex32((u32)x));
    DEBUG(putchar('\n'));
    return (ACTOR*)x;
}

ACTOR*
cons(ACTOR* a, ACTOR* d)
{
    DEBUG(puts("cons(0x"));
    DEBUG(serial_hex32((u32)a));
    DEBUG(puts(",0x"));
    DEBUG(serial_hex32((u32)d));
    DEBUG(puts(")="));
    struct example_5 *x = create_5(&b_pair);
    x->data_04 = (u32)a;
    x->data_08 = (u32)d;
    DEBUG(puts("0x"));
    DEBUG(serial_hex32((u32)x));
    DEBUG(putchar('\n'));
    return (ACTOR*)x;
}

ACTOR*
car(ACTOR* x)
{
    if (pair_p(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_04);
    }
    return NULL;  // fail
}

ACTOR*
cdr(ACTOR* x)
{
    if (pair_p(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_08);
    }
    return NULL;  // fail
}

ACTOR*
set_car(ACTOR* x, ACTOR* a)
{
    if (pair_p(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_04 = (u32)a;
        return x;
    }
    return NULL;  // fail
}

ACTOR*
set_cdr(ACTOR* x, ACTOR* d)
{
    if (pair_p(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_08 = (u32)d;
        return x;
    }
    return NULL;  // fail
}

u32
get_u32(ACTOR* num)
{
    struct example_5 *a = (struct example_5 *)num;
    return a->data_04;
}

ACTOR*
load_words(u32* addr, u32 count)
{
    ACTOR* list = &a_nil;

    while (count > 0) {
        int n = (int)(addr[--count]);
        list = cons(number(n), list);
    }
    return list;
}

void
store_words(u32* addr, ACTOR* list)
{
    while (!null_p(list)) {
        if (!pair_p(list)) return;  // abort
        struct example_5 *x = (struct example_5 *)list;
        *addr++ = get_u32((ACTOR*)(x->data_04));
        list = (ACTOR*)(x->data_08);
    }
}

typedef int (PRED)(ACTOR* x);

ACTOR*
apply_pred(PRED *p, ACTOR* list)  // apply unary predicate to a list
{
    while (!null_p(list)) {
        if (!pair_p(list)) {
            return NULL;  // fail!
        }
        if (!p(car(list))) {
            return &a_false;
        }
        list = cdr(list);
    }
    return &a_true;
}

typedef int (RLTN)(ACTOR* x, ACTOR* y);

ACTOR*
apply_rltn(RLTN *r, ACTOR* list)  // apply binary relation to a list
{
    if (null_p(list)) return &a_true;  // 0-arg base case
    if (!pair_p(list)) return NULL;  // fail: improper arg list
    ACTOR* witness = car(list);
    list = cdr(list);
    while (!null_p(list)) {
        if (!pair_p(list)) {
            return NULL;  // fail: improper arg list
        }
        ACTOR* element = car(list);
        if (!r(witness, element)) {
            return &a_false;
        }
        witness = element;
        list = cdr(list);
    }
    return &a_true;
}

ACTOR*  // returns the augmented environment, or NULL on failure
match_param_tree(ACTOR* def, ACTOR* arg, ACTOR* env)
{
    if (ignore_p(def)) return env;
    if (null_p(def)) {
        if (!null_p(arg)) return NULL;  // FAIL!
        return env;
    }
    if (symbol_p(def)) {
        struct example_5 *x = create_5(&b_binding);
        if (!x) return NULL;  // FAIL!
        x->data_04 = (u32)def;  // set symbol
        x->data_08 = (u32)arg;  // set value
        x->data_0c = (u32)env;  // set next
        return (ACTOR *)x;
    }
    if (pair_p(def)) {
        if (!pair_p(arg)) return NULL;  // FAIL!
        env = match_param_tree(car(def), car(arg), env);
        if (env) {
            env = match_param_tree(cdr(def), cdr(arg), env);
        }
        return env;
    }
    return NULL;  // FAIL!
}

/*
 * FIXME:
 *  `mutate_environment` can be more sophisticated than this implementation.
 *  It should check for errors such as duplicate bindings before mutation.
 *  It should modify existing bindings in-place, rather than prepending a new
 *  binding.
 */
u32
mutate_environment(u32 ext, u32 env)  // mutate env to include ext
{
    u32 x, y, z;
    struct example_5 *p;
    struct example_5 *q;
    struct example_5 *r;

    if (env == ext) return env;  // same environment, no mutation required
    x = ext;
    z = (u32)(&a_kernel_err);
    while (x != z) {
        p = (struct example_5 *)x;
        y = p->data_0c;
        if (y == env) {
            q = reserve();  // allocate block
            r = (struct example_5 *)env;
            *q = *r;  // copy original head
            p->data_0c = (u32)q;  // patch tail pointer
            p = (struct example_5 *)ext;
            *r = *p;  // copy extended head
            release(p);  // free extended head
            return env;
        }
        x = y;
    }
    return 0;  // FAIL!
}

static char* line = NULL;

void
flush_char()
{
    serial_in_flush();
    line = NULL;
}

static int
read_char()
{
    if (!line || !line[0]) {  // refill buffer
        line = editline();
        if (!line) return EOF;
    }
    int c = *line++;
    TRACE(serial_hex8((u8)c));
    TRACE(putchar(' '));
    TRACE(putchar(c < ' ' ? '.' : c));
    TRACE(putchar('\n'));
    return c;
}

static void
unread_char(int c)
{
    if (c > 0) {
        TRACE(putchar('<'));
        TRACE(serial_hex8((u8)c));
        TRACE(putchar(' '));
        TRACE(putchar(c < ' ' ? '.' : c));
        TRACE(putchar('\n'));
        *--line = c;
    }
}

void
parse_opt_space()
{
    int c;

    for (;;) {  // skip whitespace
        c = read_char();
        if (c == EOF) return;
        if (c == ';') {
            for (;;) {  // skip comment (to EOL)
                c = read_char();
                if ((c == '\n') || (c == '\r') || (c == EOF)) {
                    break;
                }
            }
        }
        if (c > ' ') {
            break;
        }
    }
    unread_char(c);
}

ACTOR*
parse_list()
{
    int c;
    ACTOR* x = &a_nil;
    ACTOR* y;
    ACTOR* z;

    TRACE(puts("list?\n"));
    c = read_char();
    if (c != '(') {
        unread_char(c);
        return NULL;  // failed
    }
    while ((y = parse_sexpr()) != NULL) {
        x = cons(y, x);  // build list in reverse
    }
    y = &a_nil;
    parse_opt_space();
    c = read_char();
    if ((c == '.') && (x != &a_nil)) {
        TRACE(puts("<tail>\n"));
        y = parse_sexpr();
        if (y == NULL) return NULL;  // failed
        parse_opt_space();
        c = read_char();
    }
    if (c != ')') {
        unread_char(c);
        return NULL;  // failed
    }
    while (x != &a_nil) {  // reverse list in-place
        z = cdr(x);
        y = set_cdr(x, y);
        x = z;
    }
    x = y;
    TRACE(puts("<list>\n"));
    return x;
}

static int
from_digit(int c, int r) {
    if ((c >= '0') && (c <= '9')) return (c - '0');
    if ((r == 16) && (c >= 'a') && (c <= 'f')) return (c - 'a') + 10;
    if ((r == 16) && (c >= 'A') && (c <= 'F')) return (c - 'A') + 10;
    return -1;  // fail
}

ACTOR*
parse_number()
{
    int r = 10;
    int s = 0;
    int n = 0;
    int c;
    int d;
    ACTOR* x;

    TRACE(puts("number?\n"));
    c = read_char();
    if (c == '#') {
        c = read_char();
        if (c == 'x') {
            r = 16;
        } else {
            unread_char(c);
            unread_char('#');
            return NULL;  // failed
        }
        c = read_char();
    }
    if (c == '-') {  // optional sign
        s = 1;
        c = read_char();
    } else if (c == '+') {
        c = read_char();
    }
    d = from_digit(c, r);
    if (d < 0) {
        unread_char(c);
        return NULL;  // failed
    }
    n = d;
    for (;;) {
        c = read_char();
        while (c == '_') {  // allow '_' separators
            c = read_char();
        }
        d = from_digit(c, r);
        if (d < 0) {
            unread_char(c);
            break;
        }
        n = (r * n) + d;
    }
    if (s) {
        n = -n;
    }
    x = number(n);
    TRACE(puts("<number>\n"));
    return x;
}

static int
is_ident_char(int c)
{
    return (((c >= 'a') && (c <= 'z'))
        ||  ((c >= 'A') && (c <= 'Z'))
        ||  (c == '!')
        ||  ((c >= '#') && (c <= '&'))
        ||  (c == '*')
        ||  (c == '+')
        ||  (c == '-')
        ||  (c == '.')
        ||  (c == '/')
        ||  ((c >= '0') && (c <= '9'))
        ||  (c == ':')
        ||  ((c >= '<') && (c <= '@'))
        ||  (c == '^')
        ||  (c == '_')
        ||  (c == '~'));
}

static char inert_24b[] =
	"#ine" "rt\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char t_24b[] =
	"#t\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char f_24b[] =
	"#f\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char ignore_24b[] =
	"#ign" "ore\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";

ACTOR*
parse_symbol()
{
    struct sym_24b sym = { 0, 0, 0, 0, 0, 0 };
    char *b = (char *)(&sym);
    int i = 0;
    int c;
    ACTOR* x;

    TRACE(puts("symbol?\n"));
    c = read_char();
    if (!is_ident_char(c)) {
        unread_char(c);
        return NULL;  // failed
    }
    if (c == '.') {  // check for for delimited '.'
        c = read_char();
        if (!is_ident_char(c)) {
            unread_char(c);
            unread_char('.');
            TRACE(puts("<dot>\n"));
            return NULL;  // failed
        }
        b[i++] = '.';
    }
    for (;;) {
        b[i++] = c;
        if (i >= sizeof(sym)) return NULL;  // overflow
        c = read_char();
        if (!is_ident_char(c)) {
            unread_char(c);
            break;
        }
    }
    b[i] = '\0';  // NUL termination
    if (b[0] == '#') {
        if (eq_24b(&sym, (struct sym_24b*)inert_24b)) x = &a_inert;
        else if (eq_24b(&sym, (struct sym_24b*)t_24b)) x = &a_true;
        else if (eq_24b(&sym, (struct sym_24b*)f_24b)) x = &a_false;
        else if (eq_24b(&sym, (struct sym_24b*)ignore_24b)) x = &a_no_bind;
        else return NULL;  // failed
    } else {
        x = symbol(&sym);
    }
    TRACE(puts("<symbol>\n"));
    return x;
}

ACTOR*
parse_atom()
{
    ACTOR* x;

    x = parse_number();
    if (x) return x;  // matched
    x = parse_symbol();
    return x;
}

ACTOR*
parse_sexpr()  /* parse and return s-expression */
{
    ACTOR* x;

    parse_opt_space();
    x = parse_list();
    if (x) return x;  // matched
    x = parse_atom();
    return x;
}

#if 0
#define ONE_OF(c,s) (((c) && strchr((s),(c))) ? 1 : 0)

ACTOR
parse_sexpr()  /* parse and return s-expression */
{
    static char* delim = "\"();'`,[]{}|";

...
    } else if (c == '\'') {
        (src->next)(src);
        c = MK_INT((src->get)(src));
        if (c == '\\') {
            (src->next)(src);
            c = MK_INT((src->get)(src));
            switch(c) {
                case '\\':
                case '\'':
                case '\"':  /* escaped literal */   break;
                case 'r':   c = '\r';               break;
                case 'n':   c = '\n';               break;
                case 't':   c = '\t';               break;
                case 'b':   c = '\b';               break;
                default:    c = EOF;                break;
            }
        } else if (c == '\'') {
            c = EOF;
        }
        x = get_number(NUMBER(c));
        if (c != EOF) {
            (src->next)(src);
            c = MK_INT((src->get)(src));
        }
        if (c == '\'') {
            (src->next)(src);
        } else {
            x = NUMBER(c);  /* malformed character literal */
        }
    } else if (c == '"') {
        x = NUMBER(c);  /* FIXME: implement string literals */
    } else if (ispunct(c) && ONE_OF(c, delim)) {
        x = NUMBER(c);  /* illegal lexeme */
...
#endif

static void
print_number(ACTOR* n)
{
    char dec[12];
    char *p = dec + sizeof(dec);
    int i;
    int s;

    struct example_5 *a = (struct example_5 *)n;
    i = (int)(a->data_04);
    s = (i < 0);
    if (s) i = -i;  // make positive
    *--p = '\0';
    do {
        *--p = (char)((i % 10) + '0');
        i /= 10;
    } while (i && (p > dec));
    if (s) {
        putchar('-');
    }
    puts(p);
}

static void
print_symbol(ACTOR* s)
{
    struct example_5 *a = (struct example_5 *)s;
    puts((char*)(&a->data_04));
}

static void
print_list(ACTOR* x)
{
    putchar('(');
    for (;;) {
        struct example_5 *z = (struct example_5 *)x;
        ACTOR* a = (ACTOR*)(z->data_04);
        ACTOR* d = (ACTOR*)(z->data_08);

        print_sexpr(a);
        if (null_p(d)) {
            putchar(')');
            break;
        } else if (!pair_p(d)) {
            puts(" . ");
            print_sexpr(d);
            putchar(')');
            break;
        }
        putchar(' ');
        x = d;
    }
}

void
print_sexpr(ACTOR* a)  /* print external representation of s-expression */
{
    if (!a) {  // null pointer
        puts("#<NULL>");
    } else if ((u32)a & 0x3) {  // misaligned address
        putchar('#');
        serial_hex32((u32)a);
        putchar('?');
    } else if (null_p(a)) {
        puts("()");
    } else if (a == &a_true) {
        puts("#t");
    } else if (a == &a_false) {
        puts("#f");
    } else if (inert_p(a)) {
        puts("#inert");
    } else if (a == &a_no_bind) {
        puts("#ignore");
    } else if ((u32)a & 0x1f) {  // misaligned address
        putchar('#');
        serial_hex32((u32)a);
        putchar('?');
    } else if (number_p(a)) {
        print_number(a);
    } else if (symbol_p(a)) {
        print_symbol(a);
    } else if (pair_p(a)) {
        print_list(a);
    } else {  // unrecognized value
        putchar('#');
        putchar('<');
        serial_hex32((u32)a);
        putchar('>');
    }
}

//static u8 the_margin[128];
static ACTOR* kernel_env = NULL;

static ACTOR*
extend_env(ACTOR* env, struct sym_24b* sym, u32 value)
{
    ACTOR* a = symbol((struct sym_24b*)sym);
    if (!a) return NULL;  // FAIL!
    struct example_5 *x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)value;  // set value
    x->data_0c = (u32)env;  // set next
    return (ACTOR *)x;
}

ACTOR*
ground_env()
{
    char exit_24b[] = "exit" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_reserve_24b[] = "spon" "sor-" "rese" "rve\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_release_24b[] = "spon" "sor-" "rele" "ase\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_enqueue_24b[] = "spon" "sor-" "enqu" "eue\0" "\0\0\0\0" "\0\0\0\0";
    char dump_bytes_24b[] = "dump" "-byt" "es\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char dump_words_24b[] = "dump" "-wor" "ds\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char load_words_24b[] = "load" "-wor" "ds\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char store_words_24b[] = "stor" "e-wo" "rds\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char address_of_24b[] = "addr" "ess-" "of\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char wrap_24b[] = "wrap" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char unwrap_24b[] = "unwr" "ap\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char osequence_24b[] = "$seq" "uenc" "e\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char ovau_24b[] = "$vau" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char olambda_24b[] = "$lam" "bda\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char odefinem_24b[] = "$def" "ine!" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char eqp_24b[] = "eq?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char symbolp_24b[] = "symb" "ol?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char inertp_24b[] = "iner" "t?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char booleanp_24b[] = "bool" "ean?" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char nullp_24b[] = "null" "?\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char pairp_24b[] = "pair" "?\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char list_24b[] = "list" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    ACTOR* a;
    struct example_5 *x;

    if (kernel_env) return kernel_env;  // lazy-initialized singleton

    ACTOR *env = &a_empty_env;  // signal an error on lookup failure

    /* bind "exit" */
    a = extend_env(env, (struct sym_24b*)exit_24b, (u32)&a_exit);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-reserve" */
    a = extend_env(env, (struct sym_24b*)sponsor_reserve_24b, (u32)&ap_sponsor_reserve);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-release" */
    a = extend_env(env, (struct sym_24b*)sponsor_release_24b, (u32)&ap_sponsor_release);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-enqueue" */
    a = extend_env(env, (struct sym_24b*)sponsor_enqueue_24b, (u32)&ap_sponsor_enqueue);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "dump-bytes" */
    a = extend_env(env, (struct sym_24b*)dump_bytes_24b, (u32)&ap_dump_bytes);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "dump-words" */
    a = extend_env(env, (struct sym_24b*)dump_words_24b, (u32)&ap_dump_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "load-words" */
    a = extend_env(env, (struct sym_24b*)load_words_24b, (u32)&ap_load_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "store-words" */
    a = extend_env(env, (struct sym_24b*)store_words_24b, (u32)&ap_store_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "address-of" */
    a = extend_env(env, (struct sym_24b*)address_of_24b, (u32)&ap_address_of);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "wrap" */
    a = extend_env(env, (struct sym_24b*)wrap_24b, (u32)&ap_wrap);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "unwrap" */
    a = extend_env(env, (struct sym_24b*)unwrap_24b, (u32)&ap_unwrap);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$sequence" */
    a = extend_env(env, (struct sym_24b*)osequence_24b, (u32)&op_sequence);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$vau" */
    a = extend_env(env, (struct sym_24b*)ovau_24b, (u32)&op_vau);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$lambda" */
    a = extend_env(env, (struct sym_24b*)olambda_24b, (u32)&op_lambda);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$define!" */
    a = extend_env(env, (struct sym_24b*)odefinem_24b, (u32)&op_define);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "eq?" */
    a = extend_env(env, (struct sym_24b*)eqp_24b, (u32)&ap_eq_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "symbol?" */
    a = extend_env(env, (struct sym_24b*)symbolp_24b, (u32)&ap_symbol_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "inert?" */
    a = extend_env(env, (struct sym_24b*)inertp_24b, (u32)&ap_inert_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "boolean?" */
    a = extend_env(env, (struct sym_24b*)booleanp_24b, (u32)&ap_boolean_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "null?" */
    a = extend_env(env, (struct sym_24b*)nullp_24b, (u32)&ap_null_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "pair?" */
    a = extend_env(env, (struct sym_24b*)pairp_24b, (u32)&ap_pair_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "list" */
    a = extend_env(env, (struct sym_24b*)list_24b, (u32)&ap_list);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* mutable local scope */
    x = create_5(&b_scope);
    if (!x) return NULL;  // FAIL!
    x->data_0c = (u32)env;  // set parent
    env = (ACTOR *)x;

    /* establish ground environment */
    kernel_env = env;
    TRACE(puts("ground_env=0x"));
    TRACE(serial_hex32((u32)env));
    TRACE(putchar('\n'););

    /* provide additional definitions in source form */
    line =
"($define! car ($lambda ((x . #ignore)) x))\n"
"($define! cdr ($lambda ((#ignore . x)) x))\n"
//"($define! get-current-environment (wrap ($vau () e e)))\n"
"($define! get-current-env (wrap ($vau () e e)))\n"
"\n";

    return kernel_env;
}

void
kernel_repl()
{
    flush_char();
    for (;;) {
        putchar('\n');
        puts("> ");
        ACTOR* x = parse_sexpr();
        if (x == NULL) break;
        // FIXME: this is just a read-print loop (no eval)
        print_sexpr(x);
        putchar('\n');
    }
}
