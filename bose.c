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

#define ASCII_UTF8_TO_OCTETS    0  // automatically encode ascii utf8 as octets

#define DEBUG(x) x /* debug logging */
#define TRACE(x)   /* trace logging */

/*
 * library utilities
 */

#define MIN_INT ((int)0x80000000)
#define MAX_INT ((int)0x7FFFFFFF)

int
cstr_len(char* s)
{
    char* r;

    if ((r = s)) {
        while ((*s)) {
            ++s;
        }
    }
    return (s - r);
}

void
assert_fail(char* _file_, int _line_)
{
    putchar('\n');
    puts(_file_);
    putchar(':');
    serial_dec32(_line_);
    puts(" -- assert failed!");
    putchar('\n');
    cal_fail();
}

#define assert(cond)    if (!(cond)) assert_fail(__FILE__, __LINE__)
#define assert_eq(a,b)  assert((a)==(b))
#define assert_ne(a,b)  assert((a)!=(b))

/*
 * console output
 */

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

/*
 * BOSE encode/decode helpers
 */

#define MAX_UNICODE ((int)0x10FFFF)

static int
decode_ext_int(int* result, u8 prefix, ACTOR* it)
{
    u8 b = prefix;
    int size = 0;
    if (!decode_int(&size, it)) return false;  // fail!
    if (((b & 0xF0) == 0x10)  // Integer type?
    &&  (size <= sizeof(int))) {  // not too large?
        u8 sign = (b & 0x8) ? 0xFF : 0x00;
        int shift = 0;
        int n = 0;
        while (shift < (sizeof(int) << 3)) {
            if (size > 0) {
                u32 w = read_code(it);
                if (w > MAX_UNICODE) return false;  // fail!
                b = w;
                --size;
            } else {
                b = sign;
            }
            n |= (((u32)b) << shift);
            shift += (1 << 3);
        }
        *result = n;
        TRACE(puts("decode_ext_int: int="));
        TRACE(serial_int32(n));
        TRACE(putchar('\n'));
        return true;  // success
    }
    DEBUG(puts("decode_ext_int: fail!\n"));
    return false;  // failure!
}

int
decode_int(int* result, ACTOR* it)
{
    if (!result || !it) return false;  // fail!
    u32 w = read_code(it);
    TRACE(puts("decode_int: w=0x"));
    TRACE(serial_hex32(w));
    TRACE(putchar('\n'));
    if (w > MAX_UNICODE) return false;  // fail!
    u8 b = w;  // prefix
    int n = SMOL2INT(b);
    if ((n >= SMOL_MIN) && (n <= SMOL_MAX)) {
        *result = n;
        TRACE(puts("decode_int: smol="));
        TRACE(serial_int32(n));
        TRACE(putchar('\n'));
        return true;  // success
    }
    return decode_ext_int(result, b, it);
}

static int  // return k = <0:fail, 0:done, >0:more...
decode_octets(u32* wp, u8 b, int k)
{
    *wp = (u32)b;
    return k;  // (k == 0)
}

static int  // return k = <0:fail, 0:done, >0:more...
decode_utf8(u32* wp, u8 b, int k)
{
    if (b < 0x80) {  // 1-octet encoding (ascii)
        if (k != 0) return -1;  // fail!
        *wp = (u32)b;
        return 0;  // done
    }
    if ((b & 0xC0) == 0x80) {  // continuation byte
        if (k <= 0) return -1;  // fail!
        *wp = ((*wp) << 6) | (b & 0x3F);  // shift in next 6 bits
        return --k;
    }
    if ((b & 0xE0) == 0xC0) {  // 2-octet encoding
        if (k != 0) return -1;  // fail!
        *wp = (u32)(b & 0x1F);  // first 5 bits
        return 1;  // 1 more
    }
    if ((b & 0xF0) == 0xE0) {  // 3-octet encoding
        if (k != 0) return -1;  // fail!
        *wp = (u32)(b & 0x0F);  // first 4 bits
        return 2;  // 2 more
    }
    if ((b & 0xF8) == 0xF0) {  // 4-octet encoding
        if (k != 0) return -1;  // fail!
        *wp = (u32)(b & 0x07);  // first 3 bits
        return 3;  // 3 more
    }
    return -1;  // fail!
}

int
encode_u32(ACTOR* sb, u32 w)
{
    int ok = true;
    if (w <= SMOL_MAX) {
        ok = write_code(sb, INT2SMOL(w));
    } else if (w <= 0xFFFF) {
        ok = write_code(sb, p_int_0)
          && write_code(sb, n_2)
          && write_code(sb, (w & 0xFF))
          && write_code(sb, ((w >> 8) & 0xFF));
    } else {
        ok = write_code(sb, p_int_0)
          && write_code(sb, n_4)
          && write_code(sb, (w & 0xFF))
          && write_code(sb, ((w >> 8) & 0xFF))
          && write_code(sb, ((w >> 16) & 0xFF))
          && write_code(sb, ((w >> 24) & 0xFF));
    }
    return ok;
}

int
encode_int(ACTOR* sb, int n)
{
    int ok = true;
    if (n < 0) {
        if (n >= SMOL_MIN) {
            ok = write_code(sb, INT2SMOL(n));
        } else {
            ok = write_code(sb, m_int_0)
              && write_code(sb, n_4)
              && write_code(sb, (n & 0xFF))
              && write_code(sb, ((n >> 8) & 0xFF))
              && write_code(sb, ((n >> 16) & 0xFF))
              && write_code(sb, ((n >> 24) & 0xFF));
        }
    } else {
        ok = encode_u32(sb, n);
    }
    return ok;
}

static int  // return k = <0:fail, 0:done, >0:more...
encode_octets(u8* bp, u32 w, int k)
{
    *bp = (u8)w;
    return k;  // (k == 0)
}

static int  // return k = <0:fail, 0:done, >0:more...
encode_utf8(u8* bp, u32 w, int k)
{
    // leading bytes
    if (k == 0) {
        if (w < 0x80) {  // 1-octet encoding (ascii)
            *bp = (u8)w;
            return 0;  // done
        }
        if (w < 0x800) {  // 2-octet encoding
            *bp = 0xC0 | (u8)(w >> 6);  // first 5 bits
            return 1;
        }
        if (w < 0x10000) {  // 3-octet encoding
            *bp = 0xE0 | (u8)(w >> 12);  // first 4 bits
            return 2;
        }
        if (w <= MAX_UNICODE) {  // 4-octet encoding
            *bp = 0xF0 | (u8)(w >> 18);  // first 3 bits
            return 3;
        }
    }
    // continuation bytes (next 6 bits)
    if (k == 1) {
        *bp = 0x80 | (u8)(w & 0x3F);
        return 0;
    }
    if (k == 2) {
        *bp = 0x80 | (u8)((w >> 6) & 0x3F);
        return 1;
    }
    if (k == 3) {
        *bp = 0x80 | (u8)((w >> 12) & 0x3F);
        return 2;
    }
    return -1;  // fail!
}

