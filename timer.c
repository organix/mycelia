/*
 * timer.c -- Raspberry Pi timer routines written in C
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
 *
 * Some of this code was inspired by bare-metal examples
 * from David Welch at https://github.com/dwelch67/raspberrypi
 */
#include "timer.h"

struct timer {
    u32         _00;
    u32         _04;
    u32         CTL;    //_08;
    u32         _0c;
    u32         _10;
    u32         _14;
    u32         _18;
    u32         _1c;
    u32         CNT;    //_20;
    u32         _24;
    u32         _28;
    u32         _2c;
};
#define TIMER           ((volatile struct timer *)0x2000b400)

/*
 * Initialize 1Mhz timer
 */
void
timer_init()
{
    TIMER->CTL = 0x00F90000;    // 0xF9+1 = 250
    TIMER->CTL = 0x00F90200;    // 250MHz/250 = 1MHz
}

/*
 * Get 1Mhz timer tick count (microseconds)
 */
int
timer_usecs()
{
    return TIMER->CNT;
}

/*
 * Delay loop (microseconds)
 */
int
timer_wait(int dt)
{
    int t0;
    int t1;

    t0 = timer_usecs();
    t1 = t0 + dt;
    for (;;) {
        t0 = timer_usecs();
        if ((t0 - t1) >= 0) {  // timeout
            return t0;
        }
    }
}

static int t0;
static int t1;

/*
 * Record starting time
 */
int
timer_start()
{
    t1 = t0 = timer_usecs();
    return t0;
}

/*
 * Time since start
 */
int
timer_split()
{
    t1 = timer_usecs();
    return t1 - t0;
}

/*
 * Time since last lap or start
 */
int
timer_lap()
{
    int t0 = t1;
    t1 = timer_usecs();
    return t1 - t0;
}

/*
 * Total time, start reset
 */
int
timer_stop()
{
    t1 = timer_usecs();
    int dt = t1 - t0;
    t0 = t1;
    return dt;
}
