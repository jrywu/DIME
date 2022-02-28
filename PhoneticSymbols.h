//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

#ifndef PHONETIC_SYMBOLS_H
#define PHONETIC_SYMBOLS_H
#pragma once


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

#endif