/*
 * decode BOSE values
 */

static ACTOR*
decode_number(u8 prefix, ACTOR* it)
{
    int n;
    ACTOR* v = NULL;  // init to fail
    if (decode_ext_int(&n, prefix, it)) {
        v = new_int(n);
    }
    // FIXME: handle additional number encodings
    return v;
}

static ACTOR*
decode_string(u8 prefix, ACTOR* it)
{
    int (*decode)(u32*,u8,int);  // character decode procedure
    ACTOR* v = NULL;  // init to fail
    if (prefix & 0x01) return NULL;  // fail! -- memo not supported
    int size = 0;
    if (!decode_int(&size, it)) return NULL;  // fail!
    if (prefix == octets) {
        TRACE(puts("decode_string: octets\n"));
        decode = decode_octets;
    } else if (prefix == utf8) {
        TRACE(puts("decode_string: utf8\n"));
        decode = decode_utf8;
    } else {
        // FIXME: handle additional string encodings
        DEBUG(puts("decode_string: unsupported encoding\n"));
        return NULL;  // fail!
    }
    ACTOR* sb = new_string_builder(prefix);
    if (!sb) return NULL;  // fail!
#if ASCII_UTF8_TO_OCTETS
    int ascii = true;
#endif
    u32 ch = 0;
    int k = 0;
    while (size-- > 0) {
        u32 w = read_code(it);
        if (w > 0xFF) return NULL;  // fail! -- not in octet range
#if ASCII_UTF8_TO_OCTETS
        if (w > 0x7F) {
            ascii = false;
        }
#endif
        u8 b = w;
        k = (*decode)(&ch, b, k);  // call decode procedure
        if (k < 0) return NULL;  // fail!
        if (k == 0) {  // character complete
            if (!write_code(sb, ch)) return NULL;  // fail!
            ch = 0;
            k = 0;
        }
    }
    v = get_string_built(sb);
    release(sb);
#if ASCII_UTF8_TO_OCTETS
    if ((prefix == utf8) && ascii) {
        u8* p = (u8*)v;
        *(p + 0x05) = octets;  // replace utf8 prefix with octets
        DEBUG(puts("decode_string: ascii utf8->octets\n"));
    }
#endif
    return v;
}

static ACTOR*
decode_array(u8 prefix, ACTOR* it)
{
    ACTOR* v = NULL;  // init to fail
    int size = 0;
    if (!decode_int(&size, it)) return NULL;  // fail!
    struct example_5* x = (struct example_5*)it;  // outer iterator
    struct example_5* y = (struct example_5*)reserve();  // inner iterator
    if (!y) return NULL;  // fail!
    *y = *x;  // copy outer iterator to inner
    y->data_04 = (u32)size;  // set inner size
    ACTOR *in = (ACTOR*)y;  // inner iterator
    int count = MAX_INT;
    if (prefix == array_n) {  // counted array?
        if (!decode_int(&count, in)) return NULL;  // fail!
    }
    v = new_array();
    while (y->data_04 > 0) {
        ACTOR* item = decode_bose(in);
        if (!item) return NULL;  // fail!
        ACTOR* a = array_insert(v, array_element_count(v), item);
        if (!v) return NULL;  // fail!
        release(v);
        v = a;
        --count;
        // FIXME: check for correct element count?
    }
    y->data_04 = x->data_04 - size;  // calculate outer size remaining
    *x = *y;  // update outer iterator from inner
    release(y);  // release inner iterator
    return v;
}

static ACTOR*
decode_object(u8 prefix, ACTOR* it)
{
    ACTOR* v = NULL;  // init to fail
    int size = 0;
    if (!decode_int(&size, it)) return NULL;  // fail!
    struct example_5* x = (struct example_5*)it;  // outer iterator
    struct example_5* y = (struct example_5*)reserve();  // inner iterator
    if (!y) return NULL;  // fail!
    *y = *x;  // copy outer iterator to inner
    y->data_04 = (u32)size;  // set inner size
    ACTOR *in = (ACTOR*)y;  // inner iterator
    int count = MAX_INT;
    if (prefix == object_n) {  // counted object?
        if (!decode_int(&count, in)) return NULL;  // fail!
    }
    v = new_object();
    while (y->data_04 > 0) {
        ACTOR* name = decode_bose(in);
        if (!name) return NULL;  // fail!
        ACTOR* value = decode_bose(in);
        if (!value) return NULL;  // fail!
        ACTOR* o = object_set(v, name, value);
        if (!v) return NULL;  // fail!
        release(v);
        v = o;
        --count;
        // FIXME: check for correct property count?
    }
    y->data_04 = x->data_04 - size;  // calculate outer size remaining
    *x = *y;  // update outer iterator from inner
    release(y);  // release inner iterator
    return v;
}

ACTOR*
decode_bose(ACTOR* it)
{
    ACTOR* v = NULL;  // init to fail
    if (!it) return NULL;  // fail!
    u32 w = read_code(it);
    TRACE(puts("decode_bose: w=0x"));
    TRACE(serial_hex32(w));
    TRACE(putchar('\n'));
    if (w > MAX_UNICODE) return NULL;  // fail!
    u8 b = w;  // prefix
    switch (b) {
        case null: {
            v = &v_null;
            break;
        }
        case true: {
            v = &v_true;
            break;
        }
        case false: {
            v = &v_false;
            break;
        }
        case n_0: {
            v = &v_number_0;
            break;
        }
        case string_0: {
            v = &v_string_0;
            break;
        }
        case array_0: {
            v = &v_array_0;
            break;
        }
        case object_0: {
            v = &v_object_0;
            break;
        }
        default: {
            int n = SMOL2INT(b);
            if ((n >= SMOL_MIN) && (n <= SMOL_MAX)) {
                v = new_int(n);
            } else if ((b & 0xF8) == 0x08) {  // String type (2#0000_1xxx)
                v = decode_string(b, it);
            } else if ((b & 0xF9) == 0x00) {  // Array type (2#0000_0xx0) != false
                v = decode_array(b, it);
            } else if ((b & 0xF9) == 0x01) {  // Object type (2#0000_0xx1) != true
                v = decode_object(b, it);
            } else {
                v = decode_number(b, it);
            }
            break;
        }
    }
    TRACE(puts("decode_bose: v=0x"));
    TRACE(serial_hex32(v));
    TRACE(putchar('\n'));
    return v;
}

/*
 * encode BOSE values
 */

