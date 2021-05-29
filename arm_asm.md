# ARM Assembly Language

This dialect of [Kernel](shutt.md) runs on a 32-bit ARM processor.
In order to support development of native code,
we define a set of combiners
which help in the construction of ARM machine instructions.
Each machine instruction is represented by
a 32-bit Kernel _Number_.

## Condition Field

All instructions can be made to execute conditionally
by wrapping them with an appropriate condition field check.

```
($define! arm-cond-eq
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x0000_0000)
  ))
($define! arm-cond-ne
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x1000_0000)
  ))
($define! arm-cond-cs
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x2000_0000)
  ))
($define! arm-cond-cc
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x3000_0000)
  ))
($define! arm-cond-mi
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x4000_0000)
  ))
($define! arm-cond-pl
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x5000_0000)
  ))
($define! arm-cond-vs
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x6000_0000)
  ))
($define! arm-cond-vc
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x7000_0000)
  ))
($define! arm-cond-hi
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x8000_0000)
  ))
($define! arm-cond-ls
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #x9000_0000)
  ))
($define! arm-cond-ge
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #xa000_0000)
  ))
($define! arm-cond-lt
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #xb000_0000)
  ))
($define! arm-cond-gt
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #xc000_0000)
  ))
($define! arm-cond-le
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #xd000_0000)
  ))
($define! arm-cond-al  ; note: this is the default value (always)
  ($lambda (inst)
    (bit-or (bit-and #x0fff_ffff inst) #xe000_0000)
  ))
```

## Registers

Most instructions take register numbers as parameters.
These definitions give mnemonic names to special registers.

```
($define! arm-sl 10)
($define! arm-fp 11)
($define! arm-ip 12)
($define! arm-sp 13)  ; stack pointer
($define! arm-lr 14)  ; link register
($define! arm-pc 15)  ; program counter
```

## BX Format

```
($define! arm-bx-Rn
  ($lambda (n) (bit-and n #xf)))
($define! arm-bx
  ($lambda (n)
    (bit-or #xe12f_ff10 (arm-bx-Rn n))
  ))
```

## B/BL Format

```
($define! arm-pc-prefetch 8)
($define! arm-b-offset
  ($lambda (offset)
    (bit-and (bit-asr (+ offset arm-pc-prefetch) 2) #x00ff_ffff)
  ))
($define! arm-b
  ($lambda (offset)
    (bit-or #xea00_0000 (arm-b-offset offset))
  ))
($define! arm-bl
  ($lambda (offset)
    (bit-or #xeb00_0000 (arm-b-offset offset))
  ))

($define! arm-beq  ; branch if equal
  ($lambda (offset)
    (arm-cond-eq (arm-b offset))
  ))

($define! arm-bne  ; branch if not equal
  ($lambda (offset)
    (arm-cond-ne (arm-b offset))
  ))
```

## Data Processing Format

```
($define! arm-dp-Rd
  ($lambda (d) (bit-lsl (bit-and d #xf) 12)))
($define! arm-dp-Rn
  ($lambda (n) (bit-lsl (bit-and n #xf) 16)))

($define! arm-and        ; Rd := Rn & Op2
  ($lambda (d n op2)
    (bit-or #xe000_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-ands       ; conds(Rd := Rn & Op2)
  ($lambda (d n op2)
    (bit-or #xe010_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-eor        ; Rd := Rn ^ Op2
  ($lambda (d n op2)
    (bit-or #xe020_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-eors       ; conds(Rd := Rn ^ Op2)
  ($lambda (d n op2)
    (bit-or #xe030_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-sub        ; Rd := Rn - Op2
  ($lambda (d n op2)
    (bit-or #xe040_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-subs       ; conds(Rd := Rn - Op2)
  ($lambda (d n op2)
    (bit-or #xe050_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-rsb        ; Rd := Op2 - Rn
  ($lambda (d n op2)
    (bit-or #xe060_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-rsbs       ; conds(Rd := Op2 - Rn)
  ($lambda (d n op2)
    (bit-or #xe070_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-add        ; Rd := Rn + Op2
  ($lambda (d n op2)
    (bit-or #xe080_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-adds       ; conds(Rd := Rn + Op2)
  ($lambda (d n op2)
    (bit-or #xe090_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-adc        ; Rd := Rn + Op2 + C
  ($lambda (d n op2)
    (bit-or #xe0a0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-adcs       ; conds(Rd := Rn + Op2 + C)
  ($lambda (d n op2)
    (bit-or #xe0b0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-sbc        ; Rd := Rn - Op2 + C - 1
  ($lambda (d n op2)
    (bit-or #xe0c0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-sbcs       ; conds(Rd := Rn - Op2 + C - 1)
  ($lambda (d n op2)
    (bit-or #xe0d0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-rsc        ; Rd := Op2 - Rn + C - 1
  ($lambda (d n op2)
    (bit-or #xe0e0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-rscs       ; conds(Rd := Op2 - Rn + C - 1)
  ($lambda (d n op2)
    (bit-or #xe0f0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-tst        ; conds(Rn & Op2)
  ($lambda (n op2)
    (bit-or #xe110_0000 (arm-dp-Rn n) op2)))

($define! arm-teq        ; conds(Rn ^ Op2)
  ($lambda (n op2)
    (bit-or #xe130_0000 (arm-dp-Rn n) op2)))

($define! arm-cmp        ; conds(Rn - Op2)
  ($lambda (n op2)
    (bit-or #xe150_0000 (arm-dp-Rn n) op2)))

($define! arm-cmn        ; conds(Rn + Op2)
  ($lambda (n op2)
    (bit-or #xe170_0000 (arm-dp-Rn n) op2)))

($define! arm-orr        ; Rd := Rn | Op2
  ($lambda (d n op2)
    (bit-or #xe180_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-orrs       ; conds(Rd := Rn | Op2)
  ($lambda (d n op2)
    (bit-or #xe190_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-mov        ; Rd := Op2
  ($lambda (d op2)
    (bit-or #xe1a0_0000 (arm-dp-Rd d) op2)))
($define! arm-movs       ; conds(Rd := Op2)
  ($lambda (d op2)
    (bit-or #xe1b0_0000 (arm-dp-Rd d) op2)))

($define! arm-bic        ; Rd := Rn & ~Op2
  ($lambda (d n op2)
    (bit-or #xe1c0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))
($define! arm-bics       ; conds(Rd := Rn & ~Op2)
  ($lambda (d n op2)
    (bit-or #xe1d0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))

($define! arm-mvn        ; Rd := Op2
  ($lambda (d op2)
    (bit-or #xe1e0_0000 (arm-dp-Rd d) op2)))
($define! arm-mvns       ; conds(Rd := Op2)
  ($lambda (d op2)
    (bit-or #xe1f0_0000 (arm-dp-Rd d) op2)))
```

