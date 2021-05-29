# ARM Assembly Language

This dialect of [Kernel](shutt.md) runs on a 32-bit ARM processor.
In order to support development of native code,
we define a set of combiners
which help in the construction of ARM machine instructions.
Each machine instruction is represented by
a 32-bit Kernel _Number_.

## Condition Field

```
($define! arm-cond-eq #x0000_0000)
($define! arm-cond-ne #x1000_0000)
($define! arm-cond-cs #x2000_0000)
($define! arm-cond-cc #x3000_0000)
($define! arm-cond-mi #x4000_0000)
($define! arm-cond-pl #x5000_0000)
($define! arm-cond-vs #x6000_0000)
($define! arm-cond-vc #x7000_0000)
($define! arm-cond-hi #x8000_0000)
($define! arm-cond-ls #x9000_0000)
($define! arm-cond-ge #xa000_0000)
($define! arm-cond-lt #xb000_0000)
($define! arm-cond-gt #xc000_0000)
($define! arm-cond-le #xd000_0000)
($define! arm-cond-al #xe000_0000)
```

## Registers

```
($define! arm-sl 10)
($define! arm-fp 11)
($define! arm-ip 12)
($define! arm-sp 13)
($define! arm-lr 14)
($define! arm-pc 15)
```

## BX Format

```
($define! arm-bx-fmt #x012f_ff10)
($define! arm-bx-Rn
  ($lambda (n)
    (bit-and n #xf)
  ))
($define! arm-bx
  ($lambda (n)
    (bit-or arm-cond-al arm-bx-fmt (arm-bx-Rn n))
  ))
($define! arm-bx-cond
  ($lambda (cond n)
    (bit-or cond arm-bx-fmt (arm-bx-Rn n))
  ))
```

## B/BL Format

```
($define! arm-b-fmt #x0a00_0000)
($define! arm-bl-fmt #x0b00_0000)
($define! arm-pc-prefetch 8)
($define! arm-b-offset
  ($lambda (offset)
    (bit-and (bit-asr (+ offset arm-pc-prefetch) 2) #x00ff_ffff)
  ))
($define! arm-b
  ($lambda (offset)
    (bit-or arm-b-fmt (arm-b-offset offset))
  ))
($define! arm-bl
  ($lambda (offset)
    (bit-or arm-bl-fmt (arm-b-offset offset))
  ))
($define! arm-b-cond
  ($lambda (cond offset)
    (bit-or cond arm-b-fmt (arm-b-offset offset))
  ))
($define! arm-bl-cond
  ($lambda (cond offset)
    (bit-or cond arm-bl-fmt (arm-b-offset offset))
  ))
```

## Data Processing Format