static int
encode_number(ACTOR* sb, ACTOR* v)
{
    int ok = true;
    u8* p = (u8*)v;
    u8 b = *(p + 0x05);  // prefix
    u32 w = *((u32*)(p + 0x08));  // integer

    TRACE(puts("encode_number[0x"));
    TRACE(serial_hex32(w));
    TRACE(puts("]\n"));
    if ((b & ~0x7) == p_int_0) {
        ok = encode_u32(sb, w);
    } else if ((b & ~0x7) == m_int_0) {
        ok = encode_int(sb, w);
    } else {
        // FIXME: handle different number formats and bignums...
        ok = false;  // fail!
    }
    return ok;
}

static int
encode_string(ACTOR* sb, ACTOR* v)
{
    int ok = true;
    u8* p = (u8*)v;
    u8 b = *(p + 0x05);  // prefix
    u8* q = p + 0x18;  // ending octet (extended size)
    u32 w = SMOL2INT(*(p + 0x06));  // smol size
    if (w <= 20) {
        q = p + 0x1b;  // ending octet (smol size)
        TRACE(puts("encode_string["));
        TRACE(serial_int32(w));
        TRACE(puts(":smol]\n"));
        if (w == 0) return write_code(sb, string_0);  // special case for empty string
        p += 0x07;  // pointer to starting octet
    } else {
        w = *((u32*)(p + 0x08));  // get extended size
        TRACE(puts("encode_string["));
        TRACE(serial_int32(w));
        TRACE(puts(":int]\n"));
        if (w == 0) return write_code(sb, string_0);  // special case for empty string
        p += 0x0c;  // pointer to starting octet
    }
    ok = write_code(sb, b)
      && encode_u32(sb, w);
    while (ok && (w-- > 0)) {
        if (p >= q) {  // out of bounds
            p = (u8*)(*((u32*)q));  //  load next block of data
            q = p + 0x1c;  // update end
        }
        ok = write_code(sb, *p++);
    }
    return ok;
}

static int
encode_array(ACTOR* sb, ACTOR* v)
{
    int ok = true;
    u32 w = array_element_count(v);
    TRACE(puts("encode_array["));
    TRACE(serial_dec32(w));
    TRACE(puts("]\n"));
    if (w == 0) return write_code(sb, array_0);  // special case for empty array
    // encode array contents
    ACTOR* it = new_collection_iterator(v);
    if (!it) return false;  // fail!
    ACTOR* content = new_string_builder(octets);
    if (!content) return false;  // fail!
    while (w-- > 0) {
        ACTOR* item = read_item(it);
        if (!item) return false;  // fail!
        if (!encode_bose(content, item)) return false;  // fail!
    }
    release(it);
    ACTOR* s = get_string_built(content);
    struct example_5* x = (struct example_5*)s;
    release(content);
    w = x->data_08;  // get content size
    TRACE(puts("encode_array: content size = "));
    TRACE(serial_dec32(w));
    TRACE(newline());
    // encode array (w/ known size)
    ok = write_code(sb, array)
      && encode_u32(sb, w);
    it = new_string_iterator((ACTOR*)s);
    if (!it) return false;  // fail!
    TRACE(puts("encode_array: content"));
    while (ok && (w-- > 0)) {
        u32 ch = read_code(it);
        if (ch == EOF) return false;  // fail!
        TRACE(putchar(' '));
        TRACE(serial_hex8(ch));
        ok = write_code(sb, ch);
    }
    TRACE(newline());
    release(s);
    release(it);
    return ok;
}

static int
encode_object(ACTOR* sb, ACTOR* v)
{
    int ok = true;
    u32 w = object_property_count(v);
    TRACE(puts("encode_object["));
    TRACE(serial_dec32(w));
    TRACE(puts("]\n"));
    if (w == 0) return write_code(sb, object_0);  // special case for empty object
    // encode object contents
    ACTOR* it = new_collection_iterator(v);
    if (!it) return false;  // fail!
    ACTOR* content = new_string_builder(octets);
    if (!content) return false;  // fail!
    while (w-- > 0) {
        ACTOR* name = read_item(it);
        if (!name) return false;  // fail!
        if (!encode_bose(content, name)) return false;  // fail!
        ACTOR* value = read_item(it);
        if (!value) return false;  // fail!
        if (!encode_bose(content, value)) return false;  // fail!
    }
    release(it);
    ACTOR* s = get_string_built(content);
    struct example_5* x = (struct example_5*)s;
    release(content);
    w = x->data_08;  // get content size
    TRACE(puts("encode_object: content size = "));
    TRACE(serial_dec32(w));
    TRACE(newline());
    // encode object (w/ known size)
    ok = write_code(sb, object)
      && encode_u32(sb, w);
    it = new_string_iterator((ACTOR*)s);
    if (!it) return false;  // fail!
    TRACE(puts("encode_object: content"));
    while (ok && (w-- > 0)) {
        u32 ch = read_code(it);
        if (ch == EOF) return false;  // fail!
        TRACE(putchar(' '));
        TRACE(serial_hex8(ch));
        ok = write_code(sb, ch);
    }
    TRACE(newline());
    release(s);
    release(it);
    return ok;
}

int
encode_bose(ACTOR* sb, ACTOR* v)
{
    int ok = true;
    struct example_5* x = (struct example_5*)v;
    u8* p = (u8*)v;
    u8 b = *(p + 0x05);

    if (x->beh_1c != &b_value) {
        DEBUG(puts("encode_bose: expected &b_value\n"));
        ok = false;  // fail! -- wrong actor type
    } else if ((b == null) || (b == true) || (b == false)) {
        ok = write_code(sb, b);
    } else if ((b & 0xF8) == 0x08) {  // String type (2#0000_1xxx)
        ok = encode_string(sb, v);
    } else if ((b & 0xF9) == 0x00) {  // Array type (2#0000_0xx0) != false
        ok = encode_array(sb, v);
    } else if ((b & 0xF9) == 0x01) {  // Object type (2#0000_0xx1) != true
        ok = encode_object(sb, v);
    } else {
        ok = encode_number(sb, v);
    }
    return ok;
}

/*
 * composite data structures
 */

