/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2003 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "YAM_error.h"
#include "YAM_locale.h"
#include "YAM_mime.h"
#include "YAM_utilities.h"

/* local */
static int nextcharin(FILE*, BOOL);
static void output64chunk(int, int, int, int, FILE*);
static void almostputc(int, FILE*, struct TranslationTable*, BOOL);
static void uueget(char*, FILE*, int);
static BOOL gettxtline(char*, int, char**);
static BOOL getline(char*, int, FILE*);
static int outdec(char*, FILE*);

static int rfc2047_decode_int(const char *text,
                              int (*func)(const char *, int, const char *, const char *, void *),
		                          void *arg);
static int rfc2047_dec_callback(const char *txt, int len, const char *chset,
                                const char *lang, void *arg);
static char *rfc2047_search_quote(const char **ptr);


/***************************************************************************
 Module: MIME I/O routines
***************************************************************************/

/// Global variables
static const char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char basis_hex[] = "0123456789ABCDEF";

static const unsigned char index_64[128] =
{
  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,62, 255,255,255,63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255,255,255,255,255,255,
	255,0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 255,255,255,255,255,
	255,26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 255,255,255,255,255
};

static const char index_hex[128] = {
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,
   -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

#define hexchar(c)  (((c) > 127) ? -1 : index_hex[(c)])
#define char64(c)   (((c) < 0 || (c) > 127) ? 255 : index_64[(c)])
#define SUMSIZE     64
#define ENC(c)      ((c) ? ((c) & 077) + ' ': '`')

static BOOL InNewline = FALSE;
static BOOL CRpending = FALSE;
///

/// base64encode
// optimized base64 encoding function returning the length of the
// encoded string.
int base64encode(char *to, const unsigned char *from, unsigned int len)
{
  char *fromp = (char*)from;
  char *top = to;
  unsigned char cbyte;
  unsigned char obyte;
  char end[3];

  for (; len >= 3; len -= 3)
  {
    cbyte = *fromp++;
    *top++ = basis_64[(int)(cbyte >> 2)];
    obyte = (cbyte << 4) & 0x30;            /* 0011 0000 */

    cbyte = *fromp++;
    obyte |= (cbyte >> 4);                  /* 0000 1111 */
    *top++ = basis_64[(int)obyte];
    obyte = (cbyte << 2) & 0x3C;            /* 0011 1100 */

    cbyte = *fromp++;
    obyte |= (cbyte >> 6);                  /* 0000 0011 */
    *top++ = basis_64[(int)obyte];
    *top++ = basis_64[(int)(cbyte & 0x3F)]; /* 0011 1111 */
  }

  if(len)
  {
    end[0] = *fromp++;
    if(--len) end[1] = *fromp++; else end[1] = 0;
    end[2] = 0;

    cbyte = end[0];
    *top++ = basis_64[(int)(cbyte >> 2)];
    obyte = (cbyte << 4) & 0x30;            /* 0011 0000 */

    cbyte = end[1];
    obyte |= (cbyte >> 4);
    *top++ = basis_64[(int)obyte];
    obyte = (cbyte << 2) & 0x3C;            /* 0011 1100 */

    if(len) *top++ = basis_64[(int)obyte];
    else *top++ = '=';
    *top++ = '=';
  }
  *top = 0;
  return top - to;
}

///
/// base64decode
// optimized base64 decoing function returning the length of the
// decoded string or -1 on an occurred error
int base64decode(char *to, const unsigned char *from, unsigned int len)
{
	int x, y;
  char *fromp = (char*)from;
  char *top = to;

	while(len > 0)
	{
    len--;
    x = (unsigned char)(*fromp++);
		if(x > 127 || (x = index_64[x]) == 255)
			return -1;

    if(len < 0 || (y = (unsigned char)(*fromp++)) == 0 ||
       y > 127 || (y = index_64[y]) == 255)
    {
			return -1;
    }

    len--;
		*top++ = (x << 2) | (y >> 4);

		if(len > 0)
    {
      len--;
      if((x = (unsigned char)(*fromp++)) == '=')
      {
	  		if(len > 0 && *fromp++ != '=' || *fromp != 0)
  				return -1;

        len--;
	  	}
  		else
      {
		  	if (x > 127 || (x = index_64[x]) == 255)
			  	return -1;

  			*top++ = (y << 4) | (x >> 2);
        if(len > 0)
        {
          len--;
  	  		if ((y = (unsigned char)(*fromp++)) == '=')
          {
		  	  	if(*fromp != 0) return -1;
  		  	}
  	  		else
          {
		  	  	if (y > 127 || (y = index_64[y]) == 255)
			  	  	return -1;

    				*top++ = (x << 6) | y;
	    		}
        }
		  }
    }
	}
	*top = 0;
  if(len != 0) return -1;
	return top - to;
}

///

/// nextcharin
//  Reads next byte from a text files, handles CRLF line breaks
static int nextcharin(FILE *infile, BOOL PortableNewlines)
{
   int c;

   if (!PortableNewlines) return fgetc(infile);
   if (InNewline) { InNewline = FALSE; return 10; }    /***BUG***/
   c = fgetc(infile);
   if (c == '\n') { InNewline = TRUE; return 13; }
   return c;
}

///
/// output64chunk
//  Writes three bytes in base64 format
static void output64chunk(int c1, int c2, int c3, int pads, FILE *outfile)
{
   fputc(basis_64[c1>>2], outfile);
   fputc(basis_64[((c1 & 0x3)<< 4) | ((c2 & 0xF0) >> 4)], outfile);
   switch(pads)
   {
      case 0 :
         fputc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
         fputc(basis_64[c3 & 0x3F], outfile);
         break;
      case 2 :
         fputs("==", outfile);
         break;
      default :
         fputc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
         fputc('=', outfile);
   }
/*
   if (pads == 2) 
   {
      fputs("==", outfile);
   } 
   else if (pads) 
   {
      fputc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
      fputc('=', outfile);
   }
   else 
   {
      fputc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
      fputc(basis_64[c3 & 0x3F], outfile);
   }
*/
}

///
/// to64
//  Encodes a file using base64 format
void to64(FILE *infile, FILE *outfile, BOOL PortableNewlines)
{
   int c1, c2, c3, ct=0;
   InNewline = 0;
   while ((c1 = nextcharin(infile, PortableNewlines)) != -1)
   {
      c2 = nextcharin(infile, PortableNewlines);
      if (c2 == -1)
         output64chunk(c1, 0, 0, 2, outfile);
      else
      {
         c3 = nextcharin(infile, PortableNewlines);
         if (c3 == -1) output64chunk(c1, c2, 0, 1, outfile);
         else          output64chunk(c1, c2, c3, 0, outfile);
      }
      ct += 4;
      if (ct > 71) { fputc('\n', outfile); ct = 0; }
   }
   if (ct) fputc('\n', outfile);
}

///
/// almostputc
//  Writes a bytes, handles line breaks and translation tables
static void almostputc(int c, FILE *outfile, struct TranslationTable *tt, BOOL PortableNewlines)
{
   if (tt) c = (int)tt->Table[(UBYTE)c];
   if (CRpending) 
   {
      if (c == 10) { fputc('\n', outfile); CRpending = FALSE; }
      else
      {
         fputc(13, outfile);
         if (c != 13) { fputc(c, outfile); CRpending = FALSE; }
      }
   } 
   else 
      if (PortableNewlines && c == 13) CRpending = TRUE; else fputc(c, outfile);
}

///
/// from64txt
//  Decodes a string in base64 format
void from64txt(char *src, char *dst, struct TranslationTable *tt)
{
   int c1, c2, c3, c4;
   UBYTE c;

   while ((c1 = (int)*src++))
   {
      if (ISpace((char)c1)) continue;
      do { c2 = (int)*src++; } while (c2 && ISpace((char)c2));
      do { c3 = (int)*src++; } while (c3 && ISpace((char)c3));
      do { c4 = (int)*src++; } while (c4 && ISpace((char)c4));
      if (!c2 || !c3 || !c4) return;
      c1 = char64(c1); c2 = char64(c2); c = (UBYTE)((c1<<2) | ((c2&0x30)>>4));
      *dst++ = tt ? (char)tt->Table[c] : (char)c;
      if (c3 != '=') 
      {
         c3 = char64(c3); c = (UBYTE)(((c2&0XF) << 4) | ((c3&0x3C) >> 2));
         *dst++ = tt ? (char)tt->Table[c] : (char)c;
         if (c4 != '=') 
         {
            c4 = char64(c4); c = (UBYTE)(((c3&0x03) <<6) | c4);
            *dst++ = tt ? (char)tt->Table[c] : (char)c;
         }
      }
   }
}

///
/// from64
//  Decodes a file in base64 format
void from64(FILE *infile, FILE *outfile, struct TranslationTable *tt, BOOL PortableNewlines)
{
   int c1, c2, c3, c4;
   BOOL DataDone = FALSE;

   CRpending = FALSE;
   while ((c1 = fgetc(infile)) != -1) 
   {
      if (ISpace((char)c1)) continue;
      if (DataDone) continue;
      do { c2 = fgetc(infile); } while (c2 != -1 && ISpace((char)c2));
      do { c3 = fgetc(infile); } while (c3 != -1 && ISpace((char)c3));
      do { c4 = fgetc(infile); } while (c4 != -1 && ISpace((char)c4));
      if (c2 == -1 || c3 == -1 || c4 == -1) 
      {
         ER_NewError(GetStr(MSG_ER_UnexpEOFB64), NULL, NULL);
         return;
      }
      if (c1 == '=' || c2 == '=') { DataDone = TRUE; continue; }
      c1 = char64(c1);
      c2 = char64(c2);
      almostputc(((c1<<2) | ((c2&0x30)>>4)), outfile, tt, PortableNewlines);
      if (c3 == '=') 
         DataDone = TRUE;
      else 
      {
         c3 = char64(c3);
         almostputc((((c2&0XF) << 4) | ((c3&0x3C) >> 2)), outfile, tt, PortableNewlines);
         if (c4 == '=') 
            DataDone = 1;
         else  
         {
            c4 = char64(c4);
            almostputc((((c3&0x03) <<6) | c4), outfile, tt, PortableNewlines);
         }
      }
   }
   if (CRpending) fputc(13, outfile);
}

///
/// toqp
//  Encodes a file using quoted-printable format
void toqp(FILE *infile, FILE *outfile)
{
   int c, ct = 0, prevc = 255;

   while ((c = fgetc(infile)) != -1)
   {
      if ((c < 32 && (c != '\n' && c != '\t')) || (c == '=') || (c >= 127) || (ct == 0 && c == '.')) 
      {
         fputc('=', outfile);
         fputc(basis_hex[c>>4], outfile);
         fputc(basis_hex[c&0xF], outfile);
         ct += 3;
         prevc = 'A';
      }
      else if (c == '\n') 
      {
         if (prevc == ' ' || prevc == '\t') 
         {
            fputs("=\n", outfile);
         }
         fputc('\n', outfile);
         ct = 0;
         prevc = c;
      } 
      else 
      {
         if (c == 'F' && prevc == '\n') 
         {
            if ((c = fgetc(infile)) == 'r') 
               if ((c = fgetc(infile)) == 'o') 
                  if ((c = fgetc(infile)) == 'm') 
                     if ((c = fgetc(infile)) == ' ') { fputs("=46rom", outfile);  ct += 6; }
                     else { fputs("From", outfile); ct += 4; }
                  else { fputs("Fro", outfile); ct += 3; }
               else { fputs("Fr", outfile); ct += 2; }
            else { fputc('F', outfile); ++ct; }
            ungetc(c, infile);
            prevc = 'x'; 
         } 
         else
         {
            fputc(c, outfile);
            ++ct;
            prevc = c;
         }
      }
      if (ct > 72) 
      {
         fputs("=\n", outfile);
         ct = 0;
         prevc = '\n';
      }
   }
   if (ct) fputs("=\n", outfile);
}

///
/// fromform
//  Converts an url-encoded file into plain text
void fromform(FILE *infile, FILE *outfile, struct TranslationTable *tt)
{
   unsigned int c;
   while ((c = fgetc(infile)) != -1)
   {
      switch (c)
      {
         case '&': fputc('\n', outfile); break;
         case '%': c = (index_hex[fgetc(infile)]<<4)+index_hex[fgetc(infile)];
                   switch (c)
                   {
                      case '\n': fputs("\n ", outfile); break;
                      case '\r': break;
                      default  : fputc(tt ? (int)tt->Table[(UBYTE)c] : c, outfile); break;
                   }
                   break;
         case '+': fputc(' ', outfile); break;
         case '=': fputs(" = ", outfile); break;
         default : fputc(tt ? (int)tt->Table[(UBYTE)c] : c, outfile); break;
      }
   }
}

///
/// fromqptxt
//  Decodes a string in quoted-printable format
void fromqptxt(char *src, char *dst, struct TranslationTable *tt)
{
   unsigned int c1, c2;
   UBYTE c;

   while ((c1 = *src++))
      if (c1 == '=') 
      {
         c1 = *src++; c2 = *src++;
         c1 = hexchar(c1); c2 = hexchar(c2); c = (UBYTE)(c1<<4 | c2);
         *dst++ = tt ? (char)tt->Table[c] : (char)c;
      }
      else *dst++ = tt ? (char)tt->Table[(UBYTE)c1] : (char)c1;
}

///
/// fromqp
//  Decodes a file in quoted-printable format
void fromqp(FILE *infile, FILE *outfile, struct TranslationTable *tt)
{
   unsigned int c1, c2;
   BOOL neednewline = FALSE;

   while ((c1 = fgetc(infile)) != -1) 
   {
      if (neednewline) { fputc('\n', outfile); neednewline = FALSE; };
      if (c1 == '=')
      {
         c1 = fgetc(infile);
         if (c1 != '\n')
         {
            c2 = fgetc(infile);
            c1 = hexchar(c1);
            c2 = hexchar(c2);
            fputc(tt ? (int)tt->Table[(UBYTE)(c1<<4 | c2)] : c1<<4 | c2, outfile);
         }
      }
      else if (c1 == '\n') neednewline = TRUE;
      else fputc(tt ? (int)tt->Table[(UBYTE)c1] : c1, outfile);
   }
   if (neednewline) fputc('\n', outfile);
}

///
/// DoesNeedPortableNewlines
//  Checks if line breaks must be portable (CRLF)
BOOL DoesNeedPortableNewlines(char *ctype)
{
   if (!strnicmp(ctype, "text", 4)) return TRUE;
   if (!strnicmp(ctype, "message", 7)) return TRUE;
   if (!strnicmp(ctype, "multipart", 9)) return TRUE;
   return FALSE;
}
///

/*** UU encode/decode stuff ***/
/// uueget
//  Decodes four UU encoded bytes
static void uueget(char *ptr, FILE *outfp, int n)
{
   int c1, c2, c3;
   unsigned char p0, p1, p2, p3;

   p0 = (ptr[0] - ' ') & 0x3F;
   p1 = (ptr[1] - ' ') & 0x3F;
   p2 = (ptr[2] - ' ') & 0x3F;
   p3 = (ptr[3] - ' ') & 0x3F;
   c1 = p0 << 2 | p1 >> 4;
   c2 = p1 << 4 | p2 >> 2;
   c3 = p2 << 6 | p3;
   if (n >= 1) fputc(c1, outfp);
   if (n >= 2) fputc(c2, outfp);
   if (n >= 3) fputc(c3, outfp);
}

///
/// gettxtline
//  Reads next line of UU encoded string
static BOOL gettxtline(char *buf, int size, char **rptr)
{
   int c;
   char *ptr = buf;

   for (c = 0; c < size; ++c)buf[c] = ' ';
   do 
   {
      c = (int)**rptr; (*rptr)++;
      if (!c) { *ptr = '\0'; return (BOOL)(ptr == buf); }
      else if (c == '\n' || c == '\r') { *ptr = '\0'; return FALSE; }
      // Emm: I guess the following line was meant to process quoted
      // mails, but it causes file corruption when the '>' is really
      // part of the uuencoding (usually, this happens for the last
      // line).
      //else if (ptr == buf && c == '>') continue;
      else if (size > 0) { *ptr++ = c; size--; }
   } while (TRUE);
   return FALSE;
}

///
/// fromuuetxt
//  Decodes a string in UUE format
void fromuuetxt(char **txt, FILE *outfp)
{
   char buf[SIZE_LINE];

   while (TRUE)
   {
      if (gettxtline(buf, sizeof(buf), txt)) 
      {
         ER_NewError(GetStr(MSG_ER_UnexpEOFUU), NULL, NULL);
         return;
      }        
      if (!strncmp(buf, "begin", 5)) break;
   }  
   while (TRUE) 
   {
      if (gettxtline(buf, sizeof(buf), txt)) 
      {
         ER_NewError(GetStr(MSG_ER_UnexpEOFUU), NULL, NULL);
         return;
      }        
      else if (!strncmp(buf, "end", 5)) break;
      else if (*buf == '\0') continue;
      else 
      {
         int length = (*buf - ' ');
         if (*buf == '`') length = 0;
         if (length < 0 || length > 63) 
            ER_NewError(GetStr(MSG_ER_InvalidLength), (char *)length, NULL);
         else 
         {
            char *ptr = buf + 1;
            while (length > 0) { uueget(ptr, outfp, length); length -= 3; ptr += 4; }
         }
      }
   }
}

///
/// getline
//  Reads next line from a UU encoded file
static BOOL getline(char *buf, int size, FILE *fp)
{
   int c;
   char *ptr = buf;

   for (c = 0; c < size; ++c)buf[c] = ' ';
   do 
   {
      if ((c = fgetc(fp)) == -1) {*ptr = '\0'; return (BOOL)(ptr == buf); }
      else if (c == '\n' || c == '\r') { *ptr = '\0'; return FALSE; }
      // Emm: I guess the following line was meant to process quoted
      // mails, but it causes file corruption when the '>' is really
      // part of the uuencoding (usually, this happens for the last
      // line).
      //else if (ptr == buf && c == '>') continue;
      else if (size > 0) { *ptr++ = c; size--; }
   } while (TRUE);
   return FALSE;
}

///
/// fromuue
//  Decodes a file in UUE format
void fromuue(FILE *infp, FILE *outfp)
{
   char buf[SIZE_LINE];

   while (TRUE) 
   {
      if (getline(buf, sizeof(buf), infp)) 
      {
         ER_NewError(GetStr(MSG_ER_UnexpEOFUU), NULL, NULL);
         return;
      }        
      if (!strncmp(buf, "begin", 5)) break;
   }  
   while (TRUE) 
   {
      if (getline(buf, sizeof(buf), infp)) 
      {
         ER_NewError(GetStr(MSG_ER_UnexpEOFUU), NULL, NULL);
         return;
      }        
      else if (!strncmp(buf, "end", 5)) break;
      else if (*buf == '\0') continue;
      else 
      {
         int length = (*buf - ' ');
         if (*buf == '`') length = 0;
         if (length < 0 || length > 63) 
            ER_NewError(GetStr(MSG_ER_InvalidLength), (char *)length, NULL);
         else 
         {
            char *ptr = buf + 1;
            while (length > 0) { uueget(ptr, outfp, length); length -= 3; ptr += 4; }
         }
      }
   }
}

///
/// outdec
//  Encodes three bytes using UUE format
static int outdec(char *p, FILE *out)
{
   int c1,c2,c3,c4;

   c1 = *p >> 2;
   c2 = ((p[0] << 4) & 060) | ((p[1] >> 4) & 017);
   c3 = ((p[1] << 2) & 074) | ((p[2] >> 6) &  03);
   c4 = p[2] & 077;
   fputc(ENC(c1), out);
   fputc(ENC(c2), out);
   fputc(ENC(c3), out);
   fputc(ENC(c4), out);
   return (p[0]+p[1]+p[2]) % SUMSIZE;
}

///
/// touue
//  Encodes a file using UUE format
void touue(FILE *in, FILE *out)
{
   char buf[80];
   int i,n,checksum;

   for (;;) 
   {
      n = fread(buf, 1, 45, in);
      fputc(ENC(n), out);
      checksum = 0;
      for (i = 0; i < n; i += 3) checksum = (checksum+outdec(&buf[i], out))%SUMSIZE;
      fputc(ENC(checksum), out);
      fputc('\n', out);
      if (n <= 0) break;
   }
}
///

/*** RFC 2047 MIME decoding routines ***/
/// rfc2047_decode()
// decodes a rfc2047 encoded string and eventually translates
// each character of the decoded string according to the provided
// translation table.
struct rfc2047_decode_info
{
  char *dst;
  unsigned int maxlen;
  struct TranslationTable *tt;
};

// this function uses the decode_int() function and attachs a callback
// function to it which will be called each time a token has been
// decoded so that an eventually existing translation table can be used
// to translate the charsets. returns the number of translated chars or
// -1 if an malloc error occurred and -2 if some unknown encoding was
// specified in the text, -3 if the base64 decoding failed.
int rfc2047_decode(char *dst, const char *src, unsigned int maxlen,
                   struct TranslationTable *tt)
{
  int result = 0;
  // pack all the necessary information in the decode_info
  // structure so that the decode function can process it.
  struct rfc2047_decode_info info;
  info.dst    = dst;
  info.maxlen = maxlen;
  info.tt     = tt;

  // call the decode_int function to start decoding our
  // data.
  result = rfc2047_decode_int(src, &rfc2047_dec_callback, &info);
  info.dst[0] = '\0'; // make sure this string is null-terminated

  // on success return the decoded string len.
  if(result > 0)  return (int)(maxlen)-(int)(info.maxlen);
  else            return result;
}

///
/// rfc2047_dec_callback()
// the callback function that is called by the decode_int() function each
// time a string was successfully decoded so that it can be converted.
static int rfc2047_dec_callback(const char *txt, int len, const char *chset,
                                const char *lang, void *arg)
{
  struct rfc2047_decode_info *info = (struct rfc2047_decode_info *)arg;
  BOOL translate = FALSE;

  // before we go on we check wheter we have enough space
  // to put the txt in our destination
  if(info->maxlen-len < 0)
    return -1;

  // so we can savely decrease it.
  info->maxlen -= len;

  // check wheter we have a valid translation table or not
  if(info->tt && info->tt->SourceCharset)
  {
    if(chset)
    {
      if(MatchNoCase((char *)chset, info->tt->SourceCharset))
        translate = TRUE;
    }
    else if(strcmp(info->tt->SourceCharset, "#?") == 0)
    {
      translate = TRUE;
    }
  }

  // if we recognized a valid charset translation, lets start
  if(translate)
  {
    int i;

    // it seems we have a valid translation table, so lets
    // parse through our text and put the translated char in
    // our destination.
    for(i=0; i < len; i++)
    {
      info->dst[i] = info->tt->Table[(UBYTE)(txt[i])];
    }
  }
  else
  {
    // no translation table is available, so lets just
    // copy the decoded stuff in our destination pointer
	  memcpy(info->dst, txt, len);
  }

  // increase the destination pointer so that the
  // next iteration can profit out of it.
  info->dst += len;

  return 0;
}
///
/// rfc2047_decode_int()
// This is the main rcf2047 decoding function. It receives rfc2047-encoded
// text, and a callback function.  The callback function is repeatedly
// called, each time receiving a piece of decoded text.  The decoded
// info includes the text fragment, text length, the input charset, the
// language (if existant) and void pointer to user specific data for
// the processing. If the callback function returns non-zero, rfc2047
// decoding terminates, returning the result code.  Otherwise,
// rfc2047_decode returns 0 after a successfull decoding (-1 if malloc
// failed).
static int rfc2047_decode_int(const char *text,
                              int (*func)(const char *, int, const char *, const char *, void *),
		                          void *arg)
{
  int	rc, result=0;
  int unknown_enc=0;
  int	had_last_word=0;
  const char *p;
  char *chset, *lang;
  char *encoding;
  char *enctext;

  while(text && *text)
  {
    p=text;
    if(text[0] != '=' || text[1] != '?')
    {
      while(*text)
      {
				if (text[0] == '=' && text[1] == '?')
          break;
				
        if(!isspace((int)(unsigned char)*text))
          had_last_word=0;

        ++text;
      }
			
      if(text > p && !had_last_word)
      {
        rc = (*func)(p, (int)(text-p), 0, 0, arg);
        if(rc) return (rc);
      }

      continue;
    }

    text += 2;
    if((chset = rfc2047_search_quote(&text)) == 0)
      return -1;

		if(*text) ++text;
    if((encoding = rfc2047_search_quote(&text)) == 0)
		{
      free(chset);
      return -1;
    }
		
    if(*text) ++text;
    if((enctext = rfc2047_search_quote(&text)) == 0)
		{
      free(encoding);
      free(chset);

      return -1;
    }
		
    if(*text == '?' && text[1] == '=')
      text += 2;
	
    // now we check the encoding string.
    // q/Q defines to quoted-printable decoding
    // b/B defines to base64 encoding
    // and to sort out possible errors we check for the finalizing NUL-byte
    if(encoding[0] != '\0' && encoding[1] == '\0') // have to be 1 char long
    {
      switch(tolower(encoding[0]))
      {
        // we found a quoted-printable encoded rfc2047 compliant string, so
        // lets decode it.
        case 'q':
        {
          char *q, *r;

          for(q=r=enctext; *q; )
          {
            int c;

            if(*q == '=' && q[1] && q[2])
		  	  	{
              char *p1 = strchr(basis_hex, toupper((int)(unsigned char)q[1]));
              char *p2 = strchr(basis_hex, toupper((int)(unsigned char)q[2]));

              *r++ = (char)(p1 ? p1-basis_hex : 0)*16+(p2 ? p2-basis_hex : 0);
              q += 3;

              continue;
            }

            c = *q++;
	  			  if(c == '_') c = ' ';
            *r++ = c ;
          }
          *r=0;
        }
        break;

        // we found a base64 encoded rfc2047 compliant string, so
        // lets decode it.
        case 'b':
    		{
          int res = base64decode(enctext, enctext, strlen(enctext));
          if(res > 0)
            enctext[res] = '\0';
          else
          {
            result = -3; // signal an base64 decoding error.
            unknown_enc = 1;
          }
        }
        break;

        default:
        {
          // if we end up here the probabilty that we have a rfc2047 compliant
          // string with an unknown encoding is somehow high.
          result = -2; // signal unknown encoding to caller
          unknown_enc = 1;
        }
        break;
      }
    }
    else unknown_enc = 1;

    // if no error occurred we are going to call the callback function
    if(unknown_enc == 1)
    {
      rc = (*func)(p, (int)(text-p), 0, 0, arg);
      unknown_enc = 0; // clear it immediatly
    }
    else
    {
      // RFC 2231 language
      lang = strrchr(chset, '*');
      if(lang) *lang++ = 0;

      rc = (*func)(enctext, strlen(enctext), chset, lang, arg);
    }

    // free everything and check if a returncode was given.
    free(enctext);
    free(chset);
    free(encoding);
    if(rc) return rc;

    // Ignore blanks between enc words
		had_last_word=1;
  }

  return result;
}

///
/// rfc2047_search_quote()
//
static char *rfc2047_search_quote(const char **ptr)
{
  const char *p = *ptr;
  char	*s;

  while(**ptr && **ptr != '?')
    ++(*ptr);

  if((s = malloc((size_t)(*ptr - p + 1))) == 0)
    return (0);

  memcpy(s, p, (size_t)(*ptr-p));
  s[*ptr - p]=0;

  return (s);
}

///