```
; Op2 = immediate
($define! arm-dp-imm
  ($lambda (imm)
    (bit-or (bit-and imm #xff) #x0200_0000)))
; #b0000 = #x0 = ror 0  = #b0000_0000_0000_0000_0000_0000_xxxx_xxxx
; #b0001 = #x1 = ror 2  = #bxx00_0000_0000_0000_0000_0000_00xx_xxxx
; #b0010 = #x2 = ror 4  = #bxxxx_0000_0000_0000_0000_0000_0000_xxxx
; #b0011 = #x3 = ror 6  = #bxxxx_xx00_0000_0000_0000_0000_0000_00xx
; #b0100 = #x4 = ror 8  = #bxxxx_xxxx_0000_0000_0000_0000_0000_0000
; #b0101 = #x5 = ror 10 = #b00xx_xxxx_xx00_0000_0000_0000_0000_0000
; #b0110 = #x6 = ror 12 = #b0000_xxxx_xxxx_0000_0000_0000_0000_0000
; #b0111 = #x7 = ror 14 = #b0000_00xx_xxxx_xx00_0000_0000_0000_0000
; #b1000 = #x8 = ror 16 = #b0000_0000_xxxx_xxxx_0000_0000_0000_0000
; #b1001 = #x9 = ror 18 = #b0000_0000_00xx_xxxx_xx00_0000_0000_0000
; #b1010 = #xa = ror 20 = #b0000_0000_0000_xxxx_xxxx_0000_0000_0000
; #b1011 = #xb = ror 22 = #b0000_0000_0000_00xx_xxxx_xx00_0000_0000
; #b1100 = #xc = ror 24 = #b0000_0000_0000_0000_xxxx_xxxx_0000_0000
; #b1101 = #xd = ror 26 = #b0000_0000_0000_0000_00xx_xxxx_xx00_0000
; #b1110 = #xe = ror 28 = #b0000_0000_0000_0000_0000_xxxx_xxxx_0000
; #b1111 = #xf = ror 30 = #b0000_0000_0000_0000_0000_00xx_xxxx_xx00
($define! arm-dp-imm-ror
  ($lambda (imm r)
    (bit-or (bit-and imm #xff) (bit-lsl (bit-and r #x1e) 7) #x0200_0000)))
```

```
; Op2 = register
($define! arm-dp-Rm
  ($lambda (m) (bit-and m #xf)))
; Note LSL #0 is a special case, where the shifter carry out is the old value of the CPSR C flag.
; The contents of Rm are used directly as the second operand.
($define! arm-dp-Rm-lsl-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7))))
($define! arm-dp-Rm-lsr-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0020)))
($define! arm-dp-Rm-asr-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0040)))
($define! arm-dp-Rm-ror-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0060)))
($define! arm-dp-Rm-lsl-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0010)))
($define! arm-dp-Rm-lsr-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0030)))
($define! arm-dp-Rm-asr-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0050)))
($define! arm-dp-Rm-ror-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0070)))
```