int
value_equal(ACTOR* a, ACTOR* b)
{
    if (a == b) return true;  // identical
    if ((a == NULL) || (b == NULL)) return false;  // NULL check
    struct cal_value* x = (struct cal_value*)a;
    struct cal_value* y = (struct cal_value*)b;
    if (x->byte_05 == y->byte_05) {
        u8 prefix = x->byte_05;
        if (prefix <= object_0) return true;  // false, true, [], {}
        if (prefix == string_0) return true;  // ""
        if (prefix >= n_m64) return true;     // smol numbers, null
        if ((prefix >= p_int_0) && (prefix <= m_base_7)) {
            return (number_compare(a, b) == 0);
        }
        if ((prefix >= octets) && (prefix <= s_encoded)) {
            return (string_compare(a, b) == 0);
        }
        if ((prefix == array) || (prefix == array_n)) {
            if (x->data_08 != y->data_08) return false;  // size check
            ACTOR* ai = new_collection_iterator(a);
            ACTOR* bi = new_collection_iterator(b);
            if (!ai || !bi) return false;  // fail!
            ACTOR* av = NULL;
            ACTOR* bv = NULL;
            do {
                av = read_item(ai);
                bv = read_item(bi);
                if (!value_equal(av, bv)) return false;
            } while (av && bv);
            return true;  // all elements matched!
        }
        if ((prefix == object) || (prefix == object_n)) {
            if (x->data_08 != y->data_08) return false;  // size check
            ACTOR* it = new_collection_iterator(a);
            if (!it) return false;  // fail!
            ACTOR* name = NULL;
            while ((name = read_item(it)) != NULL) {
                ACTOR* value = read_item(it);
                if (!value_equal(value, object_get(b, name))) return false;
            };
            return true;  // all properties matched!
        }
    }
    return false;
}

int
number_compare(ACTOR* a, ACTOR* b)  // MIN_INT = incomparable
{
    if (a == b) return 0;  // identical
    struct cal_value* x = (struct cal_value*)a;
    struct cal_value* y = (struct cal_value*)b;
    if ((x->byte_05 == y->byte_05)
    &&  (x->byte_06 == y->byte_06)) {
        u8 prefix = x->byte_05;
        int size = SMOL2INT(x->byte_06);
        if ((prefix >= p_int_0) && (prefix <= m_int_7) && (size <= 4)) {
            return (x->data_08 - y->data_08);
        }
    }
    return MIN_INT;
}

int
string_compare(ACTOR* s, ACTOR* t)  // MIN_INT = incomparable
{
    int d = 0;  // difference
    if (s == t) return 0;  // identical
    ACTOR* si = new_string_iterator(s);
    if (!si) return MIN_INT;  // fail!
    ACTOR* ti = new_string_iterator(t);
    if (!ti) return MIN_INT;  // fail!
    while (d == 0) {
        u32 sc = read_code(si);
        u32 tc = read_code(ti);
        d = (int)(sc - tc);
        if ((sc == EOF) || (tc == EOF)) break;  // end of string(s)
    }
    return d;
}


ACTOR*
new_string_iterator(ACTOR* s)
{
    u8* p;
    int n;

    struct example_5* x = (struct example_5*)reserve();  // new iterator
    if (!x) return NULL;  // fail!
    u8* bp = (u8*)s;
    u8 b = *(bp + 0x05);  // prefix
    // FIXME: handle special case for string_0
    // set decode procedure
    TRACE(puts("new_string_iterator: prefix = "));
    TRACE(serial_hex8(b));
    TRACE(newline());
    if (b == octets) {
        x->data_18 = (u32)decode_octets;
    } else if (b == utf8) {
        x->data_18 = (u32)decode_utf8;
    } else {
        DEBUG(puts("new_string_iterator: unsupported encoding\n"));
        release(x);
        return NULL;  // fail! -- unsupported encoding
    }
    // initialize iterator
    // FIXME: fill in code and beh for iterator actor!
    b = *(bp + 0x06);  // smol size
    n = SMOL2INT(b);
    if ((n >= 0) && (n <= 20)) {
        p = bp + 0x07;  // pointer to starting octet
        x->data_0c = (u32)(p + n);  // pointer to ending octet
    } else {
        n = *((int*)(bp + 0x08));  // get extended size
        p = bp + 0x0c;  // pointer to starting octet
        x->data_0c = (u32)(p + 12);  // pointer to ending octet
    }
    x->data_04 = (u32)n;  // total octets remaining
    x->data_08 = (u32)p;  // pointer to starting octet
    return (ACTOR*)x;  // success.
}

u32
read_code(ACTOR* it)
{
    u32 ch = 0;
    struct example_5* x = (struct example_5*)it;
    int n = x->data_04;
    int (*decode)(u32*,u8,int) = (int(*)(u32*,u8,int))(x->data_18);
    int k = 0;
    while (n > 0) {
        u8* p = (u8*)x->data_08;
        u8* q = (u8*)x->data_0c;
        if (p >= q) {  // out of bounds
            p = (u8*)(*((u32*)q));  //  load next block of data
            x->data_0c = (u32)(p + 0x1c);  // update end
        }
        k = (*decode)(&ch, *p++, k);  // call decode procedure
        if (k < 0) return EOF;  // fail!
        x->data_04 = --n;  // update count
        x->data_08 = (u32)p;  // update start
        if (k == 0) return ch;  // success.
    }
    return EOF;  // end of string
}

ACTOR*
new_string_builder(u8 prefix)
{
    u8* p;
    int n;

    struct example_5* s = (struct example_5*)reserve();
    if (!s) return NULL;  // fail!
    struct example_5* z = (struct example_5*)(&v_string_0);
    *s = *z;  // copy empty string template
    p = (u8*)s;
    *(p + 0x05) = prefix;
    *(p + 0x06) = p_int_0;  // extended size format
    *(p + 0x07) = n_4;  // size is a 4-byte integer
    p += 0x0c;  // advance to data
    n = 12;  // initial allocate holds 12 octets
    struct example_5* x = (struct example_5*)reserve();  // new builder
    if (!x) return NULL;  // fail!
    // FIXME: fill in code and beh for builder actor!
    x->data_04 = (u32)s;  // pointer to String being mutated
    x->data_08 = (u32)p;  // pointer to starting octet
    x->data_0c = (u32)(p + n);  // pointer to ending octet
    // set encode procedure
    TRACE(puts("new_string_builder: prefix = "));
    TRACE(serial_hex8(prefix));
    TRACE(newline());
    if (prefix == octets) {
        x->data_18 = (u32)encode_octets;
    } else if (prefix == utf8) {
        x->data_18 = (u32)encode_utf8;
    } else {
        DEBUG(puts("new_string_builder: unsupported encoding\n"));
        release(x);
        release(s);
        return NULL;  // fail! -- unsupported encoding
    }
    return (ACTOR*)x;  // success.
}

int
write_code(ACTOR* sb, u32 code)
{
    struct example_5* x = (struct example_5*)sb;
    struct example_5* s = (struct example_5*)x->data_04;
    u8* p = (u8*)x->data_08;
    u8* q = (u8*)x->data_0c;
    int (*encode)(u8*,u32,int) = (int(*)(u8*,u32,int))(x->data_18);
    int k = 0;
    do {
        if (p >= q) {  // out of space
            struct example_5* y = (struct example_5*)reserve();
            if (!y) return false;  // fail!
            y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
            p = (u8*)y;  // update start
            *((u32*)q) = (u32)p;  // link to next block
            q = (p + 0x1c);
            x->data_0c = (u32)q;  // update end
        }
        k = (*encode)(p, code, k);  // call encode procedure
        if (k < 0) return false;  // fail!
        ++(s->data_08);  // update count
        x->data_08 = (u32)(++p);  // update start
    } while (k > 0);
    return true;  // success.
}

