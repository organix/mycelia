/*
 * raspberry.c -- Raspberry Pi kernel routines written in C
 *
 * Copyright 2014 Dale Schumacher, Tristan Slominski
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
#include "raspi.h"
#include "timer.h"
#include "serial.h"
#include "xmodem.h"
#include "sexpr.h"

#define DUMP_ASCII 0  // show ascii translation of event data

/* Exported procedures (force full register discipline) */
extern void k_start(u32 sp);
extern void monitor();

extern u8 bss_start[];
extern u8 heap_start[];

/* Private data structures */
static char linebuf[256];  // line editing buffer
static int linepos = 0;  // read position
static int linelen = 0;  // write position

/* Public data structures */
const char hex[] = "0123456789abcdef";  // hexadecimal characters

/*
 * Print u8 in hexadecimal to serial port
 */
void
serial_hex8(u8 b)
{
    serial_write(hex[0xF & (b >> 4)]);
    serial_write(hex[0xF & b]);
}

/*
 * Print u32 in hexadecimal to serial port
 */
void
serial_hex32(u32 w)
{
    serial_write(hex[0xF & (w >> 28)]);
    serial_write(hex[0xF & (w >> 24)]);
    serial_write(hex[0xF & (w >> 20)]);
    serial_write(hex[0xF & (w >> 16)]);
    serial_write(hex[0xF & (w >> 12)]);
    serial_write(hex[0xF & (w >> 8)]);
    serial_write(hex[0xF & (w >> 4)]);
    serial_write(hex[0xF & w]);
}

/*
 * Print u32 in decimal to serial port
 */
void
serial_dec32(u32 w)
{
    char dec[12];
    char *p = dec + sizeof(dec);

    *--p = '\0';
    do {
        *--p = (char)((w % 10) + '0');
        w /= 10;
    } while (w && (p > dec));
    serial_puts(p);
}

/*
 * Pretty-printed byte dump
 */
void
hexdump(const u8* p, int n)
{
    int i;
    int c;

    while (n > 0) {
        serial_hex32((u32)p);
        serial_write(' ');
        for (i = 0; i < 16; ++i) {
            if (i == 8) {
                serial_write(' ');
            }
            if (i < n) {
                serial_write(' ');
                serial_hex8(p[i]);
            } else {
                serial_rep(' ', 3);
            }
        }
        serial_rep(' ', 2);
        serial_write('|');
        for (i = 0; i < 16; ++i) {
            if (i < n) {
                c = p[i];
                if ((c >= ' ') && (c < 0x7F)) {
                    serial_write(c);
                } else {
                    serial_write('.');
                }
            } else {
                serial_write(' ');
            }
        }
        serial_write('|');
        serial_eol();
        p += 16;
        n -= 16;
    }
}

/*
 * Pretty-printed word dump
 */
void
dump_words(const u32* p, int n)
{
    u32 w = (u32)p;
    int i;

    w &= ~0x3;  // round down to nearest 32-bit boundary
    serial_hex8((u8)(w >> 24)); // msb
    serial_hex8((u8)(w >> 16));
    serial_write('_');
    serial_eol();
    while (n > 0) {
        serial_write('_');
        serial_hex8((u8)(w >> 8));
        serial_hex8((u8)(w >> 0)); // lsb
        serial_write(':');
        p = (u32*)w;
        for (i = 0; i < 8; ++i) {
            if (i < n) {
                serial_write(' ');
                serial_hex32(p[i]);
            } else {
                serial_rep(' ', 9);
            }
        }
        serial_eol();
        w += 32;
        n -= 8;
    }
}

/*
 * Dump 256 bytes (handy for asm debugging, just load r0)
 */
void
dump256(void* p)
{
    hexdump((u8*)p, 256);
}

