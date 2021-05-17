/*
 * sexpr.h -- LISP/Scheme S-expressions (ala John McCarthy)
 *
 * Copyright 2012-2021 Dale Schumacher
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
#ifndef _SEXPR_H_
#define _SEXPR_H_

#include "raspi.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef EOF
#define EOF (-1)
#endif

extern ACTOR*   parse_sexpr();  /* parse and return s-expression */
extern void     print_sexpr(ACTOR*);  /* print external representation of s-expression */
extern void		kernel_repl();

#endif /* _SEXPR_H_ */