ACTOR*
new_array()  // allocate a new (empty) array
{
    struct example_5* x = (struct example_5*)reserve();
    if (!x) return NULL;  // fail!
    struct example_5* y = (struct example_5*)(&v_array_0);
    *x = *y;  // copy empty array template
    return (ACTOR*)x;
}

ACTOR*
array_insert(ACTOR* a, u32 index, ACTOR* element)  // insert element at (0-based) index
{
    ACTOR* b = NULL;

    struct example_5* x = (struct example_5*)a;
    u32 count = array_element_count(a);
    TRACE(puts("array_insert: a=0x"));
    TRACE(serial_hex32((u32)a));
    TRACE(puts(", index="));
    TRACE(serial_dec32(index));
    TRACE(puts(", count="));
    TRACE(serial_dec32(count));
    TRACE(puts(", element=0x"));
    TRACE(serial_hex32((u32)element));
    TRACE(putchar('\n'));
    if (x->beh_1c != &b_value) return NULL;  // fail! -- wrong actor type
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
        TRACE(puts("array_insert: allocated b="));
        TRACE(dump_words((u32*)b, 8));
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
        TRACE(puts("array_insert: at v=0x"));
        TRACE(serial_hex32((u32)v));
        TRACE(puts(", i="));
        TRACE(serial_dec32(i));
        TRACE(puts(", n="));
        TRACE(serial_dec32(n));
        TRACE(puts(", element=0x"));
        TRACE(serial_hex32((u32)element));
        TRACE(putchar('\n'));
        *v++ = (u32)element;
        ++i;
        // copy pointers after index
        while (i <= count) {
            if (n == 1) {  // next dst block
                y = (struct example_5*)reserve();
                if (!y) return NULL;  // fail!
                y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
                *v = (u32)y;
                v = (u32*)y;
            } else if (n == 0) {  // next src block
                x = (struct example_5*)(*w);
                w = (u32*)x;
                n = 7;
            }
            *v++ = *w++;  // copy pointer
            --n;
            ++i;
        }
    }
    TRACE(puts("array_insert: returning b=0x"));
    TRACE(serial_hex32((u32)b));
    TRACE(putchar('\n'));
    return b;
}

