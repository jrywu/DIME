/* Copyright (C) DAYI software Corpation & Jeremy Wu - All Rights Reserved
* Author:  Jeremy Wu
* Date:    2015-11-7
* File:    PhoneticComposition.h
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* Written by Jeremy Wu <jrywu@msn.com>, October 2015
*/

#ifndef PHONETIC_COMPOSITION_H
#define PHONETIC_COMPOSITION_H

#pragma once

#include "BaseStructure.h"

// Derived from OpenVanilla project Phonetic toolkit
enum PHONETIC_SMBOLS
{
	vpConsonantMask = 0x001F,		// 0000 0000 0001 1111, 21 consonants + 1 wildcard 
	vpMiddleVowelMask = 0x00E0,		// 0000 0000 1110 0000, 3 middle vowels  + 1 wildcard 
	vpVowelMask = 0x0F00,		    // 0000 1111 0000 0000, 13 vowels  + 1 wildcard 
	vpToneMask = 0x7000,		    // 0111 0000 0000 0000, 5 tones (tone1=0x00),  + 1 wildcard 
	//21 consonants + 1 wildcard (vpAnyConsonant)
	vpB = 0x0001, vpP = 0x0002, vpM = 0x0003, vpF = 0x0004,
	vpD = 0x0005, vpT = 0x0006, vpN = 0x0007, vpL = 0x0008,
	vpG = 0x0009, vpK = 0x000a, vpH = 0x000b,
	vpJ = 0x000c, vpQ = 0x000d, vpX = 0x000e,
	vpZH = 0x000f, vpCH = 0x0010, vpSH = 0x0011, vpR = 0x0012,
	vpZ = 0x0013, vpC = 0x0014, vpS = 0x0015, 
	vpAnyConsonant = 0x0016,
	//3 middle vowels + 1 wildcard (vpAnyMidVowel).
	vpI = 0x0020, vpU = 0x0040, vpV = 0x0060, 
	vpAnyMiddleVowel = 0x0080,
	//13 vowels + 1 wildcard (vpAnyVowel).
	vpA = 0x0100, vpO = 0x0200, vpER = 0x0300, vpE = 0x0400,
	vpAI = 0x0500, vpEI = 0x0600, vpAO = 0x0700, vpOU = 0x0800,
	vpAN = 0x0900, vpEN = 0x0A00, vpANG = 0x0B00, vpENG = 0x0C00,
	vpERR = 0x0D00, 
	vpAnyVowel = 0x0E00,
	//5 tones + 1 wildcard
	vpTone1 = 0x0000, vpTone2 = 0x1000, vpTone3 = 0x2000, vpTone4 = 0x3000, vpTone5 = 0x4000, 
	vpAnyTone = 0x5000,
	// wildcard 
	vpAny = 0x8000
};

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