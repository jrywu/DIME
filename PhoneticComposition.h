//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

#ifndef PHONETIC_COMPOSITION_H
#define PHONETIC_COMPOSITION_H

#pragma once

//#include "BaseStructure.h"
#include "PhoneticSymbols.h"

UINT vpStandardKeyTable[MAX_RADICAL+1] =
{
// Skip for 32 control characters in ascii table
// The table contains possible stroke from ascii code 0x20 (0d32) to 0x60 (0d96)

	0,
// '!',  '"',  '#',  '$',  '%',  '&',  ''',  '(',
   0,    0,    0,    0,    0,    0,    0,    0,

// ')',  '*',  '+',  ',',  '-',  '.',  '/',  '0',   
   0,    0,     0,   vpE,  vpERR,vpOU, vpENG,vpAN,  

// '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',
   vpB,  vpD,vpTone3,vpTone4,vpZH,vpTone2,vpTone5,vpA,

// '9',  ':',  ';',  '<',  '=',  '>',  '?',  '@',  
   vpAI, 0,    vpANG,0,    0,    0,    vpAny,    0,

// 'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',
   vpM,  vpR,  vpH,  vpK,  vpG,  vpQ,  vpSH, vpC,

// 'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',
   vpO,  vpU,  vpER, vpAO, vpV,  vpS,  vpEI, vpEN,

// 'Q',  'R',  'S',  'T',  'U',  'V',  'W',  'X',  
   vpP,  vpJ,  vpN,  vpCH, vpI,  vpX,  vpT,  vpL,

// 'Y',  'Z',  '[',  '\',  ']',  '^',  '_',  '`'
   vpZ,  vpF,  0,    0,    0,    0,    0,    0
};
WCHAR vpEtenKeyToStandardKeyTable[MAX_RADICAL + 1] =
{
   0,

// '!',  '"',  '#',  '$',  '%',  '&',  ''',  '(',
   0,    0,    0,    0,    0,    0,    'H',  0,

// ')',  '*',  '+',  ',',  '-',  '.',  '/',  '0',
   0,     0,    0,  '5',  '/',  'V',  'G',  ';',

// '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',
   '7',  '6',  '3',  '4',   0,    0,   'F',  '0',

// '9',  ':',  ';',  '<',  '=',  '>',  '?',  '@',  
   'P',   0,   'Y',   0,   '-',   0,   '?',   0,

// 'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',
   '8',  '1',  'V',  '2',  'U',  'Z',  'R',  'C',

// 'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',
   '9',  'B',  'D',  'X',  'A',  'S',  'I',  'Q',

// 'Q',  'R',  'S',  'T',  'U',  'V',  'W',  'X',  
   'O',  'K',  'N',  'W',  'M',  'E',  ',',  'J',

// 'Y',  'Z',  '[',  '\',  ']',  '^',  '_',  '`'
   '.',  'L',   0,    0,    0,    0,    0,    0
};

const WCHAR vpStandardLayoutCode[42] = {' ', 
										'1', 'q', 'a', 'z', 
										'2', 'w', 's', 'x', 
										'e', 'd', 'c', 
										'r', 'f', 'v', 
										'5', 't', 'g', 'b', 
										'y', 'h', 'n', 
										'u', 'j', 'm', 
										'8', 'i', 'k', ',',
										'9', 'o', 'l', '.', 
										'0', 'p', ';' , '/' , 
										'-', 
										'6', '3', '4', '7' };
#endif