ACTOR*
array_element(ACTOR* a, u32 index)  // retrieve element at (0-based) index
{
    struct example_5* x = (struct example_5*)a;
    u32 count = array_element_count(a);
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
new_object()  // allocate a new (empty) object
{
    struct example_5* x = (struct example_5*)reserve();
    if (!x) return NULL;  // fail!
    struct example_5* y = (struct example_5*)(&v_object_0);
    *x = *y;  // copy empty object template
    return (ACTOR*)x;
}

ACTOR*
object_set(ACTOR* o, ACTOR* name, ACTOR* value)  // set property in object
{
    ACTOR* b = NULL;

    struct example_5* x = (struct example_5*)o;
    u32 count = object_property_count(o);
    TRACE(puts("object_set: o=0x"));
    TRACE(serial_hex32((u32)o));
    TRACE(puts(", count="));
    TRACE(serial_dec32(count));
    TRACE(puts(", name=0x"));
    TRACE(serial_hex32((u32)name));
    TRACE(puts(", value=0x"));
    TRACE(serial_hex32((u32)value));
    TRACE(putchar('\n'));
    if (x->beh_1c != &b_value) return NULL;  // fail! -- wrong actor type
    TRACE(puts("object_set: name="));
    TRACE(dump_words((u32*)name, 8));
    TRACE(puts("object_set: value="));
    TRACE(dump_words((u32*)value, 8));
    b = (ACTOR*)reserve();
    struct example_5* y = (struct example_5*)b;
    y->code_00 = x->code_00;  // copy code field
    y->data_04 = x->data_04;  // copy object header
    y->data_08 = x->data_08;  // copy size
    y->data_18 = 0;  // NULL next/link pointer
    y->beh_1c = x->beh_1c;  // copy actor behavior
    u32 n = 3;  // number of pointers in block
    u32* w = &x->data_0c;  // src pointer
    u32* v = &y->data_0c;  // dst pointer
    TRACE(puts("object_set: allocated b="));
    TRACE(dump_words((u32*)b, 8));
    // copy properties while searching for name match
    int d = MIN_INT;
    while (count > 0) {
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
        if (d != 0) {  // only compare names if not already found
            d = string_compare(name, (ACTOR*)(*w));
            TRACE(puts("object_set: string_compare("));
            TRACE(to_JSON(name, 0, MAX_INT));
            TRACE(puts(", "));
            TRACE(to_JSON((ACTOR*)(*w), 0, MAX_INT));
            TRACE(puts(") = "));
            TRACE(serial_int32(d));
            TRACE(putchar('\n'));
            if (d == MIN_INT) return NULL;  // fail!
            if (d == 0) {  // name matched
                *v++ = *w++;  // copy name pointer
                --n;
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
                *v++ = (u32)value;  // replace value pointer
                ++w;  // skip src value pointer
                --n;
                --count;  // decement property count
                continue;  // next loop iteration
            }
        }
        *v++ = *w++;  // copy name pointer
        --n;
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
        *v++ = *w++;  // copy value pointer
        --n;
        --count;  // decement property count
    }
    if (d != 0) {  // no match found, append new property
        TRACE(puts("object_set: no match found, append new property.\n"));
        if (n == 0) {  // next block
            y = (struct example_5*)reserve();
            if (!y) return NULL;  // fail!
            y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
            *v = (u32)y;
            v = (u32*)y;
            n = 7;
        }
        *v++ = (u32)name;  // new name pointer
        --n;
        if (n == 0) {  // next block
            y = (struct example_5*)reserve();
            if (!y) return NULL;  // fail!
            y->beh_1c = (ACTOR*)0;  // NULL next/link pointer
            *v = (u32)y;
            v = (u32*)y;
            n = 7;
        }
        *v++ = (u32)value;  // new value pointer
        //--n;  <-- don't need to update because we're done here!
        x = (struct example_5*)o;
        y = (struct example_5*)b;
        y->data_08 = x->data_08 + 8;  // increase size
    }
    TRACE(puts("object_set: returning b=0x"));
    TRACE(serial_hex32((u32)b));
    TRACE(putchar('\n'));
    return b;
}

ACTOR*
object_get(ACTOR* o, ACTOR* name)  // get property value from object
{
    ACTOR* a = NULL;
    ACTOR* it = new_collection_iterator(o);
    if (!it) return NULL;  // fail!
    while ((a = read_item(it)) != NULL) {
        int d = string_compare(name, a);
        a = read_item(it);  // get value
        if (a == NULL) return NULL;  // fail!
        if (d == 0) {  // name matched
            return a;  // success.
        }
    }
    return NULL;  // fail!
}

ACTOR*
new_collection_iterator(ACTOR* c)
{
    u32* p;
    u32 w;

    p = (u32*)c;
    struct example_5* x = (struct example_5*)reserve();  // new iterator
    if (!x) return NULL;  // fail!
    // FIXME: fill in code and beh for iterator actor!
    p += 2;
    w = *p++;
    x->data_04 = w;  // total octets remaining
    x->data_08 = (u32)p;  // starting address
    x->data_0c = (u32)(p + 3);  // ending address
    return (ACTOR*)x;  // success.
}

ACTOR*
read_item(ACTOR* it)
{
    struct example_5* x = (struct example_5*)it;
    int n = x->data_04;
    if (n > 0) {
        u32* p = (u32*)x->data_08;
        u32* q = (u32*)x->data_0c;
        if (p >= q) {  // out of bounds
            p = (u32*)(*q);  //  load next block of data
            x->data_0c = (u32)(p + 7);  // update end
        }
        u32 w = *p++;
        x->data_04 = n - sizeof(u32);  // update count
        x->data_08 = (u32)p;  // update start
        return (ACTOR*)w;  // success.
    }
    return NULL;  // fail!
}

/*
 * conversion from internal representation to JSON string
 */

static int
number_to_JSON(ACTOR* a)
{
    u8* p = (u8*)a;
    u8 b = *(p + 0x05);

    if ((b & ~0x7) == p_int_0) {
        u32 w = *((u32*)(p + 0x08));
        serial_dec32(w);
    } else if ((b & ~0x7) == m_int_0) {
        int n = *((int*)(p + 0x08));
        serial_int32(n);
    } else {
        // FIXME: handle different number formats and bignums...
        return false;  // fail!
    }
    return true;  // success.
}

static int
string_to_JSON(ACTOR* a)
{
    u32 ch = EOF;
    ACTOR* it = new_string_iterator(a);
    if (!it) return false;  // fail!
    putchar('"');
    while ((ch = read_code(it)) != EOF) {
        switch (ch) {
            case 0x0022:    puts("\\\"");   break;
            case 0x005C:    puts("\\\\");   break;
            case 0x002F:    puts("\\/");    break;
            case 0x0008:    puts("\\b");    break;
            case 0x000C:    puts("\\f");    break;
            case 0x000A:    puts("\\n");    break;
            case 0x000D:    puts("\\r");    break;
            case 0x0009:    puts("\\t");    break;
            default:
                if ((ch < 0x0020) || (ch >= 0x007F)) {
                    if (ch >= 0x10000) {  // encode surrogate pair
                        ch -= 0x10000;
                        u32 w = (ch >> 10) + 0xD800;  // hi 10 bits
                        puts("\\u");
                        serial_hex8(w >> 8);
                        serial_hex8(w);
                        w = (ch & 0x03FF) + 0xDC00;  // lo 10 bits
                        puts("\\u");
                        serial_hex8(w >> 8);
                        serial_hex8(w);
                    } else {  // encode unicode hexadecimal escape
                        puts("\\u");
                        serial_hex8(ch >> 8);
                        serial_hex8(ch);
                    }
                } else {
                    putchar(ch);
                }
                break;
        }
    }
    putchar('"');
    return true;  // success.
}

static int
array_to_JSON(ACTOR* a, int indent, int limit)
{
    putchar('[');
    if (array_element_count(a) > 0) {
        if (limit < 1) {
            puts("...");
        } else {
            ACTOR* it = new_collection_iterator(a);
            if (!it) return false;  // fail!
            if (indent) {
                space(++indent);
            }
            int first = true;
            while ((a = read_item(it)) != NULL) {
                if (first) {
                    first = false;
                } else {
                    putchar(',');
                    space(indent);
                }
                if (!to_JSON(a, indent, limit - 1)) return false;  // fail!
            }
            if (indent) {
                space(--indent);
            }
        }
    }
    putchar(']');
    return true;  // success.
}

static int
object_to_JSON(ACTOR* a, int indent, int limit)
{
    putchar('{');
    if (object_property_count(a) > 0) {
        if (limit < 1) {
            puts("...");
        } else {
            ACTOR* it = new_collection_iterator(a);
            if (!it) return false;  // fail!
            if (indent) {
                space(++indent);
            }
            int first = true;
            while ((a = read_item(it)) != NULL) {
                if (first) {
                    first = false;
                } else {
                    putchar(',');
                    space(indent);
                }
                if (!string_to_JSON(a)) return false;  // fail!
                putchar(':');
                if (indent) {
                    putchar(' ');
                }
                a = read_item(it);
                if (a == NULL) return false;  // fail!
                if (!to_JSON(a, indent, limit - 1)) return false;  // fail!
            }
            if (indent) {
                space(--indent);
            }
        }
    }
    putchar('}');
    return true;  // success.
}

int
to_JSON(ACTOR* a, int indent, int limit)
{
    int ok = true;
    struct example_5* x = (struct example_5*)a;
    u8* p = (u8*)a;
    u8 b = *(p + 0x05);

    if (x->beh_1c != &b_value) {
        putchar('<');
        serial_hex32((u32)a);
        putchar('>');
        ok = false;  // fail! -- wrong actor type
    } else if (b == null) {
        prints("null");
    } else if (b == true) {
        prints("true");
    } else if (b == false) {
        prints("false");
    } else if ((b & 0xF8) == 0x08) {  // String type (2#0000_1xxx)
        ok = string_to_JSON(a);
    } else if ((b & 0xF9) == 0x00) {  // Array type (2#0000_0xx0) != false
        ok = array_to_JSON(a, indent, limit);
    } else if ((b & 0xF9) == 0x01) {  // Object type (2#0000_0xx1) != true
        ok = object_to_JSON(a, indent, limit);
    } else {
        ok = number_to_JSON(a);
    }
    return ok;
}

/*
 * test suite
 */

static void
dump_extended(ACTOR* a)
{
    struct example_5* x = (struct example_5*)a;
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    u8* p = (u8*)a;
    if ((p[0x06] != p_int_0) || (p[0x07] != n_4)) return;  // not extended
    a = (ACTOR*)(x->data_18);  // follow extended block pointer
    while (a && ((u8*)a >= heap_start)) {
        dump_words((u32*)a, 8);
        hexdump((u8*)a, 32);
        x = (struct example_5*)a;
        a = (x->beh_1c);  // follow next extended data pointer
    }
}

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

static void
test_number()
{
    ACTOR* a;

    a = &v_number_0;
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    assert_eq(0, number_int(a));
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_u32(42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    assert_eq(42, number_int(a));
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_int(-42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    assert_eq(-42, number_int(a));
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_u32(-42);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    assert_eq(-42, number_int(a));
    to_JSON(a, 0, MAX_INT);
    newline();
}

static void
test_string()
{
    ACTOR* a;
    ACTOR* b;
    char* s;
    int i;

    a = &v_string_0;
    puts("&v_string_0 = 0x");
    serial_hex32((u32)a);
    putchar('\n');
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    s = "";
    a = new_octets((u8*)s, (u32)cstr_len(s));
//    a = new_literal("");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_octets((u8*)"x", 1);
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("test");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("Hello, World!");
    dump_words((u32*)a, 8);
    hexdump((u8*)a, 32);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("< twenty characters");
    dump_extended(a);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("<= twenty characters");
    dump_extended(a);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    dump_extended(a);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    s = "0123456789+-*/abcdefghijklmnopqrstuvwxyz";
    a = new_octets((u8*)s, (u32)cstr_len(s));
//    a = new_literal("0123456789+-*/abcdefghijklmnopqrstuvwxyz");
    dump_extended(a);
    assert(a);
    to_JSON(a, 0, MAX_INT);
    newline();

    a = new_literal("a bird in hand is worth two in the bush");
    assert(a);
    puts("a = ");
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
    b = new_literal("a bird in hand is worth two in the bush?");
    assert(b);
    puts("b = ");
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
    i = string_compare(a, b);
    serial_int32(i);
    puts(" = (a ");
    putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
    puts(" b); ");
    assert(i < 0);
    i = string_compare(a, a);
    serial_int32(i);
    puts(" = (a ");
    putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
    puts(" a); ");
    assert(i == 0);
    i = string_compare(b, a);
    serial_int32(i);
    puts(" = (b ");
    putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
    puts(" a)\n");
    assert(i > 0);
}

static void
test_collection()
{
    ACTOR* a;
    ACTOR* b;
    u32 n;

    a = new_array();
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
    a = array_insert(a, 0, &v_true);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
    a = array_insert(a, 1, &v_false);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
    b = new_int(-2); //&v_number_0;
    dump_extended(b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
//    a = array_insert(a, 0, &v_null);
    a = array_insert(a, 0, b);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
//    a = array_insert(a, 1, &v_string_0);
//    a = array_insert(a, 3, &v_string_0);
//    a = array_insert(a, 1, b);
    a = array_insert(a, 3, &v_null);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
//    a = array_insert(a, 4, new_int(-2));
//    a = array_insert(a, 1, new_int(-2));
    a = array_insert(a, 2, &v_string_0);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');
    b = new_literal("binary-octet stream encoding");
    dump_extended(b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
    a = array_insert(a, array_element_count(a), b);
    dump_extended(a);
    to_JSON(a, 0, MAX_INT);
    putchar('\n');

    for (n = 0; n < array_element_count(a); ++n) {
        puts("a[");
        serial_dec32(n);
        puts("] = ");
        b = array_element(a, n);
        to_JSON(b, 0, MAX_INT);
        putchar('\n');
    }

    ACTOR* o = new_object();
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');
    o = object_set(o, new_literal("x"), new_int(1));
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');
    o = object_set(o, new_literal("y"), new_int(2));
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');
    o = object_set(o, new_literal("z"), new_int(0));
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');
    o = object_set(o, new_literal("x"), new_int(-1));
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');
    o = object_set(o, new_literal("y"), new_int(-2));
    dump_extended(o);
    to_JSON(o, 0, MAX_INT);
    putchar('\n');

    b = new_literal("x");
    puts("o[");
    to_JSON(b, 0, MAX_INT);
    puts("] = ");
    b = object_get(o, b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
    b = new_literal("y");
    puts("o[");
    to_JSON(b, 0, MAX_INT);
    puts("] = ");
    b = object_get(o, b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
    b = new_literal("z");
    puts("o[");
    to_JSON(b, 0, MAX_INT);
    puts("] = ");
    b = object_get(o, b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');
    b = new_literal("q");
    puts("o[");
    to_JSON(b, 0, MAX_INT);
    puts("] = ");
    b = object_get(o, b);
    to_JSON(b, 0, MAX_INT);
    putchar('\n');

    a = array_insert(a, 0, o);
    to_JSON(a, 1, 0);
    newline();
    to_JSON(a, 1, 1);
    newline();
    to_JSON(a, 1, MAX_INT);
    newline();
}

static u8 buf_smol_0[] = { n_0 };
static u8 buf_p_int_0[] = { p_int_0, n_0 };
static u8 buf_p_int_1[] = { p_int_0, n_1, 0x01 };
static u8 buf_m_int_m1[] = { m_int_0, n_1, 0xFF };
static u8 buf_m_int_m2[] = { m_int_0, n_1, 0xFE };
static u8 buf_p_int_42[] = { p_int_4, n_3, 0x2A, 0x00, 0x00 };
static u8 buf_m_int_m42[] = { m_int_4, n_3, 0xD6, 0xFF, 0xFF };
static u8 buf_p_int_2G[] = { p_int_0, n_4, 0x00, 0x00, 0x00, 0x80 };
static u8 buf_m_int_m2G[] = { m_int_0, n_4, 0x00, 0x00, 0x00, 0x80 };

static u8 buf_string_0[] = { string_0 };
static u8 buf_octets_0[] = { octets, n_0 };
static u8 buf_utf8_0[] = { utf8, n_0 };
static u8 buf_utf8_u16_0[] = { utf8, p_int_0, n_2, 0x00, 0x00 };
static u8 buf_octets_x[] = { octets, n_1, 'x' };
static u8 buf_utf8_x[] = { utf8, n_1, 'x' };
static u8 buf_octets_u16_20[] = { octets, p_int_0, n_2, 20, 0,
    '<', '=', ' ', 't', 'w', 'e', 'n', 't', 'y', ' ',
    'c', 'h', 'a', 'r', 'a', 'c', 't', 'e', 'r', 's' };
static u8 buf_utf8_u16_20[] = { utf8, p_int_0, n_2, 20, 0,
    '<', '=', ' ', 't', 'w', 'e', 'n', 't', 'y', ' ',
    'c', 'h', 'a', 'r', 'a', 'c', 't', 'e', 'r', 's' };
static u8 buf_utf16_u16_10[] = { utf16, p_int_0, n_2, 20, 0,
    0, '<', 0, '=', 0, ' ', 0, '1', 0, '0',
    0, ' ', 0, 'c', 0, 'h', 0, 'a', 0, 'r' };
static u8 buf_utf8_wikipedia[] = { utf8, n_16,
    0x24,
    0xC2, 0xA2,
    0xE0, 0xA4, 0xB9,
    0xE2, 0x82, 0xAC,
    0xED, 0x95, 0x9C,
    0xF0, 0x90, 0x8D, 0x88 };
static u8 buf_array_5[] = { array, n_9,
    null, n_0, octets, n_3, 'f', 'o', 'o', true, false };
static u8 buf_object_3[] = { object, n_22,
    octets, n_4, 'n', 'u', 'l', 'l', null,
    utf8, n_4, 't', 'r', 'u', 'e', true,
    utf8, n_5, 'f', 'a', 'l', 's', 'e', false };
static u8 buf_object_2_array_2[] = { object, n_24,
    utf8, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
    array, n_2, n_5, n_3,
    utf8, n_6, 'e', 'x', 't', 'e', 'n', 't',
    array, n_2, n_21, n_13 };

void
test_decode()
{
    ACTOR* a;
    ACTOR* b;
    int i;

    /*
     * numbers
     */

    a = new_octets(buf_smol_0, sizeof(buf_smol_0));
    dump_extended(a);
    i = 0xDEAD;  // == 57005 == -8531
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_p_int_0, sizeof(buf_p_int_0));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_p_int_1, sizeof(buf_p_int_1));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_m_int_m1, sizeof(buf_m_int_m1));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_m_int_m2, sizeof(buf_m_int_m2));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_p_int_42, sizeof(buf_p_int_42));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_m_int_m42, sizeof(buf_m_int_m42));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    a = new_octets(buf_p_int_2G, sizeof(buf_p_int_2G));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_dec32(i);  // as unsigned
        newline();
    }

    a = new_octets(buf_m_int_m2G, sizeof(buf_m_int_m2G));
    dump_extended(a);
    i = 0xDEAD;
    if (decode_int(&i, new_string_iterator(a))) {
        serial_int32(i);
        newline();
    }

    /*
     * strings
     */

    a = new_octets(buf_string_0, sizeof(buf_string_0));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }

    a = new_octets(buf_octets_0, sizeof(buf_octets_0));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }

    a = new_octets(buf_utf8_0, sizeof(buf_utf8_0));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }

    a = new_octets(buf_utf8_u16_0, sizeof(buf_utf8_u16_0));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }

    a = new_octets(buf_octets_x, sizeof(buf_octets_x));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }
    a = new_octets(buf_utf8_x, sizeof(buf_utf8_x));
    dump_extended(a);
    a = decode_bose(new_string_iterator(a));
    if (a) {
        dump_extended(a);
        to_JSON(a, 1, MAX_INT);
        i = string_compare(a, b);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        putchar(' ');
        to_JSON(b, 1, MAX_INT);
        newline();
    }

    a = new_octets(buf_octets_u16_20, sizeof(buf_octets_u16_20));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }
    a = new_octets(buf_utf8_u16_20, sizeof(buf_utf8_u16_20));
    dump_extended(a);
    a = decode_bose(new_string_iterator(a));
    if (a) {
        dump_extended(a);
        to_JSON(a, 1, MAX_INT);
        i = string_compare(a, b);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        putchar(' ');
        to_JSON(b, 1, MAX_INT);
        newline();
    }

    a = new_octets(buf_utf16_u16_10, sizeof(buf_utf16_u16_10));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"\n");
    }

    a = new_octets(buf_utf8_wikipedia, sizeof(buf_utf8_wikipedia));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