```
($define! arm-dp-op-and #x0000_0000)  ; Rd := Rn & Op2
($define! arm-dp-op-eor #x0020_0000)  ; Rd := Rn ^ Op2
($define! arm-dp-op-sub #x0040_0000)  ; Rd := Rn - Op2
($define! arm-dp-op-rsb #x0060_0000)  ; Rd := Op2 - Rn
($define! arm-dp-op-add #x0080_0000)  ; Rd := Rn + Op2
($define! arm-dp-op-adc #x00a0_0000)  ; Rd := Rn + Op2 + C
($define! arm-dp-op-sbc #x00c0_0000)  ; Rd := Rn - Op2 + C - 1
($define! arm-dp-op-rsc #x00e0_0000)  ; Rd := Op2 - Rn + C - 1
($define! arm-dp-op-tst #x0100_0000)  ; cond(Rn & Op2)
($define! arm-dp-op-teq #x0120_0000)  ; cond(Rn ^ Op2)
($define! arm-dp-op-cmp #x0140_0000)  ; cond(Rn - Op2)
($define! arm-dp-op-cmn #x0160_0000)  ; cond(Rn + Op2)
($define! arm-dp-op-orr #x0180_0000)  ; Rd := Rn | Op2
($define! arm-dp-op-mov #x01a0_0000)  ; Rd := Op2
($define! arm-dp-op-bic #x01c0_0000)  ; Rd := Rn & ~Op2
($define! arm-dp-op-mvn #x01e0_0000)  ; Rd := ~Op2

($define! arm-dp-conds #x0010_0000)  ; set condition flags

($define! arm-dp-Rd
  ($lambda (d)
    (bit-lsl (bit-and d #xf) 12)
  ))
($define! arm-dp-Rn
  ($lambda (n)
    (bit-lsl (bit-and n #xf) 16)
  ))

($define! arm-and        ; Rd := Rn & Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-and (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-ands       ; conds(Rd := Rn & Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-and (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-and-cond   ; Rd := Rn & Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-and (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-ands-cond  ; conds(Rd := Rn & Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-and (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-eor        ; Rd := Rn ^ Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-eor (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-eors       ; conds(Rd := Rn ^ Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-eor (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-eor-cond   ; Rd := Rn ^ Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-eor (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-eors-cond  ; conds(Rd := Rn ^ Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-eor (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-sub        ; Rd := Rn - Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-sub (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-subs       ; conds(Rd := Rn - Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-sub (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-sub-cond   ; Rd := Rn - Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-sub (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-subs-cond  ; conds(Rd := Rn - Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-sub (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-rsb        ; Rd := Op2 - Rn
  ($lambda (d n op2)
    (bit-or arm-dp-op-rsb (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-rsbs       ; conds(Rd := Op2 - Rn)
  ($lambda (d n op2)
    (bit-or arm-dp-op-rsb (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-rsb-cond   ; Rd := Op2 - Rn iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-rsb (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-rsbs-cond  ; conds(Rd := Op2 - Rn) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-rsb (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-add        ; Rd := Rn + Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-add (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-adds       ; conds(Rd := Rn + Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-add (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-add-cond   ; Rd := Rn + Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-add (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-adds-cond  ; conds(Rd := Rn + Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-add (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-adc        ; Rd := Rn + Op2 + C
  ($lambda (d n op2)
    (bit-or arm-dp-op-adc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-adcs       ; conds(Rd := Rn + Op2 + C)
  ($lambda (d n op2)
    (bit-or arm-dp-op-adc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-adc-cond   ; Rd := Rn + Op2 + C iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-adc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-adcs-cond  ; conds(Rd := Rn + Op2 + C) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-adc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-sbc        ; Rd := Rn - Op2 + C - 1
  ($lambda (d n op2)
    (bit-or arm-dp-op-sbc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-sbcs       ; conds(Rd := Rn - Op2 + C - 1)
  ($lambda (d n op2)
    (bit-or arm-dp-op-sbc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-sbc-cond   ; Rd := Rn - Op2 + C - 1 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-sbc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-sbcs-cond  ; conds(Rd := Rn - Op2 + C - 1) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-sbc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-rsc        ; Rd := Op2 - Rn + C - 1
  ($lambda (d n op2)
    (bit-or arm-dp-op-rsc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-rscs       ; conds(Rd := Op2 - Rn + C - 1)
  ($lambda (d n op2)
    (bit-or arm-dp-op-rsc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-rsc-cond   ; Rd := Op2 - Rn + C - 1 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-rsc (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-rscs-cond  ; conds(Rd := Op2 - Rn + C - 1) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-rsc (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-tst        ; conds(Rn & Op2)
  ($lambda (n op2)
    (bit-or arm-dp-op-tst (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-tst-cond   ; conds(Rn & Op2) iff cond
  ($lambda (cond n op2)
    (bit-or cond arm-dp-op-tst (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-teq        ; conds(Rn ^ Op2)
  ($lambda (n op2)
    (bit-or arm-dp-op-teq (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-teq-cond   ; conds(Rn ^ Op2) iff cond
  ($lambda (cond n op2)
    (bit-or cond arm-dp-op-teq (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-cmp        ; conds(Rn - Op2)
  ($lambda (n op2)
    (bit-or arm-dp-op-cmp (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-cmp-cond   ; conds(Rn - Op2) iff cond
  ($lambda (cond n op2)
    (bit-or cond arm-dp-op-cmp (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-cmn        ; conds(Rn + Op2)
  ($lambda (n op2)
    (bit-or arm-dp-op-cmn (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-cmn-cond   ; conds(Rn + Op2) iff cond
  ($lambda (cond n op2)
    (bit-or cond arm-dp-op-cmn (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-orr        ; Rd := Rn | Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-orr (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-orrs       ; conds(Rd := Rn | Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-orr (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-orr-cond   ; Rd := Rn | Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-orr (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-orrs-cond  ; conds(Rd := Rn | Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-orr (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-mov        ; Rd := Op2
  ($lambda (d op2)
    (bit-or arm-dp-op-mov (arm-dp-Rd d) op2)
  ))
($define! arm-movs       ; conds(Rd := Op2)
  ($lambda (d op2)
    (bit-or arm-dp-op-mov (arm-dp-Rd d) op2 arm-dp-conds)
  ))
($define! arm-mov-cond   ; Rd := Op2 iff cond
  ($lambda (cond d op2)
    (bit-or cond arm-dp-op-mov (arm-dp-Rd d) op2)
  ))
($define! arm-movs-cond  ; conds(Rd := Op2) iff cond
  ($lambda (cond d op2)
    (bit-or cond arm-dp-op-mov (arm-dp-Rd d) op2 arm-dp-conds)
  ))

($define! arm-bic #x01c0_0000)  ; Rd := Rn & ~Op2
($define! arm-bic        ; Rd := Rn & ~Op2
  ($lambda (d n op2)
    (bit-or arm-dp-op-bic (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-bics       ; conds(Rd := Rn & ~Op2)
  ($lambda (d n op2)
    (bit-or arm-dp-op-bic (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))
($define! arm-bic-cond   ; Rd := Rn & ~Op2 iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-bic (arm-dp-Rd d) (arm-dp-Rn n) op2)
  ))
($define! arm-bics-cond  ; conds(Rd := Rn & ~Op2) iff cond
  ($lambda (cond d n op2)
    (bit-or cond arm-dp-op-bic (arm-dp-Rd d) (arm-dp-Rn n) op2 arm-dp-conds)
  ))

($define! arm-mvn #x01e0_0000)  ; Rd := ~Op2
($define! arm-mvn        ; Rd := Op2
  ($lambda (d op2)
    (bit-or arm-dp-op-mvn (arm-dp-Rd d) op2)
  ))
($define! arm-mvns       ; conds(Rd := Op2)
  ($lambda (d op2)
    (bit-or arm-dp-op-mvn (arm-dp-Rd d) op2 arm-dp-conds)
  ))
($define! arm-mvn-cond   ; Rd := Op2 iff cond
  ($lambda (cond d op2)
    (bit-or cond arm-dp-op-mvn (arm-dp-Rd d) op2)
  ))
($define! arm-mvns-cond  ; conds(Rd := Op2) iff cond
  ($lambda (cond d op2)
    (bit-or cond arm-dp-op-mvn (arm-dp-Rd d) op2 arm-dp-conds)
  ))

; Op2 = register
($define! arm-dp-Rm
  ($lambda (m)
    (bit-or (bit-and m #xf))
  ))
; Note LSL #0 is a special case, where the shifter carry out is the old value of the CPSR C flag.
; The contents of Rm are used directly as the second operand.
($define! arm-dp-Rm-lsl-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7))
  ))
($define! arm-dp-Rm-lsr-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0020)
  ))
($define! arm-dp-Rm-asr-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0040)
  ))
($define! arm-dp-Rm-ror-imm
  ($lambda (m imm)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0060)
  ))
($define! arm-dp-Rm-lsl-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0010)
  ))
($define! arm-dp-Rm-lsr-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0030)
  ))
($define! arm-dp-Rm-asr-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0050)
  ))
($define! arm-dp-Rm-ror-Rs
  ($lambda (m s)
    (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0070)
  ))

; Op2 = immediate
($define! arm-dp-imm
  ($lambda (imm)
    (bit-or (bit-and imm #xff) #x0200_0000)
  ))
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
    (bit-or (bit-and imm #xff) (bit-lsl (bit-and r #x1e) 7) #x0200_0000)
  ))
```
