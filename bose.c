/*
 * bose.c -- Binary Octet-Stream Encoding
 *
 * Copyright 2019-2021 Dale Schumacher
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "bose.h"

#define HEXDUMP_ANNOTATION  0  // dump bose-encoded bytes for collection values
#define ANSI_COLOR_OUTPUT   0  // use ansi terminal escape sequences to set output colors

#define DEBUG(x) x /* debug logging */
#define TRACE(x)   /* trace logging */

static void serial_int32(int n) {
    if (n < 0) {
        putchar('-');
        n = -n;
    }
    serial_dec32(n);
}

static void
print(u32 unicode) {
    if ((unicode == '\t')
    ||  (unicode == '\n')
    ||  ((unicode >= 0x20) && (unicode < 0x7F))) {
        putchar(unicode);
    } else if (unicode >= 0xA0) {
        putchar('~');  // replacement character
    }
}

static void
prints(char * s) {
    u32 c;

    while (s && (c = (u32)(*s++))) {
        print(c);
    }
}

static void
newline() {
    putchar('\n');
}

static void
space(int indent) {  // space between values
    if (indent > 0) {
        newline();
        while (--indent > 0) {
            prints("  ");  // indent 2 spaces
        }
    } else {
        print(' ');
    }
}

typedef enum {
    Black = '0',    // ^[[30m
    Red,            // ^[[31m
    Green,          // ^[[32m
    Yellow,         // ^[[33m
    Blue,           // ^[[34m
    Magenta,        // ^[[35m
    Cyan,           // ^[[36m
    White           // ^[[37m
} color_t;

#if ANSI_COLOR_OUTPUT

#define NUM_COLOR   Green       // number
#define TEXT_COLOR  Yellow      // string
#define MEMO_COLOR  Red         // memo marker
#define PRIM_COLOR  Magenta     // primitive/capability
#define PUNCT_COLOR Cyan        // punctuation
#define DUMP_COLOR  Blue        // hexdump

#define ESC 0x1B

static void
set_color(color_t color) {
    putchar(ESC);
    putchar('[');
    putchar('3');
    putchar(color);
    putchar('m');
}

static void
clear_color() {
    putchar(ESC);
    putchar('[');
    putchar('m');
}

#else

#define set_color(c) /* REMOVED */
#define clear_color() /* REMOVED */

#endif