/*
        i = string_compare(b, &v_string_0);
        putchar(' ');
        putchar((i == MIN_INT) ? '?' : ((i < 0) ? '<' : ((i > 0) ? '>' : '=')));
        puts(" \"\"");
*/
        newline();
    }

    /*
     * collections
     */

    a = new_octets(buf_array_5, sizeof(buf_array_5));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        newline();
    }

    a = new_octets(buf_object_3, sizeof(buf_object_3));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        newline();
    }

    a = new_octets(buf_object_2_array_2, sizeof(buf_object_2_array_2));
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        newline();
    }

    a = new_octets(buf_0, sizeof(buf_0));  // original BOSE example
    dump_extended(a);
    b = decode_bose(new_string_iterator(a));
    if (b) {
        dump_extended(b);
        to_JSON(b, 1, MAX_INT);
        newline();
        to_JSON(b, 0, 2);
        newline();
    }
    // re-encode example
    a = new_string_builder(octets);
    if (a) {
        encode_bose(a, b);
        dump_words((u32*)a, 8);
        a = get_string_built(a);
        dump_extended(a);
        to_JSON(a, 1, MAX_INT);
        newline();
        // decode again
        b = decode_bose(new_string_iterator(a));
        if (b) {
            dump_extended(b);
            to_JSON(b, 1, MAX_INT);
            newline();
            to_JSON(b, 0, 2);
            newline();
        }
    }
}