void
dump_block(const u32* p)
{
    int i;

    for (i = 0; i < 8; ++i) {
        serial_write(' ');
        serial_hex32(*p++);
    }
}
#if DUMP_ASCII
void
dump_ascii(const u32* p)
{
    int i;

    for (i = 0; i < 8; ++i) {
        serial_write(' ');
        u8* q = (u8*)p++;
        for (int j = 0; j < 4; ++j) {
            u8 c = *q++;
            if ((c >= ' ') && (c < 0x7F)) {
                serial_write(c);
                serial_write(' ');
            } else if (c == '\0') {
                serial_puts("\\0");
            } else if (c == '\n') {
                serial_puts("\\n");
            } else if (c == '\r') {
                serial_puts("\\r");
            } else if (c == '\t') {
                serial_puts("\\t");
            } else {
                serial_puts(". ");
            }
        }
    }
}
#endif
/*
@ 12345678 12345678 12345678 12345678 12345678 12345678 12345678 12345678
 \_ 12345678 12345678 12345678 12345678 12345678 12345678 12345678 12345678
*/
void
dump_event(const u32* p)
{
    serial_write('@');
    dump_block(p);
    serial_eol();
#if DUMP_ASCII
    serial_write(' ');
    dump_ascii(p);
    serial_eol();
#endif
    if (((*p) > 0x8000) && ((*p) < 0x10000000)) {
        serial_puts(" \\_");
        dump_block((const u32*)(*p));
        serial_eol();
#if DUMP_ASCII
        serial_puts("   ");
        dump_ascii((const u32*)(*p));
        serial_eol();
#endif
    }
}

/*
 * Display time (or elapsed time) value
 */
void
report_time(u32 t)
{
    serial_puts("time ");
    serial_dec32(t);
    serial_puts("us");
    serial_eol();
}

/*
 * Traditional "cooked" single-character output
 */
int
putchar(int c)
{
    if (c == '\n') {
        serial_eol();
    } else {
        serial_write(c);
    }
    return c;
}

/*
 * Traditional "cooked" string output
 */
void
puts(char* s)
{
    int c;

    while ((c = *s++) != '\0') {
        putchar(c);
    }
}

/*
 * Single-character "cooked" input (unbuffered)
 */
static int
_getchar()
{
    int c;

    c = serial_read();
    if (c == '\r') {
        c = '\n';
    }
    return c;
}

/*
 * Traditional single-character input (buffered)
 */
int
getchar()
{
    char* editline();

    while (linepos >= linelen) {
        editline();
    }
    return linebuf[linepos++];
}

/*
 * Get single line of edited input
 */
char*
editline()
{
    int c;

    linelen = 0;  // reset write position
    while (linelen < (sizeof(linebuf) - 1)) {
        c = _getchar();
        if (c == '\b') {
            if (--linelen < 0) {
                linelen = 0;
                continue;  // no echo
            }
            putchar(c);
            putchar(' ');  // erase previous character
        } else {
            linebuf[linelen++] = c;
        }
        if (c == '\r') {
            putchar(c);
            c = '\n';  // convert CR to LF
        }
        putchar(c);  // echo input
        if (c == '\n') {
            break;  // end-of-line
        }
    }
    linebuf[linelen] = '\0';  // ensure NUL termination
    linepos = 0;  // reset read position
    return linebuf;
}

/*
 * Wait for whitespace character from keyboard
 */
int
wait_for_kb()
{
    int c;

    for (;;) {
        c = _getchar();
        if ((c == '\r') || (c == '\n') || (c == ' ')) {
            return c;
        }
    }
}

#define KERNEL_ADDR     (0x00008000)
#define UPLOAD_ADDR     (0x00010000)
#define UPLOAD_LIMIT    (0x00007F00)

/*
 * Simple bootstrap monitor
 */
