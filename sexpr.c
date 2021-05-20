/*
 * sexpr.c -- LISP/Scheme S-expressions (ala John McCarthy)
 */
#include "sexpr.h"
#include "serial.h"

#define DEBUG(x)   /* debug logging */

// asm utilities
extern struct example_1 *create_0(ACTOR* behavior);
extern struct template_1 *create_1(ACTOR* behavior, u32 r4);
extern struct template_2 *create_2(ACTOR* behavior, u32 r4, u32 r5);
extern struct template_3 *create_3(ACTOR* behavior, u32 r4, u32 r5, u32 r6);
extern struct example_4 *create_4(ACTOR* behavior, u32 r4, u32 r5, u32 r6);
extern struct example_5 *create_5(ACTOR* behavior);

// static actors
extern ACTOR a_nil;
extern ACTOR a_true;
extern ACTOR a_false;
extern ACTOR a_inert;
extern ACTOR a_no_bind;
extern ACTOR a_kernel_err;
extern ACTOR ap_list;
extern ACTOR a_exit;

// static behaviors
extern ACTOR b_binding;
extern ACTOR b_scope;
extern ACTOR b_symbol;
extern ACTOR b_pair;
extern ACTOR b_number;

struct example_1 {
    u32         code_00;
    ACTOR*      beh_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
    u32         data_1c;
};

struct template_1 {
    u32         code_00;
    u32         code_04;
    u32         r4_08;
    ACTOR*      beh_0c;
    u32         _10;
    u32         _14;
    u32         _18;
    u32         _1c;
};

struct template_2 {
    u32         code_00;
    u32         code_04;
    u32         r4_08;
    u32         r5_0c;
    ACTOR*      beh_10;
    u32         _14;
    u32         _18;
    u32         _1c;
};

struct template_3 {
    u32         code_00;
    u32         code_04;
    u32         r4_08;
    u32         r5_0c;
    u32         r6_10;
    ACTOR*      beh_14;
    u32         _18;
    u32         _1c;
};

struct example_3 {
    u32         code_00;
    u32         code_04;
    u32         r4_08;
    u32         r5_0c;
    u32         r6_10;
    u32         r7_14;
    u32         r8_18;
    ACTOR*      beh_1c;
};

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
boolean_q(ACTOR* x)
{
    return (x == &a_true) || (x == &a_false);
}

int
eq_q(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return (x == y);
}

int
equal_q(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return eq_q(x, y);
}

int
symbol_q(ACTOR* x)
{
    struct example_1 *a = (struct example_1 *)x;
    return (a->beh_04 == &b_symbol);
}

int
inert_q(ACTOR* x)
{
    return (x == &a_inert);
}

int
pair_q(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_pair);
}

int
null_q(ACTOR* x)
{
    return (x == &a_nil);
}

int
environment_q(ACTOR* x)
{
    struct template_3 *a = (struct template_3 *)x;
    return (a->beh_14 == &b_scope) || (a->beh_14 == &b_binding);
}

int
ignore_q(ACTOR* x)
{
    return (x == &a_inert);
}

int
number_q(ACTOR* x)
{
    struct template_1 *a = (struct template_1 *)x;
    return (a->beh_0c == &b_number);
}

int
integer_q(ACTOR* x)
{
    return number_q(x);  // [FIXME] currently only int32 is supported
}

struct sym_24b {  // symbol data is offset 0x08 into a cache-line
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
    u32         data_1c;
};

static int
eq_24b(struct sym_24b* x, struct sym_24b* y)
{
    return (x->data_08 == y->data_08)
        && (x->data_0c == y->data_0c)
        && (x->data_10 == y->data_10)
        && (x->data_14 == y->data_14)
        && (x->data_18 == y->data_18)
        && (x->data_1c == y->data_1c);
}

static struct example_1* sym_table[256];
static struct example_1** next_sym = sym_table;

