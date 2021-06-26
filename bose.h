/*
 * bose.h -- Binary Octet-Stream Encoding
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
#ifndef _BOSE_H_
#define _BOSE_H_

#include "raspi.h"

#ifdef true
#error conflicting definition for `true`
#endif

#ifdef false
#error conflicting definition for `false`
#endif

typedef enum { /*2#_000*/ /*2#_001*/ /*2#_010*/ /*2#_011*/ /*2#_100*/ /*2#_101*/ /*2#_110*/ /*2#_111*/
/*2#00000_*/   false,     true,      array_0,   object_0,  array,     object,    array_n,   object_n,
/*2#00001_*/   octets,    mem_ref,   utf8,      utf8_mem,  utf16,     utf16_mem, s_encoded, string_0,
/*2#00010_*/   p_int_0,   p_int_1,   p_int_2,   p_int_3,   p_int_4,   p_int_5,   p_int_6,   p_int_7,
/*2#00011_*/   m_int_0,   m_int_1,   m_int_2,   m_int_3,   m_int_4,   m_int_5,   m_int_6,   m_int_7,
/*2#00100_*/   p_dec_0,   p_dec_1,   p_dec_2,   p_dec_3,   p_dec_4,   p_dec_5,   p_dec_6,   p_dec_7,
/*2#00101_*/   m_dec_0,   m_dec_1,   m_dec_2,   m_dec_3,   m_dec_4,   m_dec_5,   m_dec_6,   m_dec_7,
/*2#00110_*/   p_base_0,  p_base_1,  p_base_2,  p_base_3,  p_base_4,  p_base_5,  p_base_6,  p_base_7,
/*2#00111_*/   m_base_0,  m_base_1,  m_base_2,  m_base_3,  m_base_4,  m_base_5,  m_base_6,  m_base_7,
/*2#01000_*/   n_m64,     n_m63,     n_m62,     n_m61,     n_m60,     n_m59,     n_m58,     n_m57,
/*2#01001_*/   n_m56,     n_m55,     n_m54,     n_m53,     n_m52,     n_m51,     n_m50,     n_m49,
/*2#01010_*/   n_m48,     n_m47,     n_m46,     n_m45,     n_m44,     n_m43,     n_m42,     n_m41,
/*2#01011_*/   n_m40,     n_m39,     n_m38,     n_m37,     n_m36,     n_m35,     n_m34,     n_m33,
/*2#01100_*/   n_m32,     n_m31,     n_m30,     n_m29,     n_m28,     n_m27,     n_m26,     n_m25,
/*2#01101_*/   n_m24,     n_m23,     n_m22,     n_m21,     n_m20,     n_m19,     n_m18,     n_m17,
/*2#01110_*/   n_m16,     n_m15,     n_m14,     n_m13,     n_m12,     n_m11,     n_m10,     n_m9,
/*2#01111_*/   n_m8,      n_m7,      n_m6,      n_m5,      n_m4,      n_m3,      n_m2,      n_m1,
/*2#10000_*/   n_0,       n_1,       n_2,       n_3,       n_4,       n_5,       n_6,       n_7,
/*2#10001_*/   n_8,       n_9,       n_10,      n_11,      n_12,      n_13,      n_14,      n_15,
/*2#10010_*/   n_16,      n_17,      n_18,      n_19,      n_20,      n_21,      n_22,      n_23,
/*2#10011_*/   n_24,      n_25,      n_26,      n_27,      n_28,      n_29,      n_30,      n_31,
/*2#10100_*/   n_32,      n_33,      n_34,      n_35,      n_36,      n_37,      n_38,      n_39,
/*2#10101_*/   n_40,      n_41,      n_42,      n_43,      n_44,      n_45,      n_46,      n_47,
/*2#10110_*/   n_48,      n_49,      n_50,      n_51,      n_52,      n_53,      n_54,      n_55,
/*2#10111_*/   n_56,      n_57,      n_58,      n_59,      n_60,      n_61,      n_62,      n_63,
/*2#11000_*/   n_64,      n_65,      n_66,      n_67,      n_68,      n_69,      n_70,      n_71,
/*2#11001_*/   n_72,      n_73,      n_74,      n_75,      n_76,      n_77,      n_78,      n_79,
/*2#11010_*/   n_80,      n_81,      n_82,      n_83,      n_84,      n_85,      n_86,      n_87,
/*2#11011_*/   n_88,      n_89,      n_90,      n_91,      n_92,      n_93,      n_94,      n_95,
/*2#11100_*/   n_96,      n_97,      n_98,      n_99,      n_100,     n_101,     n_102,     n_103,
/*2#11101_*/   n_104,     n_105,     n_106,     n_107,     n_108,     n_109,     n_110,     n_111,
/*2#11110_*/   n_112,     n_113,     n_114,     n_115,     n_116,     n_117,     n_118,     n_119,
/*2#11111_*/   n_120,     n_121,     n_122,     n_123,     n_124,     n_125,     n_126,     null
} bose_pfx_t;  // prefix octet values

#define SMOL_MIN (-64)
#define SMOL_MAX (126)
#define INT2SMOL(n) (u8)(n_0 + (n))
#define SMOL2INT(b) (int)((b) - n_0)

// print an arbitrary BOSE-encoded value
extern int      print_bose(u8** data_ref, int indent, int limit);

/*
 * symbols from `cal.s`
 */
extern ACTOR    b_value;

extern ACTOR    v_null;
extern ACTOR    v_false;
extern ACTOR    v_true;
extern ACTOR    v_number_0;
extern ACTOR    v_string_0;
extern ACTOR    v_array_0;
extern ACTOR    v_object_0;

extern ACTOR*   new_u32(u32 value);
extern ACTOR*   new_i32(int value);
extern ACTOR*   new_octets(u8* s, u32 n);

/*
 * symbols from `bose.c`
 */
extern int      decode_int(int* result, ACTOR* it);

extern ACTOR*   string_iterator(ACTOR* s);
extern u32      next_character(ACTOR* it);  // or EOF

extern ACTOR*   new_array();
extern ACTOR*   array_insert(ACTOR* a, u32 index, ACTOR* element);
extern ACTOR*   array_element(ACTOR* a, u32 index);

extern ACTOR*   new_object();
extern ACTOR*   object_set(ACTOR* o, ACTOR* key, ACTOR* value);
extern ACTOR*   object_get(ACTOR* o, ACTOR* key);

extern ACTOR*   collection_iterator(ACTOR* c);
extern ACTOR*   next_item(ACTOR* it);  // or NULL

extern int      to_JSON(ACTOR* a, int indent, int limit);

#define new_literal(c_str)          (new_octets((u8*)(c_str), (u32)(sizeof(c_str) - 1)))
#define array_element_count(a)      (((struct example_5*)(a))->data_08 >> 2)
#define object_property_count(o)    (((struct example_5*)(o))->data_08 >> 3)

#endif /* _BOSE_H_ */
