// From https://github.com/nickgammon/MAX7219/MAX7219_font.h
// MAX7219 class
// Author: Nick Gammon
// Date:   17 March 2015
//
// PERMISSION TO DISTRIBUTE
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.




// NOTE blanks ensure line count matches calculated index in code








// bit patterns for the letters / digits
static const unsigned char  HYPHEN = 0b0000001;

unsigned char MAX7219_font [91] = {
   0b0000000, // ' '
   0b0000001, // '!'
   0b0000001, // '"'
   0b0000001, // '#'
   0b0000001, // '$'
   0b0000001, // '%'
   0b0000001, // '&'
   0b0000001, // '''
   0b1001110,       // '('   - same as [
   0b1111000,       // ')'   - same as ]
   0b0000001, // '*'
   0b0000001, // '+'
   0b0000001, // ','
   0b0000001, // '-' - LOL *is* a hyphen
   0b0000000,       // '.'  (done by turning DP on)
   0b0000001, // '/'
   0b1111110,       // '0'
   0b0110000,       // '1'
   0b1101101,       // '2'
   0b1111001,       // '3'
   0b0110011,       // '4'
   0b1011011,       // '5'
   0b1011111,       // '6'
   0b1110000,       // '7'
   0b1111111,       // '8'
   0b1111011,       // '9'
   0b0000001, // ':'
   0b0000001, // ';'
   0b0000001, // '<'
   0b0000001, // '='
   0b0000001, // '>'
   0b0000001, // '?'
   0b0000001, // '@'
   0b1110111,       // 'A'
   0b0011111,       // 'B'
   0b1001110,       // 'C'  
   0b0111101,       // 'D'
   0b1001111,       // 'E'
   0b1000111,       // 'F'
   0b1011110,       // 'G'
   0b0110111,       // 'H'
   0b0110000,       // 'I' - same as 1
   0b0111100,       // 'J'  
   0b0000001, // 'K'
   0b0001110,       // 'L'
   0b0000001, // 'M'
   0b0010101,       // 'N'
   0b1111110,       // 'O' - same as 0
   0b1100111,       // 'P'
   0b0000001, // 'Q'
   0b0000101,       // 'R'
   0b1011011,       // 'S'
   0b0000111,       // 'T'
   0b0111110,       // 'U'
   0b0000001, // 'V'
   0b0000001, // 'W'
   0b0000001, // 'X'
   0b0100111,       // 'Y'
   0b0000001, // 'Z'
   0b1001110,       // '['  - same as C  
   0b0000001, // backslash
   0b1111000,       // ']' 
   0b0000001, // '^'
   0b0001000,       // '_'
   0b0000001, // '`'
   0b1111101,       // 'a'
   0b0011111,       // 'b'
   0b0001101,       // 'c'
   0b0111101,       // 'd'
   0b1001111,       // 'e'
   0b1000111,       // 'f'
   0b1011110,       // 'g'
   0b0010111,       // 'h'
   0b0010000,       // 'i' 
   0b0111100,       // 'j'
   0b0000001, // 'k'
   0b0001110,       // 'l'
   0b0000001, // 'm'
   0b0010101,       // 'n'
   0b1111110,       // 'o' - same as 0
   0b1100111,       // 'p'
   0b1110011, 		// 'q'
   0b0000101,       // 'r'
   0b1011011,       // 's'
   0b0001111,       // 't'
   0b0011100,       // 'u'
   0b0000001, // 'v'
   0b0000001, // 'w'
   0b0000001, // 'x'
   0b0100111,       // 'y'
   0b0000001, // 'z'
};  //  end of MAX7219_font