static int
decode_integer(int* result, u8** data_ref)
{
    u8* data = *data_ref;
    u8 b = *data++;
    TRACE(puts("decode_integer: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    int n = SMOL2INT(b);
    if ((n >= SMOL_MIN) && (n <= SMOL_MAX)) {
        *result = n;
        *data_ref = data;
        TRACE(puts("decode_integer: smol="));
        TRACE(serial_int32(n));
        TRACE(putchar('\n'));
        return true;  // success
    }
    int size = 0;
    if (decode_integer(&size, &data)) {
        u8* end = data + size;
        if (((b & 0xF0) == 0x10)  // Integer type?
        &&  (size <= sizeof(int))) {  // not too large?
            //int pad = b & 0x7;
            //int sign = (b & 0x8) ? -1 : 1;
            n = (b & 0x8) ? -1 : 0;  // sign extend
            while (size > 0) {
                n = (n << 8) | data[--size];
            }
            *result = n;
            *data_ref = end;
            TRACE(puts("decode_integer: int="));
            TRACE(serial_int32(n));
            TRACE(putchar('\n'));
            return true;  // success
        }
        *data_ref = end;  // skip value
    } else {
        *data_ref = data;  // update data reference
    }
    TRACE(puts("decode_integer: fail!\n"));
    return false;  // failure!
}

static int
print_number(u8** data_ref)
{
    int ok = true;
    u8* data = *data_ref;
    int n = 0;
    if (decode_integer(&n, &data)) {
        set_color(NUM_COLOR);
        serial_int32(n);
        clear_color();
    } else {
        set_color(NUM_COLOR);
        prints("<bad number>");
        clear_color();
        ok = false;  // fail!
    }
    *data_ref = data;  // update data reference
    return ok;
}

static int
print_string(u8** data_ref)
{
    int ok = true;
    u8* data = *data_ref;
    u8 b = *data++;
    TRACE(puts("print_string: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    if (b == string_0) {
        set_color(TEXT_COLOR);
        print('"');
        print('"');
        clear_color();
    } else if (b == mem_ref) {
        b = *data++;
        set_color(TEXT_COLOR);
        prints("<no memo>");
        clear_color();
        ok = false;  // fail!
    } else {
        int size = 0;
        if (decode_integer(&size, &data)) {
            u8* end = data + size;
            if ((b == utf8_mem) || (b == utf16_mem)) {
                set_color(TEXT_COLOR);
                prints("<no memo>");
                clear_color();
                data += size;
                ok = false;  // fail!
            } else if (b == octets) {
                set_color(TEXT_COLOR);
                print('"');
                while (data < end) {
                    b = *data++;
                    print(b);
                }
                print('"');
                clear_color();
            } else if (b == utf8) {
                set_color(TEXT_COLOR);
                print('"');
                while (data < end) {
                    b = *data++;
                    print(b);  // FIXME: handle utf8 encoding
                }
                print('"');
                clear_color();
            } else if (b == utf16) {
                set_color(TEXT_COLOR);
                print('"');
                while (data < end) {
                    u32 u = *data++;
                    b = *data++;
                    u = (u << 8) | b;
                    print(u);  // FIXME: handle utf16-be/le encoding
                }
                print('"');
                clear_color();
            } else {
                set_color(TEXT_COLOR);
                prints("<bad encoding>");
                clear_color();
                data += size;
                ok = false;  // fail!
            }
        } else {
            set_color(TEXT_COLOR);
            prints("<bad string size>");
            clear_color();
            ok = false;  // fail!
        }
    }
    *data_ref = data;  // update data reference
    return ok;
}

static int
print_array(u8** data_ref, int indent, int limit)
{
    int ok = true;
    u8* data = *data_ref;
    u8 b = *data++;
    TRACE(puts("print_array: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    set_color(PUNCT_COLOR);
    print('[');
    if (b == array_0) {
        print(']');
        clear_color();
    } else {
        int size = 0;
        if (decode_integer(&size, &data)) {
            u8* end = data + size;
            if (b == array_n) {
                int count = 0;
                if (!decode_integer(&count, &data)) {
                    set_color(PUNCT_COLOR);
                    prints("<bad element count>");
                    clear_color();
                    *data_ref = end;  // skip value
                    return false;  // failure!
                }
            }
            if (limit < 1) {
                prints("...]");
                clear_color();
                *data_ref = end;  // skip value
                return true;  // success
            }
            if (indent) {
                space(++indent);
            }
            int once = true;
            while (data < end) {
                if (once) {
                    once = false;
                } else {
                    set_color(PUNCT_COLOR);
                    print(',');
                    space(indent);
                    clear_color();
                }
                if (!print_bose(&data, indent, limit - 1)) {  // element
                    set_color(PUNCT_COLOR);
                    prints("<bad element>");
                    clear_color();
                    ok = false;  // fail!
                    break;
                }
            }
            if (indent) {
                space(--indent);
            }
            set_color(PUNCT_COLOR);
            print(']');
            clear_color();
        } else {
            set_color(PUNCT_COLOR);
            prints("<bad array size>");
            clear_color();
            ok = false;  // fail!
        }
    }
    clear_color();
    *data_ref = data;  // update data reference
    return ok;
}

static int
print_object(u8** data_ref, int indent, int limit)
{
    int ok = true;
    u8* data = *data_ref;
    u8 b = *data++;
    TRACE(puts("print_object: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    set_color(PUNCT_COLOR);
    print('{');
    if (b == object_0) {
        print('}');
        clear_color();
    } else {
        int size = 0;
        if (decode_integer(&size, &data)) {
            u8* end = data + size;
            if (b == object_n) {
                int count = 0;
                if (!decode_integer(&count, &data)) {
                    set_color(PUNCT_COLOR);
                    prints("<bad property count>");
                    clear_color();
                    *data_ref = end;  // skip value
                    return false;  // failure!
                }
            }
            if (limit < 1) {
                prints("...}");
                clear_color();
                *data_ref = end;  // skip value
                return true;  // success
            }
            if (indent) {
                space(++indent);
            }
            int once = true;
            while (data < end) {
                if (once) {
                    once = false;
                } else {
                    set_color(PUNCT_COLOR);
                    print(',');
                    space(indent);
                    clear_color();
                }
                if (!print_string(&data)) {  // property name
                    set_color(PUNCT_COLOR);
                    prints("<bad property name>");
                    clear_color();
                    ok = false;  // fail!
                    break;
                }
                set_color(PUNCT_COLOR);
                print(':');
                if (indent) {
                    print(' ');
                }
                clear_color();
                if (!print_bose(&data, indent, limit - 1)) {  // property value
                    set_color(PUNCT_COLOR);
                    prints("<bad property value>");
                    clear_color();
                    ok = false;  // fail!
                    break;
                }
            }
            if (indent) {
                space(--indent);
            }
            set_color(PUNCT_COLOR);
            print('}');
            clear_color();
        } else {
            set_color(PUNCT_COLOR);
            prints("<bad object size>");
            clear_color();
            ok = false;  // fail!
        }
    }
    *data_ref = data;  // update data reference
    return ok;
}

// print an arbitrary BOSE-encoded value
int
print_bose(u8** data_ref, int indent, int limit)
{
    int ok = true;
    u8* data = *data_ref;
    u8 b = *data;
    TRACE(puts("print_bose: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    if (b == null) {
        set_color(PRIM_COLOR);
        prints("null");
        clear_color();
        ++data;
    } else if (b == true) {
        set_color(PRIM_COLOR);
        prints("true");
        clear_color();
        ++data;
    } else if (b == false) {
        set_color(PRIM_COLOR);
        prints("false");
        clear_color();
        ++data;
    } else if ((b & 0xF8) == 0x08) {  // String type (2#0000_1xxx)
        ok = print_string(&data);
    } else if ((b & 0xF9) == 0x00) {  // Array type (2#0000_0xx0) != false
        ok = print_array(&data, indent, limit);
    } else if ((b & 0xF9) == 0x01) {  // Object type (2#0000_0xx1) != true
        ok = print_object(&data, indent, limit);
    } else {
        ok = print_number(&data);
    }
    *data_ref = data;  // update data reference
    return ok;
}

/*
 * composite data structures
 */

ACTOR*
new_array()  // allocate a new (empty) array
{
    struct example_5* x = (struct example_5*)reserve();
    struct example_5* y = (struct example_5*)(&v_array_0);
    *x = *y;  // copy empty array template
    return (ACTOR*)x;
}

inline u32
array_element_count(ACTOR *a)
{
    struct example_5* x = (struct example_5*)a;
    u32 count = (x->data_08 >> 2);
    return count;
}

ACTOR*
array_element(ACTOR* a, u32 index)  // retrieve element at (0-based) index
{
    struct example_5* x = (struct example_5*)a;
    u32 count = (x->data_08 >> 2);
    if (index < count) {
        if (index < 3) {
            u32* w = &x->data_0c;
            return (ACTOR*)(w[index]);
        } else {
            index -= 3;
            x = (struct example_5*)(x->data_18);
            while (x) {
                if (index < 7) {
                    u32* w = (u32*)x;
                    return (ACTOR*)(w[index]);
                }
                index -= 7;
                x = (struct example_5*)(x->beh_1c);
            }
        }
    }
    return NULL;  // fail!
}

ACTOR*
array_insert(ACTOR* a, u32 index, ACTOR* element)  // insert element at (0-based) index
{
    ACTOR* b = NULL;

    struct example_5* x = (struct example_5*)a;
    u32 count = (x->data_08 >> 2);
    if (index <= count) {
        b = (ACTOR*)reserve();
        struct example_5* y = (struct example_5*)b;
        y->code_00 = x->code_00;  // copy code field
        y->data_04 = x->data_04;  // copy array header
        y->data_08 = x->data_08 + 4;  // increase size
        y->data_18 = 0;  // NULL next/link pointer
        y->beh_1c = x->beh_1c;  // copy actor behavior
        u32 i = 0;  // index of pointer to copy
        u32 n = 3;  // number of pointers in block
        u32* w = &x->data_0c;  // src pointer
        u32* v = &y->data_0c;  // dst pointer
        // copy pointers before index
        while (i < index) {
            if (n == 0) {  // next block
                x = (struct example_5*)(*w);
                w = (u32*)x;
                y = (struct example_5*)reserve();
                if (!y) return NULL;  // fail!
                y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
                *v = (u32)y;
                v = (u32*)y;
                n = 7;
            }
            *v++ = *w++;  // copy pointer
            --n;
            ++i;
        }
        // insert element at index
        if (n == 0) {  // next block
            x = (struct example_5*)(*w);
            w = (u32*)x;
            y = (struct example_5*)reserve();
            if (!y) return NULL;  // fail!
            y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
            *v = (u32)y;
            v = (u32*)y;
            n = 7;
        }
        *v++ = (u32)element;
        --n;
        ++i;
        // copy pointers after index
        while (i <= count) {
            if (n == 0) {  // next block
                x = (struct example_5*)(*w);
                w = (u32*)x;
                y = (struct example_5*)reserve();
                if (!y) return NULL;  // fail!
                y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
                *v = (u32)y;
                v = (u32*)y;
                n = 7;
            }
            *v++ = *w++;  // copy pointer
            --n;
            ++i;
        }
    }
    return b;
}

ACTOR*
new_object()  // allocate a new (empty) object
{
    struct example_5* x = (struct example_5*)reserve();
    struct example_5* y = (struct example_5*)(&v_object_0);
    *x = *y;  // copy empty object template
    return (ACTOR*)x;
}

inline u32
object_property_count(ACTOR *a)
{
    struct example_5* x = (struct example_5*)a;
    u32 count = (x->data_08 >> 3);
    return count;
}

ACTOR*
string_iterator(ACTOR* s)
{
    u8* p;
    int n;

    u8* bp = (u8*)s;
    u8 b = *(bp + 0x05);
    if (b == octets) {
        struct example_5* x = (struct example_5*)reserve();  // new iterator
        if (!x) return NULL;  // fail!
        // FIXME: fill in code and beh for iterator actor!
        p = bp + 0x06;
        if (!decode_integer(&n, &p)) return NULL;  // fail!
        x->data_04 = (u32)n;  // total octets remaining
        x->data_08 = (u32)p;  // pointer to starting octet
        if (n <= 20) {
            x->data_0c = (u32)(p + n);  // pointer to ending octet
        } else {
            x->data_0c = (u32)(p + 12);  // pointer to ending octet
        }
        return (ACTOR*)x;  // success.
    }
    return NULL;  // fail!
}

u32
next_character(ACTOR* it)
{
    struct example_5* x = (struct example_5*)it;
    int n = x->data_04;
    if (n > 0) {
        u8* p = (u8*)x->data_08;
        u8* q = (u8*)x->data_0c;
        if (p >= q) {  // out of bounds
            p = (u8*)(*((u32*)q));  //  load next block of data
            q = p + 0x1c;  // update end
        }
        // FIXME: use decode procedure to handling encoding
        u32 code = *p++;
        x->data_04 = --n;  // update count
        x->data_08 = (u32)p;  // update start
        return code;  // success.
    }
    return EOF;  // fail!
}

/*
 * "standard" library
 */

#define MAX_INT ((int)0x7FFFFFFF)

int
strlen(char* s)
{
    int n = 0;
#if 1
    char* r;

    if ((r = s)) {
        while ((*s)) {
            ++s;
        }
    }
    return (s - r);
#else
    while ((*s++)) {
        ++n;
    }
#endif
    return n;
}

/*
 * test suite
 */

static u8 buf_0[] = {
//    object_n, n_104, n_2,
    object_n, n_109, n_2,
        octets, n_5, 's', 'p', 'a', 'c', 'e',
        object, n_32,
            utf8, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
            array_n, n_3, n_2,
                n_m40,
                n_m20,
            utf8, n_6, 'e', 'x', 't', 'e', 'n', 't',
            array_n, n_9, n_2,
                p_int_0, n_2, 600 & 0xFF, 600 >> 8,
                p_int_0, n_2, 460 & 0xFF, 460 >> 8,
//        utf8, n_6, 's', 'h', 'a', 'p', 'e', 's',
        utf8, p_int_0, n_4, 6, 0, 0, 0, 's', 'h', 'a', 'p', 'e', 's',
        array, n_52,
            object, n_24,
                utf8, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
                array, n_2, n_5, n_3,
                utf8, n_6, 'e', 'x', 't', 'e', 'n', 't',
                array, n_2, n_21, n_13,
            object, n_24,
                utf8, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
                array, n_2, n_8, n_5,
                utf8, n_6, 'e', 'x', 't', 'e', 'n', 't',
                array, n_2, n_13, n_8,
};

void
test_bose()
{
    u8* data;
    ACTOR* a;
    char* s;
    struct example_5* x;

    hexdump(buf_0, sizeof(buf_0));

    data = buf_0;
    print_bose(&data, 1, MAX_INT);
    newline();

    data = buf_0;
    print_bose(&data, 0, 2);
    newline();

    a = new_u32(42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_i32(-42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_u32(-42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = &v_string_0;
    puts("&v_string_0 = 0x");
    serial_hex32((u32)a);
    putchar('\n');
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    s = "";
    a = new_octets((u8*)s, (u32)strlen(s));
//    a = new_literal("");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_literal("test");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_literal("Hello, World!");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_literal("< twenty characters");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_literal("<= twenty characters");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_literal("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    x = (struct example_5*)a;
    a = (ACTOR*)(x->data_18);  // follow extended block pointer
    while (a) {
        dump_words((u32*)a, 8);
        hexdump((u8*)a, 32);
        x = (struct example_5*)a;
        a = (x->beh_1c);  // follow extended data pointer
    }

    s = "0123456789+-*/abcdefghijklmnopqrstuvwxyz";
    a = new_octets((u8*)s, (u32)strlen(s));
//    a = new_literal("0123456789+-*/abcdefghijklmnopqrstuvwxyz");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    x = (struct example_5*)a;
    a = (ACTOR*)(x->data_18);  // follow extended block pointer
    while (a) {
        dump_words((u32*)a, 8);
        hexdump((u8*)a, 32);
        x = (struct example_5*)a;
        a = (x->beh_1c);  // follow extended data pointer
    }

    a = new_array();
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    a = new_object();
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);

    puts("Completed.\n");
}
