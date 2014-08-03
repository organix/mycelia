/*
 * raspi.h -- Raspberry Pi kernel definitions
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
#ifndef _RASPI_H_
#define _RASPI_H_

#include "raspi.h"

typedef unsigned char u8;
typedef unsigned int u32;

typedef void (ACTOR)(void);

/* Declare kernel entry-point */
extern void mycelia(ACTOR start, u32 trace);

/* Declare ARM assembly-language helper functions */
extern void PUT_32(u32 addr, u32 data);
extern u32 GET_32(u32 addr);
extern void NO_OP();
extern void SPIN(u32 count);
extern void BRANCH_TO(u32 addr);

/* Macros to enhance efficiency */
#define PUT_32(addr, data)      (*((volatile u32*)(addr)) = (data))
#define GET_32(addr)            (*((volatile u32*)(addr)))

#endif /* _RASPI_H_ */
