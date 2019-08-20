#ifndef QBITMACROS_H
    #define QBITMACROS_H

    #include "qtypes.h"    

    #define qBitsSet(Register, Bits)                    (Register) |= (Bits)
    #define qBitsClear(Register, Bits)                  (Register) &= ~(Bits)
    #define qBitSet(Register, Bit)                      (Register) |= (1 << (Bit))
    #define qBitClear(Register, Bit)                    (Register) &= (~(1<< (Bit)))
    #define qBitRead(Register,Bit)                      ((qFalse == ((Register)& (1<<(Bit))))? qFalse : qTrue)
    #define qBitToggle(Register,Bit)                    ((Register)^= (1<<(Bit)))
    #define qBitWrite(Register, Bit, Value)             ((Value) ? qBitSet((Register),(Bit)) : qBitClear((Register),(Bit)))
    #define qBitMakeByte(b7,b6,b5,b4,b3,b2,b1,b0)       (uint8_t)( ((b7)<<7) + ((b6)<<6) + ((b5)<<5) + ((b4)<<4) + ((b3)<<3) + ((b2)<<2) + ((b1)<<1) + ((b0)<<0) )
    #define qByteMakeFromBits(b7,b6,b5,b4,b3,b2,b1,b0)  (qBitMakeByte((b7),(b6),(b5),(b4),(b3),(b2),(b1),(b0)))
    #define qByteHighNibble(Register)                   ((uint8_t)((Register)>>4))
    #define qByteLowNibble(Register)                    ((uint8_t)((Register)&0x0F))
    #define qByteMergeNibbles(H,L)                      ((uint8_t)(((H)<<4)|(0x0F&(L))))    
    #define qWordHighByte(Register)                     ((uint8_t)((Register)>>8))
    #define qWordLowByte(Register)                      ((uint8_t)((Register)&0x00FF))
    #define qWordMergeBytes(H,L)                        ((uint16_t)(((H)<<8)|(L)))
    #define qDWordHighWord(Register)                    ((uint16_t)((Register) >> 16))
    #define qDWordLowWord(Register)                     ((uint16_t)((Register) & 0xFFFF))
    #define qDWordMergeWords(H,L)                       ((uint32_t)(((uint32_t)(H) << 16 ) | (L) ) )

    #define qClip(X, Max, Min)                          (((X) < (Min)) ? (Min) : (((X) > (Max)) ? (Max) : (X)))
    #define qClipUpper(X, Max)                          (((X) > (Max)) ? (Max) : (X))
    #define qClipLower(X, Min)                          (((X) < (Min)) ? (Min) : (X))
    #define qIsBetween(X, Low, High)                    ((qBool_t)((X) >= (Low) && (X) <= (High)))
    #define qMin(a,b)                                   (((a)<(b))?(a):(b))
    #define qMax(a,b)                                   (((a)>(b))?(a):(b))

#endif