void
monitor()
{
    int c;
    int z = 0;
    int len = 0;

    // display banner
    serial_eol();
    serial_puts("^D=exit-monitor ^Z=toggle-hexadecimal ^L=xmodem-upload");
    serial_eol();

    // echo console input to output
    for (;;) {
        if (z) {  // "raw" mode
            c = serial_read();
            serial_hex8(c);  // display as hexadecimal value
            serial_write('=');
            if ((c > 0x20) && (c < 0x7F)) {  // echo printables
                serial_write(c);
            } else {
                serial_write(' ');
            }
            serial_write(' ');
        } else {  // "cooked" mode
            c = _getchar();
            putchar(c);
        }
        if (c == 0x04) {  // ^D to exit monitor loop
            break;
        }
        if (c == 0x1A) {  // ^Z toggle hexadecimal substitution
            z = !z;
        }
        if (c == 0x0C) {  // ^L xmodem file upload
            serial_eol();
            serial_puts("START XMODEM...");
            len = rcv_xmodem((u8*)UPLOAD_ADDR, UPLOAD_LIMIT);
            putchar(wait_for_kb());
            if (len < 0) {
                serial_puts("UPLOAD FAILED!");
                serial_eol();
            } else {
                hexdump((u8*)UPLOAD_ADDR, 128);  // show first block
                serial_rep('.', 3);
                serial_eol();
                hexdump((u8*)UPLOAD_ADDR + (len - 128), 128);  // and last block
                serial_dec32(len);
                serial_puts(" BYTES RECEIVED.");  // and length
                serial_eol();
                serial_puts("^W=boot-uploaded-image");
                serial_eol();
            }
        }
        if ((c == 0x17) && (len > 0)) {  // ^W copy upload and boot
            serial_eol();
            BRANCH_TO(UPLOAD_ADDR);  // should not return...
        }
    }
    serial_eol();
    serial_puts("OK ");
}

static void
clear_bss()
{
    u32* p = (u32*)bss_start;
    while (p < (u32*)heap_start) {
        *p++ = 0;
    }
}

/*
 * Entry point for C code
 */
void
k_start(u32 sp)
{
    extern ACTOR a_poll;
    extern ACTOR a_test;
    extern ACTOR a_bench;
    extern ACTOR a_kernel_repl;
    extern ACTOR a_exit;

    // device initialization
    timer_init();
    serial_init();

    // wait for initial interaction
    serial_puts(";-) ");
    putchar(wait_for_kb());

    // display banner
    serial_puts("mycelia-pi1b 0.1.30 ");
    serial_puts("sp=0x");
    serial_hex32(sp);
#if 0
    serial_puts(" bss=0x");
    serial_hex32((u32)bss_start);
#endif
    serial_puts(" heap=0x");
    serial_hex32((u32)heap_start);
    serial_eol();

    clear_bss();

    for (;;) {
        // display menu
        serial_eol();
        serial_puts("Choose your adventure:");
        serial_eol();
        serial_puts("  1. Monitor");
        serial_eol();
        serial_puts("  2. Console echo");
        serial_eol();
        serial_puts("  3. Unit tests");
        serial_eol();
        serial_puts("  4. Benchmark");
        serial_eol();
        serial_puts("  5. Kernel REPL");
        serial_eol();
        serial_puts("  9. Exit");
        serial_eol();

        // execute selected option
        switch (_getchar()) {
            case '1': {
                monitor();
                break;
            }
            case '2': {
                mycelia(&sponsor_1, &a_poll, 0);
                break;
            }
            case '3': {
                timer_start();
                mycelia(&sponsor_0, &a_test, (u32)&dump_event);
                report_time(timer_stop());
                break;
            }
            case '4': {
                timer_start();
                mycelia(&sponsor_1, &a_bench, 0);  // fast sponsor (no tracing)
//                mycelia(&sponsor_0, &a_bench, 0);  // default sponsor, tracing off
                report_time(timer_stop());
                break;
            }
            case '5': {
                mycelia(&sponsor_0, &a_kernel_repl, 0);
//                mycelia(&sponsor_2, &a_kernel_repl, (u32)&dump_event);
                break;
            }
            case '9': {
                mycelia(&sponsor_1, &a_exit, 0);
                break;
            }
        }
    }
}
