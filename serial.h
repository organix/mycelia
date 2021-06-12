/*
 * serial.h -- Raspberry Pi serial i/o (UART) routines written in C
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
#ifndef _SERIAL_H_
#define _SERIAL_H_

#include "raspi.h"

extern void     serial_init();              /* initialize serial UART */
extern int      serial_in_ready();          /* input ready != 0, wait == 0 */
extern int      serial_in();                /* raw input from serial port */
extern void     serial_in_flush();          /* consume input until !ready */
extern int      serial_out_ready();         /* output ready != 0, wait == 0 */
extern int      serial_out(u8 data);        /* raw output to serial port */

extern int      serial_read();              /* blocking read from serial port */
extern int      serial_write(u8 data);      /* blocking write to serial port */

extern void     serial_puts(char* s);       /* print C-string to serial port */
extern void     serial_rep(int c, int n);   /* print n repetitions of c */
extern void     serial_eol();               /* print end-of-line */

#endif /* _SERIAL_H_ */