static void
test_encode()
{
    ACTOR* v;
    ACTOR* sb;
    ACTOR* s;

    /*
     * numbers
     */

    v = &v_number_0;
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_u32(42);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_int(-42);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_u32(420);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_int(-420);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_u32(MAX_INT);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_int(MIN_INT);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    /*
     * strings
     */

    v = &v_string_0;
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_literal("x");
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_literal("testing");
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_literal("<= twenty characters");
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_literal("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    /*
     * collections
     */

    v = &v_array_0;
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_array();
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_array();
    v = array_insert(v, 0, &v_null);
    v = array_insert(v, 1, &v_true);
    v = array_insert(v, 2, &v_false);
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = &v_object_0;
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_object();
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

    v = new_object();
    v = object_set(v, new_literal("x"), new_int(1));
    v = object_set(v, new_literal("y"), new_int(-1));
    v = object_set(v, new_literal("z"), new_int(0));
    sb = new_string_builder(octets);
    if (sb && encode_bose(sb, v)) {
        s = get_string_built(sb);
        dump_extended(s);
        to_JSON(s, 1, MAX_INT);
        newline();
        release(s);
        release(sb);
    }

}

void
test_cal()
{
    puts("MIN_INT=");
    serial_int32(MIN_INT);
    puts(", MAX_UNICODE=");
    serial_int32(MAX_UNICODE);
    puts(", MAX_INT=");
    serial_int32(MAX_INT);
    newline();

    puts("sizeof(struct cal_value) = ");
    serial_dec32(sizeof(struct cal_value));
    newline();
    assert(sizeof(struct cal_value) == sizeof(struct cal_extend));
    assert(sizeof(struct cal_extend) == sizeof(struct cal_stream));

    test_number();

    test_string();

    test_collection();

    test_decode();

    test_encode();

    puts("Completed.\n");
}
