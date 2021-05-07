/*
 * timer.h -- Raspberry Pi timer routines written in C
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
#ifndef _TIMER_H_
#define _TIMER_H_

#include "raspi.h"

#define usecs   /* 1e-6 seconds */
#define msecs   * 1000 usecs
#define secs    * 1000 msecs

extern void     timer_init();                   /* initialize microsecond timer */
extern int      timer_usecs();                  /* read microsecond timer value */
extern int      timer_wait(int dt);             /* wait for dt microseconds */

extern int      timer_start();                  /* record starting time */
extern int      timer_split();                  /* time since start */
extern int      timer_lap();                    /* time since last lap or start */
extern int      timer_stop();                   /* total time, start reset */

#endif /* _TIMER_H_ */