ACTOR*
sym_search(struct sym_24b* name)  // search for interned symbol
{
    struct example_1** sp = sym_table;

    while (sp < next_sym) {
        if (eq_24b(name, (struct sym_24b*)&((*sp)->data_08))) {
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
        DEBUG(puts("sym:found\n"));
        return x;  // symbol found, return it
    }
    if (((void *)(*next_sym) - (void *)(sym_table)) < sizeof(sym_table)) {
        DEBUG(puts("sym:overflow\n"));
        return NULL;  // fail -- symbol table overflow!
    }
    // create a new symbol
    struct example_1 *a = create_0(&b_symbol);
    if (a) {
        a->data_08 = name->data_08;
        a->data_0c = name->data_0c;
        a->data_10 = name->data_10;
        a->data_14 = name->data_14;
        a->data_18 = name->data_18;
        a->data_1c = name->data_1c;
        *next_sym++ = a;  // intern symbol in table
        DEBUG(puts("sym:created\n"));
        return (ACTOR*)a;  // return new symbol
    }
    DEBUG(puts("sym:fail\n"));
    return NULL;  // fail
}

ACTOR*
number(int n)
{
    struct template_1 *a = create_1(&b_number, (u32)n);
    return (ACTOR*)a;
}

ACTOR*
cons(ACTOR* a, ACTOR* d)
{
    struct example_5 *x = create_5(&b_pair);
    x->data_04 = (u32)a;
    x->data_08 = (u32)d;
    return (ACTOR*)x;
}

ACTOR*
car(ACTOR* x)
{
    if (pair_q(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_04);
    }
    return NULL;  // fail
}

ACTOR*
cdr(ACTOR* x)
{
    if (pair_q(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_08);
    }
    return NULL;  // fail
}

ACTOR*
set_car(ACTOR* x, ACTOR* a)
{
    if (pair_q(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_04 = (u32)a;
        return x;
    }
    return NULL;  // fail
}

ACTOR*
set_cdr(ACTOR* x, ACTOR* d)
{
    if (pair_q(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_08 = (u32)d;
        return x;
    }
    return NULL;  // fail
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
    DEBUG(serial_hex8((u8)c));
    DEBUG(putchar(' '));
    DEBUG(putchar(c < ' ' ? '.' : c));
    DEBUG(putchar('\n'));
    return c;
}

static void
unread_char(int c)
{
    if (c > 0) {
        DEBUG(putchar('<'));
        DEBUG(serial_hex8((u8)c));
        DEBUG(putchar(' '));
        DEBUG(putchar(c < ' ' ? '.' : c));
        DEBUG(putchar('\n'));
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

    DEBUG(puts("list?\n"));
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
        DEBUG(puts("<tail>\n"));
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
    DEBUG(puts("<list>\n"));
    return x;
}

ACTOR*
parse_number()
{
    int s = 0;
    int n = 0;
    int c;
    ACTOR* x;

    DEBUG(puts("number?\n"));
    c = read_char();
    if (c == '-') {  // optional sign
        s = 1;
        c = read_char();
    } else if (c == '+') {
        c = read_char();
    }
    if ((c < '0') || (c > '9')) {
        unread_char(c);
        return NULL;  // failed
    }
    n = (c - '0');
    for (;;) {
        c = read_char();
        while (c == '_') {  // allow '_' separators
            c = read_char();
        }
        if ((c < '0') || (c > '9')) {
            unread_char(c);
            break;
        }
        n = (10 * n) + (c - '0');
    }
    if (s) {
        n = -n;
    }
    x = number(n);
    DEBUG(puts("<number>\n"));
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

    DEBUG(puts("symbol?\n"));
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
            DEBUG(puts("<dot>\n"));
            return NULL;  // failed
        }
        b[i++] = '.';
    }
    for (;;) {
        b[i++] = c;
        if (i >= (sizeof(sym) - 1)) return NULL;  // overflow
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
    DEBUG(puts("<symbol>\n"));
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
print_number(ACTOR* num)
{
    char dec[12];
    char *p = dec + sizeof(dec);
    int n;
    int s;

    struct template_1 *a = (struct template_1 *)num;
    n = (int)(a->r4_08);
    s = (n < 0);
    if (s) n = -n;  // make positive
    *--p = '\0';
    do {
        *--p = (char)((n % 10) + '0');
        n /= 10;
    } while (n && (p > dec));
    if (s) {
        putchar('-');
    }
    puts(p);
}

static void
print_symbol(ACTOR* sym)
{
    struct example_1 *a = (struct example_1 *)sym;
    puts((char*)(&a->data_08));
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
        if (null_q(d)) {
            putchar(')');
            break;
        } else if (!pair_q(d)) {
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
    if (!a) {
        puts("#<NULL>");
    } else if (null_q(a)) {
        puts("()");
    } else if (a == &a_true) {
        puts("#t");
    } else if (a == &a_false) {
        puts("#f");
    } else if (inert_q(a)) {
        puts("#inert");
    } else if (a == &a_no_bind) {
        puts("#ignore");
    } else if (number_q(a)) {
        print_number(a);
    } else if (symbol_q(a)) {
        print_symbol(a);
    } else if (pair_q(a)) {
        print_list(a);
    } else {
        putchar('#');
        putchar('<');
        serial_hex32((u32)a);
        putchar('>');
    }
}

ACTOR*
ground_env()
{
    static ACTOR* env = NULL;
    char exit_24b[] = "exit" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char list_24b[] = "list" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    ACTOR* a;
    ACTOR* x;

    if (env) return env;  // lazy-initialized singleton
    env = &a_kernel_err;  // signal an error on lookup failure

    /* bind "exit" */
    a = symbol((struct sym_24b*)exit_24b);
    if (!a) return NULL;  // FAIL!
    x = (ACTOR *)create_3(&b_binding, (u32)a, (u32)&a_exit, (u32)env);
    if (!x) return NULL;  // FAIL!
    env = x;

    /* bind "list" */
    a = symbol((struct sym_24b*)list_24b);
    if (!a) return NULL;  // FAIL!
    x = (ACTOR *)create_3(&b_binding, (u32)a, (u32)&ap_list, (u32)env);
    if (!x) return NULL;  // FAIL!
    env = x;

    /* mutable local scope */
    x = (ACTOR *)create_3(&b_scope, (u32)env, (u32)0, (u32)0);
    if (!x) return NULL;  // FAIL!
    env = x;

    return env;
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
        print_sexpr(x);
        putchar('\n');
    }
}
