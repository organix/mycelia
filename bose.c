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
parse_integer(int* result, u8** data_ref)
{
    u8* data = *data_ref;
    u8 b = *data++;
    TRACE(puts("parse_integer: b=0x"));
    TRACE(serial_hex8(b));
    TRACE(putchar('\n'));
    int n = SMOL2INT(b);
    if ((n >= SMOL_MIN) && (n <= SMOL_MAX)) {
        *result = n;
        *data_ref = data;
        TRACE(puts("parse_integer: smol="));
        TRACE(serial_int32(n));
        TRACE(putchar('\n'));
        return true;  // success
    }
    int size = 0;
    if (parse_integer(&size, &data)) {
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
            TRACE(puts("parse_integer: int="));
            TRACE(serial_int32(n));
            TRACE(putchar('\n'));
            return true;  // success
        }
        *data_ref = end;  // skip value
    } else {
        *data_ref = data;  // update data reference
    }
    TRACE(puts("parse_integer: fail!\n"));
    return false;  // failure!
}

static int
print_number(u8** data_ref)
{
    int ok = true;
    u8* data = *data_ref;
    int n = 0;
    if (parse_integer(&n, &data)) {
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
        if (parse_integer(&size, &data)) {
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
    if (b == array_0) {
        set_color(PUNCT_COLOR);
        print('[');
        print(']');
        clear_color();
    } else {
        int size = 0;
        if (parse_integer(&size, &data)) {
            u8* end = data + size;
            if (b == array_n) {
                int count = 0;
                if (!parse_integer(&count, &data)) {
                    set_color(PUNCT_COLOR);
                    prints("<bad element count>");
                    clear_color();
                    *data_ref = end;  // skip value
                    return false;  // failure!
                }
            }
            set_color(PUNCT_COLOR);
            print('[');
            clear_color();
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
                if (!print_bose(&data, indent, limit)) {  // element
                    set_color(PUNCT_COLOR);
                    prints("<bad element>");
                    clear_color();
                    ok = false;  // fail!
                    break;
                }
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
    if (b == object_0) {
        set_color(PUNCT_COLOR);
        print('{');
        print('}');
        clear_color();
    } else {
        int size = 0;
        if (parse_integer(&size, &data)) {
            u8* end = data + size;
            if (b == object_n) {
                int count = 0;
                if (!parse_integer(&count, &data)) {
                    set_color(PUNCT_COLOR);
                    prints("<bad property count>");
                    clear_color();
                    *data_ref = end;  // skip value
                    return false;  // failure!
                }
            }
            set_color(PUNCT_COLOR);
            print('{');
            clear_color();
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
                clear_color();
                if (indent) {
                    print(' ');
                }
                if (!print_bose(&data, indent, limit)) {  // property value
                    set_color(PUNCT_COLOR);
                    prints("<bad property value>");
                    clear_color();
                    ok = false;  // fail!
                    break;
                }
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
 * test suite
 */

static u8 buf_0[] = {
    object_n, n_103, n_2,
        utf8, n_5, 's', 'p', 'a', 'c', 'e',
        object, n_32,
            utf8, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
            array_n, n_3, n_2,
                n_m40,
                n_m20,
            utf8, n_6, 'e', 'x', 't', 'e', 'n', 't',
            array_n, n_9, n_2,
                p_int_0, n_2, 600 & 0xFF, 600 >> 8,
                p_int_0, n_2, 460 & 0xFF, 460 >> 8,
        utf8, n_6, 's', 'h', 'a', 'p', 'e', 's',
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

#define MAX_INT ((int)0x7FFFFFFF)

void
test_bose()
{
    u8* data;

    data = buf_0;
    hexdump(data, sizeof(buf_0));
    print_bose(&data, 0, MAX_INT);
    newline();
}
