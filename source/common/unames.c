/*
******************************************************************************
*
*   Copyright (C) 1999-2004, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  unames.c
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 1999oct04
*   created by: Markus W. Scherer
*/

#include "unicode/utypes.h"
#include "unicode/uchar.h"
#include "unicode/udata.h"
#include "ustr_imp.h"
#include "umutex.h"
#include "cmemory.h"
#include "cstring.h"
#include "ucln_cmn.h"
#include "udataswp.h"

/* prototypes ------------------------------------------------------------- */

#define LENGTHOF(array) (sizeof(array)/sizeof((array)[0]))

static const char DATA_NAME[] = "unames";
static const char DATA_TYPE[] = "icu";

#define GROUP_SHIFT 5
#define LINES_PER_GROUP (1UL<<GROUP_SHIFT)
#define GROUP_MASK (LINES_PER_GROUP-1)

typedef struct {
    uint16_t groupMSB,
             offsetHigh, offsetLow; /* avoid padding */
} Group;

typedef struct {
    uint32_t start, end;
    uint8_t type, variant;
    uint16_t size;
} AlgorithmicRange;

typedef struct {
    uint32_t tokenStringOffset, groupsOffset, groupStringOffset, algNamesOffset;
} UCharNames;

typedef struct {
    const char *otherName;
    UChar32 code;
} FindName;

#define DO_FIND_NAME NULL

static UDataMemory *uCharNamesData=NULL;
static UCharNames *uCharNames=NULL;
static UErrorCode gLoadErrorCode=U_ZERO_ERROR;

/*
 * Maximum length of character names (regular & 1.0).
 * Maximum length of ISO comments.
 */
static int32_t gMaxNameLength=0, gMaxISOCommentLength=0;

/*
 * Set of chars used in character names (regular & 1.0).
 * Set of chars used in ISO comments.
 * Chars are platform-dependent (can be EBCDIC).
 */
static uint32_t gNameSet[8]={ 0 }, gISOCommentSet[8]={ 0 };

#define U_NONCHARACTER_CODE_POINT U_CHAR_CATEGORY_COUNT
#define U_LEAD_SURROGATE U_CHAR_CATEGORY_COUNT + 1
#define U_TRAIL_SURROGATE U_CHAR_CATEGORY_COUNT + 2

#define U_CHAR_EXTENDED_CATEGORY_COUNT (U_CHAR_CATEGORY_COUNT + 3)

static const char * const
charCatNames[U_CHAR_EXTENDED_CATEGORY_COUNT];

/* implementation ----------------------------------------------------------- */

UBool
unames_cleanup()
{
    if(uCharNamesData) {
        udata_close(uCharNamesData);
        uCharNamesData = NULL;
    }
    if(uCharNames) {
        uCharNames = NULL;
    }
    gMaxNameLength=0;
    return TRUE;
}

static UBool U_CALLCONV
isAcceptable(void *context,
             const char *type, const char *name,
             const UDataInfo *pInfo) {
    return (UBool)(
        pInfo->size>=20 &&
        pInfo->isBigEndian==U_IS_BIG_ENDIAN &&
        pInfo->charsetFamily==U_CHARSET_FAMILY &&
        pInfo->dataFormat[0]==0x75 &&   /* dataFormat="unam" */
        pInfo->dataFormat[1]==0x6e &&
        pInfo->dataFormat[2]==0x61 &&
        pInfo->dataFormat[3]==0x6d &&
        pInfo->formatVersion[0]==1);
}

static UBool
isDataLoaded(UErrorCode *pErrorCode) {
    /* load UCharNames from file if necessary */
    UBool isCached;

    /* do this because double-checked locking is broken */
    umtx_lock(NULL);
    isCached=uCharNames!=NULL;
    umtx_unlock(NULL);

    if(!isCached) {
        UCharNames *names;
        UDataMemory *data;

        /* check error code from previous attempt */
        if(U_FAILURE(gLoadErrorCode)) {
            *pErrorCode=gLoadErrorCode;
            return FALSE;
        }

        /* open the data outside the mutex block */
        data=udata_openChoice(NULL, DATA_TYPE, DATA_NAME, isAcceptable, NULL, pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            gLoadErrorCode=*pErrorCode;
            return FALSE;
        }

        names=(UCharNames *)udata_getMemory(data);

        /* in the mutex block, set the data for this process */
        {
            umtx_lock(NULL);
            if(uCharNames==NULL) {
                uCharNames=names;
                uCharNamesData=data;
                data=NULL;
                names=NULL;
            }
            umtx_unlock(NULL);
        }

        /* if a different thread set it first, then close the extra data */
        if(data!=NULL) {
            udata_close(data); /* NULL if it was set correctly */
        }
    }
    return TRUE;
}

#define WRITE_CHAR(buffer, bufferLength, bufferPos, c) { \
    if((bufferLength)>0) { \
        *(buffer)++=c; \
        --(bufferLength); \
    } \
    ++(bufferPos); \
}

#define U_ISO_COMMENT U_CHAR_NAME_CHOICE_COUNT

/*
 * Important: expandName() and compareName() are almost the same -
 * apply fixes to both.
 *
 * UnicodeData.txt uses ';' as a field separator, so no
 * field can contain ';' as part of its contents.
 * In unames.dat, it is marked as token[';']==-1 only if the
 * semicolon is used in the data file - which is iff we
 * have Unicode 1.0 names or ISO comments.
 * So, it will be token[';']==-1 if we store U1.0 names/ISO comments
 * although we know that it will never be part of a name.
 */
static uint16_t
expandName(UCharNames *names,
           const uint8_t *name, uint16_t nameLength, UCharNameChoice nameChoice,
           char *buffer, uint16_t bufferLength) {
    uint16_t *tokens=(uint16_t *)names+8;
    uint16_t token, tokenCount=*tokens++, bufferPos=0;
    uint8_t *tokenStrings=(uint8_t *)names+names->tokenStringOffset;
    uint8_t c;

    if(nameChoice==U_UNICODE_10_CHAR_NAME || nameChoice==U_ISO_COMMENT) {
        /*
         * skip the modern name if it is not requested _and_
         * if the semicolon byte value is a character, not a token number
         */
        if((uint8_t)';'>=tokenCount || tokens[(uint8_t)';']==(uint16_t)(-1)) {
            while(nameLength>0) {
                --nameLength;
                if(*name++==';') {
                    break;
                }
            }
            if(nameChoice==U_ISO_COMMENT) {
                /* skip the Unicode 1.0 name as well to get the ISO comment */
                while(nameLength>0) {
                    --nameLength;
                    if(*name++==';') {
                        break;
                    }
                }
            }
        } else {
            /*
             * the semicolon byte value is a token number, therefore
             * only modern names are stored in unames.dat and there is no
             * such requested Unicode 1.0 name here
             */
            nameLength=0;
        }
    }

    /* write each letter directly, and write a token word per token */
    while(nameLength>0) {
        --nameLength;
        c=*name++;

        if(c>=tokenCount) {
            if(c!=';') {
                /* implicit letter */
                WRITE_CHAR(buffer, bufferLength, bufferPos, c);
            } else {
                /* finished */
                break;
            }
        } else {
            token=tokens[c];
            if(token==(uint16_t)(-2)) {
                /* this is a lead byte for a double-byte token */
                token=tokens[c<<8|*name++];
                --nameLength;
            }
            if(token==(uint16_t)(-1)) {
                if(c!=';') {
                    /* explicit letter */
                    WRITE_CHAR(buffer, bufferLength, bufferPos, c);
                } else {
                    /* stop, but skip the semicolon if we are seeking
                       extended names and there was no 2.0 name but there
                       is a 1.0 name. */
                    if(!bufferPos && nameChoice == U_EXTENDED_CHAR_NAME) {
                        if ((uint8_t)';'>=tokenCount || tokens[(uint8_t)';']==(uint16_t)(-1)) {
                            continue;
                        }
                    }
                    /* finished */
                    break;
                }
            } else {
                /* write token word */
                uint8_t *tokenString=tokenStrings+token;
                while((c=*tokenString++)!=0) {
                    WRITE_CHAR(buffer, bufferLength, bufferPos, c);
                }
            }
        }
    }

    /* zero-terminate */
    if(bufferLength>0) {
        *buffer=0;
    }

    return bufferPos;
}

/*
 * compareName() is almost the same as expandName() except that it compares
 * the currently expanded name to an input name.
 * It returns the match/no match result as soon as possible.
 */
static UBool
compareName(UCharNames *names,
            const uint8_t *name, uint16_t nameLength, UCharNameChoice nameChoice,
            const char *otherName) {
    uint16_t *tokens=(uint16_t *)names+8;
    uint16_t token, tokenCount=*tokens++;
    uint8_t *tokenStrings=(uint8_t *)names+names->tokenStringOffset;
    uint8_t c;
    const char *origOtherName = otherName;

    if(nameChoice==U_UNICODE_10_CHAR_NAME) {
        /*
         * skip the modern name if it is not requested _and_
         * if the semicolon byte value is a character, not a token number
         */
        if((uint8_t)';'>=tokenCount || tokens[(uint8_t)';']==(uint16_t)(-1)) {
            while(nameLength>0) {
                --nameLength;
                if(*name++==';') {
                    break;
                }
            }
        } else {
            /*
             * the semicolon byte value is a token number, therefore
             * only modern names are stored in unames.dat and there is no
             * such requested Unicode 1.0 name here
             */
            nameLength=0;
        }
    }

    /* compare each letter directly, and compare a token word per token */
    while(nameLength>0) {
        --nameLength;
        c=*name++;

        if(c>=tokenCount) {
            if(c!=';') {
                /* implicit letter */
                if((char)c!=*otherName++) {
                    return FALSE;
                }
            } else {
                /* finished */
                break;
            }
        } else {
            token=tokens[c];
            if(token==(uint16_t)(-2)) {
                /* this is a lead byte for a double-byte token */
                token=tokens[c<<8|*name++];
                --nameLength;
            }
            if(token==(uint16_t)(-1)) {
                if(c!=';') {
                    /* explicit letter */
                    if((char)c!=*otherName++) {
                        return FALSE;
                    }
                } else {
                    /* stop, but skip the semicolon if we are seeking
                       extended names and there was no 2.0 name but there
                       is a 1.0 name. */
                    if(otherName == origOtherName && nameChoice == U_EXTENDED_CHAR_NAME) {
                        if ((uint8_t)';'>=tokenCount || tokens[(uint8_t)';']==(uint16_t)(-1)) {
                            continue;
                        }
                    }
                    /* finished */
                    break;
                }
            } else {
                /* write token word */
                uint8_t *tokenString=tokenStrings+token;
                while((c=*tokenString++)!=0) {
                    if((char)c!=*otherName++) {
                        return FALSE;
                    }
                }
            }
        }
    }

    /* complete match? */
    return (UBool)(*otherName==0);
}

static const char * const charCatNames[U_CHAR_EXTENDED_CATEGORY_COUNT] = {
    "unassigned",
    "uppercase letter",
    "lowercase letter",
    "titlecase letter",
    "modifier letter",
    "other letter",
    "non spacing mark",
    "enclosing mark",
    "combining spacing mark",
    "decimal digit number",
    "letter number",
    "other number",
    "space separator",
    "line separator",
    "paragraph separator",
    "control",
    "format",
    "private use area",
    "surrogate",
    "dash punctuation",   
    "start punctuation",
    "end punctuation",
    "connector punctuation",
    "other punctuation",
    "math symbol",
    "currency symbol",
    "modifier symbol",
    "other symbol",
    "initial punctuation",
    "final punctuation",
    "noncharacter",
    "lead surrogate",
    "trail surrogate"
};

static uint8_t getCharCat(UChar32 cp) {
    uint8_t cat;

    if (UTF_IS_UNICODE_NONCHAR(cp)) {
        return U_NONCHARACTER_CODE_POINT;
    }

    if ((cat = u_charType(cp)) == U_SURROGATE) {
        cat = UTF_IS_LEAD(cp) ? U_LEAD_SURROGATE : U_TRAIL_SURROGATE;
    }

    return cat;
}

static const char *getCharCatName(UChar32 cp) {
    uint8_t cat = getCharCat(cp);

    /* Return unknown if the table of names above is not up to
       date. */

    if (cat >= LENGTHOF(charCatNames)) {
        return "unknown";
    } else {
        return charCatNames[cat];
    }
}

static uint16_t getExtName(uint32_t code, char *buffer, uint16_t bufferLength) {
    const char *catname = getCharCatName(code);
    uint16_t length = 0;

    UChar32 cp;
    int ndigits, i;
    
    WRITE_CHAR(buffer, bufferLength, length, '<');
    while (catname[length - 1]) {
        WRITE_CHAR(buffer, bufferLength, length, catname[length - 1]);
    }
    WRITE_CHAR(buffer, bufferLength, length, '-');
    for (cp = code, ndigits = 0; cp; ++ndigits, cp >>= 4)
        ;
    if (ndigits < 4)
        ndigits = 4;
    for (cp = code, i = ndigits; (cp || i > 0) && bufferLength; cp >>= 4, bufferLength--) {
        uint8_t v = (uint8_t)(cp & 0xf);
        buffer[--i] = (v < 10 ? '0' + v : 'A' + v - 10);
    }
    buffer += ndigits;
    length += ndigits;
    WRITE_CHAR(buffer, bufferLength, length, '>');

    return length;
}

/*
 * getGroup() does a binary search for the group that contains the
 * Unicode code point "code".
 * The return value is always a valid Group* that may contain "code"
 * or else is the highest group before "code".
 * If the lowest group is after "code", then that one is returned.
 */
static Group *
getGroup(UCharNames *names, uint32_t code) {
    uint16_t groupMSB=(uint16_t)(code>>GROUP_SHIFT),
             start=0,
             limit=*(uint16_t *)((char *)names+names->groupsOffset),
             number;
    Group *groups=(Group *)((char *)names+names->groupsOffset+2);

    /* binary search for the group of names that contains the one for code */
    while(start<limit-1) {
        number=(uint16_t)((start+limit)/2);
        if(groupMSB<groups[number].groupMSB) {
            limit=number;
        } else {
            start=number;
        }
    }

    /* return this regardless of whether it is an exact match */
    return groups+start;
}

/*
 * expandGroupLengths() reads a block of compressed lengths of 32 strings and
 * expands them into offsets and lengths for each string.
 * Lengths are stored with a variable-width encoding in consecutive nibbles:
 * If a nibble<0xc, then it is the length itself (0=empty string).
 * If a nibble>=0xc, then it forms a length value with the following nibble.
 * Calculation see below.
 * The offsets and lengths arrays must be at least 33 (one more) long because
 * there is no check here at the end if the last nibble is still used.
 */
static const uint8_t *
expandGroupLengths(const uint8_t *s,
                   uint16_t offsets[LINES_PER_GROUP+1], uint16_t lengths[LINES_PER_GROUP+1]) {
    /* read the lengths of the 32 strings in this group and get each string's offset */
    uint16_t i=0, offset=0, length=0;
    uint8_t lengthByte;

    /* all 32 lengths must be read to get the offset of the first group string */
    while(i<LINES_PER_GROUP) {
        lengthByte=*s++;

        /* read even nibble - MSBs of lengthByte */
        if(length>=12) {
            /* double-nibble length spread across two bytes */
            length=(uint16_t)(((length&0x3)<<4|lengthByte>>4)+12);
            lengthByte&=0xf;
        } else if((lengthByte /* &0xf0 */)>=0xc0) {
            /* double-nibble length spread across this one byte */
            length=(uint16_t)((lengthByte&0x3f)+12);
        } else {
            /* single-nibble length in MSBs */
            length=(uint16_t)(lengthByte>>4);
            lengthByte&=0xf;
        }

        *offsets++=offset;
        *lengths++=length;

        offset+=length;
        ++i;

        /* read odd nibble - LSBs of lengthByte */
        if((lengthByte&0xf0)==0) {
            /* this nibble was not consumed for a double-nibble length above */
            length=lengthByte;
            if(length<12) {
                /* single-nibble length in LSBs */
                *offsets++=offset;
                *lengths++=length;

                offset+=length;
                ++i;
            }
        } else {
            length=0;   /* prevent double-nibble detection in the next iteration */
        }
    }

    /* now, s is at the first group string */
    return s;
}

static uint16_t
expandGroupName(UCharNames *names, Group *group,
                uint16_t lineNumber, UCharNameChoice nameChoice,
                char *buffer, uint16_t bufferLength) {
    uint16_t offsets[LINES_PER_GROUP+2], lengths[LINES_PER_GROUP+2];
    const uint8_t *s=(uint8_t *)names+names->groupStringOffset+
                                    (group->offsetHigh<<16|group->offsetLow);
    s=expandGroupLengths(s, offsets, lengths);
    return expandName(names, s+offsets[lineNumber], lengths[lineNumber], nameChoice,
                      buffer, bufferLength);
}

static uint16_t
getName(UCharNames *names, uint32_t code, UCharNameChoice nameChoice,
        char *buffer, uint16_t bufferLength) {
    Group *group=getGroup(names, code);
    if((uint16_t)(code>>GROUP_SHIFT)==group->groupMSB) {
        return expandGroupName(names, group, (uint16_t)(code&GROUP_MASK), nameChoice,
                               buffer, bufferLength);
    } else {
        /* group not found */
        /* zero-terminate */
        if(bufferLength>0) {
            *buffer=0;
        }
        return 0;
    }
}

/*
 * enumGroupNames() enumerates all the names in a 32-group
 * and either calls the enumerator function or finds a given input name.
 */
static UBool
enumGroupNames(UCharNames *names, Group *group,
               UChar32 start, UChar32 end,
               UEnumCharNamesFn *fn, void *context,
               UCharNameChoice nameChoice) {
    uint16_t offsets[LINES_PER_GROUP+2], lengths[LINES_PER_GROUP+2];
    const uint8_t *s=(uint8_t *)names+names->groupStringOffset+
                                    (group->offsetHigh<<16|group->offsetLow);

    s=expandGroupLengths(s, offsets, lengths);
    if(fn!=DO_FIND_NAME) {
        char buffer[200];
        uint16_t length;

        while(start<=end) {
            length=expandName(names, s+offsets[start&GROUP_MASK], lengths[start&GROUP_MASK], nameChoice, buffer, sizeof(buffer));
            if (!length && nameChoice == U_EXTENDED_CHAR_NAME) {
                buffer[length = getExtName(start, buffer, sizeof(buffer))] = 0;
            }
            /* here, we assume that the buffer is large enough */
            if(length>0) {
                if(!fn(context, start, nameChoice, buffer, length)) {
                    return FALSE;
                }
            }
            ++start;
        }
    } else {
        const char *otherName=((FindName *)context)->otherName;
        while(start<=end) {
            if(compareName(names, s+offsets[start&GROUP_MASK], lengths[start&GROUP_MASK], nameChoice, otherName)) {
                ((FindName *)context)->code=start;
                return FALSE;
            }
            ++start;
        }
    }
    return TRUE;
}

/*
 * enumExtNames enumerate extended names.
 * It only needs to do it if it is called with a real function and not
 * with the dummy DO_FIND_NAME, because u_charFromName() does a check
 * for extended names by itself.
 */ 
static UBool
enumExtNames(UChar32 start, UChar32 end,
             UEnumCharNamesFn *fn, void *context)
{
    if(fn!=DO_FIND_NAME) {
        char buffer[200];
        uint16_t length;
        
        while(start<=end) {
            buffer[length = getExtName(start, buffer, sizeof(buffer))] = 0;
            /* here, we assume that the buffer is large enough */
            if(length>0) {
                if(!fn(context, start, U_EXTENDED_CHAR_NAME, buffer, length)) {
                    return FALSE;
                }
            }
            ++start;
        }
    }

    return TRUE;
}

static UBool
enumNames(UCharNames *names,
          UChar32 start, UChar32 limit,
          UEnumCharNamesFn *fn, void *context,
          UCharNameChoice nameChoice) {
    uint16_t startGroupMSB, endGroupMSB, groupCount;
    Group *group, *groupLimit;

    startGroupMSB=(uint16_t)(start>>GROUP_SHIFT);
    endGroupMSB=(uint16_t)((limit-1)>>GROUP_SHIFT);

    /* find the group that contains start, or the highest before it */
    group=getGroup(names, start);

    if(startGroupMSB==endGroupMSB) {
        if(startGroupMSB==group->groupMSB) {
            /* if start and limit-1 are in the same group, then enumerate only in that one */
            return enumGroupNames(names, group, start, limit-1, fn, context, nameChoice);
        }
    } else {
        groupCount=*(uint16_t *)((char *)names+names->groupsOffset);
        groupLimit=(Group *)((char *)names+names->groupsOffset+2)+groupCount;

        if(startGroupMSB==group->groupMSB) {
            /* enumerate characters in the partial start group */
            if((start&GROUP_MASK)!=0) {
                if(!enumGroupNames(names, group,
                                   start, ((UChar32)startGroupMSB<<GROUP_SHIFT)+LINES_PER_GROUP-1,
                                   fn, context, nameChoice)) {
                    return FALSE;
                }
                ++group; /* continue with the next group */
            }
        } else if(startGroupMSB>group->groupMSB) {
            /* make sure that we start enumerating with the first group after start */
            if (group + 1 < groupLimit && (group + 1)->groupMSB > startGroupMSB && nameChoice == U_EXTENDED_CHAR_NAME) {
                UChar32 end = (group + 1)->groupMSB << GROUP_SHIFT;
                if (end > limit) {
                    end = limit;
                }
                if (!enumExtNames(start, end - 1, fn, context)) {
                    return FALSE;
                }
            }
            ++group;
        }

        /* enumerate entire groups between the start- and end-groups */
        while(group<groupLimit && group->groupMSB<endGroupMSB) {
            start=(UChar32)group->groupMSB<<GROUP_SHIFT;
            if(!enumGroupNames(names, group, start, start+LINES_PER_GROUP-1, fn, context, nameChoice)) {
                return FALSE;
            }
            if (group + 1 < groupLimit && (group + 1)->groupMSB > group->groupMSB + 1 && nameChoice == U_EXTENDED_CHAR_NAME) {
                UChar32 end = (group + 1)->groupMSB << GROUP_SHIFT;
                if (end > limit) {
                    end = limit;
                }
                if (!enumExtNames((group->groupMSB + 1) << GROUP_SHIFT, end - 1, fn, context)) {
                    return FALSE;
                }
            }
            ++group;
        }

        /* enumerate within the end group (group->groupMSB==endGroupMSB) */
        if(group<groupLimit && group->groupMSB==endGroupMSB) {
            return enumGroupNames(names, group, (limit-1)&~GROUP_MASK, limit-1, fn, context, nameChoice);
        } else if (nameChoice == U_EXTENDED_CHAR_NAME && group == groupLimit) {
            UChar32 next = ((group - 1)->groupMSB + 1) << GROUP_SHIFT;
            if (next > start) {
                start = next;
            }
        } else {
            return TRUE;
        }
    }

    /* we have not found a group, which means everything is made of
       extended names. */
    if (nameChoice == U_EXTENDED_CHAR_NAME) {
        if (limit > UCHAR_MAX_VALUE + 1) {
            limit = UCHAR_MAX_VALUE + 1;
        }
        return enumExtNames(start, limit - 1, fn, context);
    }
    
    return TRUE;
}

static uint16_t
writeFactorSuffix(const uint16_t *factors, uint16_t count,
                  const char *s, /* suffix elements */
                  uint32_t code,
                  uint16_t indexes[8], /* output fields from here */
                  const char *elementBases[8], const char *elements[8],
                  char *buffer, uint16_t bufferLength) {
    uint16_t i, factor, bufferPos=0;
    char c;

    /* write elements according to the factors */

    /*
     * the factorized elements are determined by modulo arithmetic
     * with the factors of this algorithm
     *
     * note that for fewer operations, count is decremented here
     */
    --count;
    for(i=count; i>0; --i) {
        factor=factors[i];
        indexes[i]=(uint16_t)(code%factor);
        code/=factor;
    }
    /*
     * we don't need to calculate the last modulus because start<=code<=end
     * guarantees here that code<=factors[0]
     */
    indexes[0]=(uint16_t)code;

    /* write each element */
    for(;;) {
        if(elementBases!=NULL) {
            *elementBases++=s;
        }

        /* skip indexes[i] strings */
        factor=indexes[i];
        while(factor>0) {
            while(*s++!=0) {}
            --factor;
        }
        if(elements!=NULL) {
            *elements++=s;
        }

        /* write element */
        while((c=*s++)!=0) {
            WRITE_CHAR(buffer, bufferLength, bufferPos, c);
        }

        /* we do not need to perform the rest of this loop for i==count - break here */
        if(i>=count) {
            break;
        }

        /* skip the rest of the strings for this factors[i] */
        factor=(uint16_t)(factors[i]-indexes[i]-1);
        while(factor>0) {
            while(*s++!=0) {}
            --factor;
        }

        ++i;
    }

    /* zero-terminate */
    if(bufferLength>0) {
        *buffer=0;
    }

    return bufferPos;
}

/*
 * Important:
 * Parts of findAlgName() are almost the same as some of getAlgName().
 * Fixes must be applied to both.
 */
static uint16_t
getAlgName(AlgorithmicRange *range, uint32_t code, UCharNameChoice nameChoice,
        char *buffer, uint16_t bufferLength) {
    uint16_t bufferPos=0;

    /*
     * Do not write algorithmic Unicode 1.0 names because
     * Unihan names are the same as the modern ones,
     * extension A was only introduced with Unicode 3.0, and
     * the Hangul syllable block was moved and changed around Unicode 1.1.5.
     */
    if(nameChoice==U_UNICODE_10_CHAR_NAME) {
        /* zero-terminate */
        if(bufferLength>0) {
            *buffer=0;
        }
        return 0;
    }

    switch(range->type) {
    case 0: {
        /* name = prefix hex-digits */
        const char *s=(const char *)(range+1);
        char c;

        uint16_t i, count;

        /* copy prefix */
        while((c=*s++)!=0) {
            WRITE_CHAR(buffer, bufferLength, bufferPos, c);
        }

        /* write hexadecimal code point value */
        count=range->variant;

        /* zero-terminate */
        if(count<bufferLength) {
            buffer[count]=0;
        }

        for(i=count; i>0;) {
            if(--i<bufferLength) {
                c=(char)(code&0xf);
                if(c<10) {
                    c+='0';
                } else {
                    c+='A'-10;
                }
                buffer[i]=c;
            }
            code>>=4;
        }

        bufferPos+=count;
        break;
    }
    case 1: {
        /* name = prefix factorized-elements */
        uint16_t indexes[8];
        const uint16_t *factors=(const uint16_t *)(range+1);
        uint16_t count=range->variant;
        const char *s=(const char *)(factors+count);
        char c;

        /* copy prefix */
        while((c=*s++)!=0) {
            WRITE_CHAR(buffer, bufferLength, bufferPos, c);
        }

        bufferPos+=writeFactorSuffix(factors, count,
                                     s, code-range->start, indexes, NULL, NULL, buffer, bufferLength);
        break;
    }
    default:
        /* undefined type */
        /* zero-terminate */
        if(bufferLength>0) {
            *buffer=0;
        }
        break;
    }

    return bufferPos;
}

/*
 * Important: enumAlgNames() and findAlgName() are almost the same.
 * Any fix must be applied to both.
 */
static UBool
enumAlgNames(AlgorithmicRange *range,
             UChar32 start, UChar32 limit,
             UEnumCharNamesFn *fn, void *context,
             UCharNameChoice nameChoice) {
    char buffer[200];
    uint16_t length;

    if(nameChoice==U_UNICODE_10_CHAR_NAME) {
        return TRUE;
    }

    switch(range->type) {
    case 0: {
        char *s, *end;
        char c;

        /* get the full name of the start character */
        length=getAlgName(range, (uint32_t)start, nameChoice, buffer, sizeof(buffer));
        if(length<=0) {
            return TRUE;
        }

        /* call the enumerator function with this first character */
        if(!fn(context, start, nameChoice, buffer, length)) {
            return FALSE;
        }

        /* go to the end of the name; all these names have the same length */
        end=buffer;
        while(*end!=0) {
            ++end;
        }

        /* enumerate the rest of the names */
        while(++start<limit) {
            /* increment the hexadecimal number on a character-basis */
            s=end;
            for (;;) {
                c=*--s;
                if(('0'<=c && c<'9') || ('A'<=c && c<'F')) {
                    *s=(char)(c+1);
                    break;
                } else if(c=='9') {
                    *s='A';
                    break;
                } else if(c=='F') {
                    *s='0';
                }
            }

            if(!fn(context, start, nameChoice, buffer, length)) {
                return FALSE;
            }
        }
        break;
    }
    case 1: {
        uint16_t indexes[8];
        const char *elementBases[8], *elements[8];
        const uint16_t *factors=(const uint16_t *)(range+1);
        uint16_t count=range->variant;
        const char *s=(const char *)(factors+count);
        char *suffix, *t;
        uint16_t prefixLength, i, index;

        char c;

        /* name = prefix factorized-elements */

        /* copy prefix */
        suffix=buffer;
        prefixLength=0;
        while((c=*s++)!=0) {
            *suffix++=c;
            ++prefixLength;
        }

        /* append the suffix of the start character */
        length=(uint16_t)(prefixLength+writeFactorSuffix(factors, count,
                                              s, (uint32_t)start-range->start,
                                              indexes, elementBases, elements,
                                              suffix, (uint16_t)(sizeof(buffer)-prefixLength)));

        /* call the enumerator function with this first character */
        if(!fn(context, start, nameChoice, buffer, length)) {
            return FALSE;
        }

        /* enumerate the rest of the names */
        while(++start<limit) {
            /* increment the indexes in lexical order bound by the factors */
            i=count;
            for (;;) {
                index=(uint16_t)(indexes[--i]+1);
                if(index<factors[i]) {
                    /* skip one index and its element string */
                    indexes[i]=index;
                    s=elements[i];
                    while(*s++!=0) {
                    }
                    elements[i]=s;
                    break;
                } else {
                    /* reset this index to 0 and its element string to the first one */
                    indexes[i]=0;
                    elements[i]=elementBases[i];
                }
            }

            /* to make matters a little easier, just append all elements to the suffix */
            t=suffix;
            length=prefixLength;
            for(i=0; i<count; ++i) {
                s=elements[i];
                while((c=*s++)!=0) {
                    *t++=c;
                    ++length;
                }
            }
            /* zero-terminate */
            *t=0;

            if(!fn(context, start, nameChoice, buffer, length)) {
                return FALSE;
            }
        }
        break;
    }
    default:
        /* undefined type */
        break;
    }

    return TRUE;
}

/*
 * findAlgName() is almost the same as enumAlgNames() except that it
 * returns the code point for a name if it fits into the range.
 * It returns 0xffff otherwise.
 */
static UChar32
findAlgName(AlgorithmicRange *range, UCharNameChoice nameChoice, const char *otherName) {
    UChar32 code;

    if(nameChoice==U_UNICODE_10_CHAR_NAME) {
        return 0xffff;
    }

    switch(range->type) {
    case 0: {
        /* name = prefix hex-digits */
        const char *s=(const char *)(range+1);
        char c;

        uint16_t i, count;

        /* compare prefix */
        while((c=*s++)!=0) {
            if((char)c!=*otherName++) {
                return 0xffff;
            }
        }

        /* read hexadecimal code point value */
        count=range->variant;
        code=0;
        for(i=0; i<count; ++i) {
            c=*otherName++;
            if('0'<=c && c<='9') {
                code=(code<<4)|(c-'0');
            } else if('A'<=c && c<='F') {
                code=(code<<4)|(c-'A'+10);
            } else {
                return 0xffff;
            }
        }

        /* does it fit into the range? */
        if(*otherName==0 && range->start<=(uint32_t)code && (uint32_t)code<=range->end) {
            return code;
        }
        break;
    }
    case 1: {
        char buffer[64];
        uint16_t indexes[8];
        const char *elementBases[8], *elements[8];
        const uint16_t *factors=(const uint16_t *)(range+1);
        uint16_t count=range->variant;
        const char *s=(const char *)(factors+count), *t;
        UChar32 start, limit;
        uint16_t i, index;

        char c;

        /* name = prefix factorized-elements */

        /* compare prefix */
        while((c=*s++)!=0) {
            if((char)c!=*otherName++) {
                return 0xffff;
            }
        }

        start=(UChar32)range->start;
        limit=(UChar32)(range->end+1);

        /* initialize the suffix elements for enumeration; indexes should all be set to 0 */
        writeFactorSuffix(factors, count, s, 0,
                          indexes, elementBases, elements, buffer, sizeof(buffer));

        /* compare the first suffix */
        if(0==uprv_strcmp(otherName, buffer)) {
            return start;
        }

        /* enumerate and compare the rest of the suffixes */
        while(++start<limit) {
            /* increment the indexes in lexical order bound by the factors */
            i=count;
            for (;;) {
                index=(uint16_t)(indexes[--i]+1);
                if(index<factors[i]) {
                    /* skip one index and its element string */
                    indexes[i]=index;
                    s=elements[i];
                    while(*s++!=0) {}
                    elements[i]=s;
                    break;
                } else {
                    /* reset this index to 0 and its element string to the first one */
                    indexes[i]=0;
                    elements[i]=elementBases[i];
                }
            }

            /* to make matters a little easier, just compare all elements of the suffix */
            t=otherName;
            for(i=0; i<count; ++i) {
                s=elements[i];
                while((c=*s++)!=0) {
                    if(c!=*t++) {
                        s=""; /* does not match */
                        i=99;
                    }
                }
            }
            if(i<99 && *t==0) {
                return start;
            }
        }
        break;
    }
    default:
        /* undefined type */
        break;
    }

    return 0xffff;
}

/* sets of name characters, maximum name lengths ---------------------------- */

#define SET_ADD(set, c) ((set)[(uint8_t)c>>5]|=((uint32_t)1<<((uint8_t)c&0x1f)))
#define SET_CONTAINS(set, c) (((set)[(uint8_t)c>>5]&((uint32_t)1<<((uint8_t)c&0x1f)))!=0)

static int32_t
calcStringSetLength(uint32_t set[8], const char *s) {
    int32_t length=0;
    char c;

    while((c=*s++)!=0) {
        SET_ADD(set, c);
        ++length;
    }
    return length;
}

static int32_t
calcAlgNameSetsLengths(int32_t maxNameLength) {
    AlgorithmicRange *range;
    uint32_t *p;
    uint32_t rangeCount;
    int32_t length;

    /* enumerate algorithmic ranges */
    p=(uint32_t *)((uint8_t *)uCharNames+uCharNames->algNamesOffset);
    rangeCount=*p;
    range=(AlgorithmicRange *)(p+1);
    while(rangeCount>0) {
        switch(range->type) {
        case 0:
            /* name = prefix + (range->variant times) hex-digits */
            /* prefix */
            length=calcStringSetLength(gNameSet, (const char *)(range+1))+range->variant;
            if(length>maxNameLength) {
                maxNameLength=length;
            }
            break;
        case 1: {
            /* name = prefix factorized-elements */
            const uint16_t *factors=(const uint16_t *)(range+1);
            const char *s;
            int32_t i, count=range->variant, factor, factorLength, maxFactorLength;

            /* prefix length */
            s=(const char *)(factors+count);
            length=calcStringSetLength(gNameSet, s);
            s+=length+1; /* start of factor suffixes */

            /* get the set and maximum factor suffix length for each factor */
            for(i=0; i<count; ++i) {
                maxFactorLength=0;
                for(factor=factors[i]; factor>0; --factor) {
                    factorLength=calcStringSetLength(gNameSet, s);
                    s+=factorLength+1;
                    if(factorLength>maxFactorLength) {
                        maxFactorLength=factorLength;
                    }
                }
                length+=maxFactorLength;
            }

            if(length>maxNameLength) {
                maxNameLength=length;
            }
            break;
        }
        default:
            /* unknown type */
            break;
        }

        range=(AlgorithmicRange *)((uint8_t *)range+range->size);
        --rangeCount;
    }
    return maxNameLength;
}

static int32_t
calcExtNameSetsLengths(int32_t maxNameLength) {
    int32_t i, length;

    for(i=0; i<LENGTHOF(charCatNames); ++i) {
        /*
         * for each category, count the length of the category name
         * plus 9=
         * 2 for <>
         * 1 for -
         * 6 for most hex digits per code point
         */
        length=9+calcStringSetLength(gNameSet, charCatNames[i]);
        if(length>maxNameLength) {
            maxNameLength=length;
        }
    }
    return maxNameLength;
}

static int32_t
calcNameSetLength(const uint16_t *tokens, uint16_t tokenCount, const uint8_t *tokenStrings, int8_t *tokenLengths,
                  uint32_t set[8],
                  const uint8_t **pLine, const uint8_t *lineLimit) {
    const uint8_t *line=*pLine;
    int32_t length=0, tokenLength;
    uint16_t c, token;

    while(line!=lineLimit && (c=*line++)!=(uint8_t)';') {
        if(c>=tokenCount) {
            /* implicit letter */
            SET_ADD(set, c);
            ++length;
        } else {
            token=tokens[c];
            if(token==(uint16_t)(-2)) {
                /* this is a lead byte for a double-byte token */
                c=c<<8|*line++;
                token=tokens[c];
            }
            if(token==(uint16_t)(-1)) {
                /* explicit letter */
                SET_ADD(set, c);
                ++length;
            } else {
                /* count token word */
                if(tokenLengths!=NULL) {
                    /* use cached token length */
                    tokenLength=tokenLengths[c];
                    if(tokenLength==0) {
                        tokenLength=calcStringSetLength(set, (const char *)tokenStrings+token);
                        tokenLengths[c]=(int8_t)tokenLength;
                    }
                } else {
                    tokenLength=calcStringSetLength(set, (const char *)tokenStrings+token);
                }
                length+=tokenLength;
            }
        }
    }

    *pLine=line;
    return length;
}

static void
calcGroupNameSetsLengths(int32_t maxNameLength) {
    uint16_t offsets[LINES_PER_GROUP+2], lengths[LINES_PER_GROUP+2];

    uint16_t *tokens=(uint16_t *)uCharNames+8;
    uint16_t tokenCount=*tokens++;
    uint8_t *tokenStrings=(uint8_t *)uCharNames+uCharNames->tokenStringOffset;

    int8_t *tokenLengths;

    uint16_t *groups;
    Group *group;
    const uint8_t *s, *line, *lineLimit;

    int32_t maxISOCommentLength=0;
    int32_t groupCount, lineNumber, length;

    tokenLengths=(int8_t *)uprv_malloc(tokenCount);
    if(tokenLengths!=NULL) {
        uprv_memset(tokenLengths, 0, tokenCount);
    }

    groups=(uint16_t *)((char *)uCharNames+uCharNames->groupsOffset);
    groupCount=*groups++;
    group=(Group *)groups;

    /* enumerate all groups */
    while(groupCount>0) {
        s=(uint8_t *)uCharNames+uCharNames->groupStringOffset+
                                    ((int32_t)group->offsetHigh<<16|group->offsetLow);
        s=expandGroupLengths(s, offsets, lengths);

        /* enumerate all lines in each group */
        for(lineNumber=0; lineNumber<LINES_PER_GROUP; ++lineNumber) {
            line=s+offsets[lineNumber];
            length=lengths[lineNumber];
            if(length==0) {
                continue;
            }

            lineLimit=line+length;

            /* read regular name */
            length=calcNameSetLength(tokens, tokenCount, tokenStrings, tokenLengths, gNameSet, &line, lineLimit);
            if(length>maxNameLength) {
                maxNameLength=length;
            }
            if(line==lineLimit) {
                continue;
            }

            /* read Unicode 1.0 name */
            length=calcNameSetLength(tokens, tokenCount, tokenStrings, tokenLengths, gNameSet, &line, lineLimit);
            if(length>maxNameLength) {
                maxNameLength=length;
            }
            if(line==lineLimit) {
                continue;
            }

            /* read ISO comment */
            length=calcNameSetLength(tokens, tokenCount, tokenStrings, tokenLengths, gISOCommentSet, &line, lineLimit);
            if(length>maxISOCommentLength) {
                maxISOCommentLength=length;
            }
        }

        ++group;
        --groupCount;
    }

    if(tokenLengths!=NULL) {
        uprv_free(tokenLengths);
    }

    /* set gMax... - name length last for threading */
    gMaxISOCommentLength=maxISOCommentLength;
    gMaxNameLength=maxNameLength;
}

static UBool
calcNameSetsLengths(UErrorCode *pErrorCode) {
    static const char extChars[]="0123456789ABCDEF<>-";
    int32_t i, maxNameLength;

    if(gMaxNameLength!=0) {
        return TRUE;
    }

    if(!isDataLoaded(pErrorCode)) {
        return FALSE;
    }

    /* set hex digits, used in various names, and <>-, used in extended names */
    for(i=0; i<sizeof(extChars)-1; ++i) {
        SET_ADD(gNameSet, extChars[i]);
    }

    /* set sets and lengths from algorithmic names */
    maxNameLength=calcAlgNameSetsLengths(0);

    /* set sets and lengths from extended names */
    maxNameLength=calcExtNameSetsLengths(maxNameLength);

    /* set sets and lengths from group names, set global maximum values */
    calcGroupNameSetsLengths(maxNameLength);

    return TRUE;
}

/* public API --------------------------------------------------------------- */

U_CAPI int32_t U_EXPORT2
u_charName(UChar32 code, UCharNameChoice nameChoice,
           char *buffer, int32_t bufferLength,
           UErrorCode *pErrorCode) {
    AlgorithmicRange *algRange;
    uint32_t *p;
    uint32_t i;
    int32_t length;

    /* check the argument values */
    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return 0;
    } else if(nameChoice>=U_CHAR_NAME_CHOICE_COUNT ||
              bufferLength<0 || (bufferLength>0 && buffer==NULL)
    ) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    if((uint32_t)code>UCHAR_MAX_VALUE || !isDataLoaded(pErrorCode)) {
        return u_terminateChars(buffer, bufferLength, 0, pErrorCode);
    }

    length=0;

    /* try algorithmic names first */
    p=(uint32_t *)((uint8_t *)uCharNames+uCharNames->algNamesOffset);
    i=*p;
    algRange=(AlgorithmicRange *)(p+1);
    while(i>0) {
        if(algRange->start<=(uint32_t)code && (uint32_t)code<=algRange->end) {
            length=getAlgName(algRange, (uint32_t)code, nameChoice, buffer, (uint16_t)bufferLength);
            break;
        }
        algRange=(AlgorithmicRange *)((uint8_t *)algRange+algRange->size);
        --i;
    }

    if(i==0) {
        if (nameChoice == U_EXTENDED_CHAR_NAME) {
            length = getName(uCharNames, (uint32_t )code, U_EXTENDED_CHAR_NAME, buffer, (uint16_t) bufferLength);
            if (!length) {
                /* extended character name */
                length = getExtName((uint32_t) code, buffer, (uint16_t) bufferLength);
            }
        } else {
            /* normal character name */
            length=getName(uCharNames, (uint32_t)code, nameChoice, buffer, (uint16_t)bufferLength);
        }
    }

    return u_terminateChars(buffer, bufferLength, length, pErrorCode);
}

U_CAPI int32_t U_EXPORT2
u_getISOComment(UChar32 c,
                char *dest, int32_t destCapacity,
                UErrorCode *pErrorCode) {
    int32_t length;

    /* check the argument values */
    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return 0;
    } else if(destCapacity<0 || (destCapacity>0 && dest==NULL)) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    if((uint32_t)c>UCHAR_MAX_VALUE || !isDataLoaded(pErrorCode)) {
        return u_terminateChars(dest, destCapacity, 0, pErrorCode);
    }

    /* the ISO comment is stored like a normal character name */
    length=getName(uCharNames, (uint32_t)c, U_ISO_COMMENT, dest, (uint16_t)destCapacity);
    return u_terminateChars(dest, destCapacity, length, pErrorCode);
}

U_CAPI UChar32 U_EXPORT2
u_charFromName(UCharNameChoice nameChoice,
               const char *name,
               UErrorCode *pErrorCode) {
    char upper[120], lower[120];
    FindName findName;
    AlgorithmicRange *algRange;
    uint32_t *p;
    uint32_t i;
    UChar32 cp = 0;
    char c0;
    UChar32 error = 0xffff;     /* Undefined, but use this for backwards compatibility. */

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return error;
    }

    if(nameChoice>=U_CHAR_NAME_CHOICE_COUNT || name==NULL || *name==0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return error;
    }

    if(!isDataLoaded(pErrorCode)) {
        return error;
    }

    /* construct the uppercase and lowercase of the name first */
    for(i=0; i<sizeof(upper); ++i) {
        if((c0=*name++)!=0) {
            upper[i]=uprv_toupper(c0);
            lower[i]=uprv_tolower(c0);
        } else {
            upper[i]=lower[i]=0;
            break;
        }
    }
    if(i==sizeof(upper)) {
        /* name too long, there is no such character */
        *pErrorCode = U_ILLEGAL_CHAR_FOUND;
        return error;
    }

    /* try extended names first */
    if (lower[0] == '<') {
        if (nameChoice == U_EXTENDED_CHAR_NAME) {
            if (lower[--i] == '>') {
                for (--i; lower[i] && lower[i] != '-'; --i) {
                }

                if (lower[i] == '-') { /* We've got a category. */
                    uint32_t cIdx;

                    lower[i] = 0;

                    for (++i; lower[i] != '>'; ++i) {
                        if (lower[i] >= '0' && lower[i] <= '9') {
                            cp = (cp << 4) + lower[i] - '0';
                        } else if (lower[i] >= 'a' && lower[i] <= 'f') {
                            cp = (cp << 4) + lower[i] - 'a' + 10;
                        } else {
                            *pErrorCode = U_ILLEGAL_CHAR_FOUND;
                            return error;
                        }
                    }

                    /* Now validate the category name.
                       We could use a binary search, or a trie, if
                       we really wanted to. */

                    for (lower[i] = 0, cIdx = 0; cIdx < LENGTHOF(charCatNames); ++cIdx) {

                        if (!uprv_strcmp(lower + 1, charCatNames[cIdx])) {
                            if (getCharCat(cp) == cIdx) {
                                return cp;
                            }
                            break;
                        }
                    }
                }
            }
        }

        *pErrorCode = U_ILLEGAL_CHAR_FOUND;
        return error;
    }

    /* try algorithmic names now */
    p=(uint32_t *)((uint8_t *)uCharNames+uCharNames->algNamesOffset);
    i=*p;
    algRange=(AlgorithmicRange *)(p+1);
    while(i>0) {
        if((cp=findAlgName(algRange, nameChoice, upper))!=0xffff) {
            return cp;
        }
        algRange=(AlgorithmicRange *)((uint8_t *)algRange+algRange->size);
        --i;
    }

    /* normal character name */
    findName.otherName=upper;
    findName.code=error;
    enumNames(uCharNames, 0, UCHAR_MAX_VALUE + 1, DO_FIND_NAME, &findName, nameChoice);
    if (findName.code == error) {
         *pErrorCode = U_ILLEGAL_CHAR_FOUND;
    }
    return findName.code;
}

U_CAPI void U_EXPORT2
u_enumCharNames(UChar32 start, UChar32 limit,
                UEnumCharNamesFn *fn,
                void *context,
                UCharNameChoice nameChoice,
                UErrorCode *pErrorCode) {
    AlgorithmicRange *algRange;
    uint32_t *p;
    uint32_t i;

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return;
    }

    if(nameChoice>=U_CHAR_NAME_CHOICE_COUNT || fn==NULL) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if((uint32_t) limit > UCHAR_MAX_VALUE + 1) {
        limit = UCHAR_MAX_VALUE + 1;
    }
    if((uint32_t)start>=(uint32_t)limit) {
        return;
    }

    if(!isDataLoaded(pErrorCode)) {
        return;
    }

    /* interleave the data-driven ones with the algorithmic ones */
    /* iterate over all algorithmic ranges; assume that they are in ascending order */
    p=(uint32_t *)((uint8_t *)uCharNames+uCharNames->algNamesOffset);
    i=*p;
    algRange=(AlgorithmicRange *)(p+1);
    while(i>0) {
        /* enumerate the character names before the current algorithmic range */
        /* here: start<limit */
        if((uint32_t)start<algRange->start) {
            if((uint32_t)limit<=algRange->start) {
                enumNames(uCharNames, start, limit, fn, context, nameChoice);
                return;
            }
            if(!enumNames(uCharNames, start, (UChar32)algRange->start, fn, context, nameChoice)) {
                return;
            }
            start=(UChar32)algRange->start;
        }
        /* enumerate the character names in the current algorithmic range */
        /* here: algRange->start<=start<limit */
        if((uint32_t)start<=algRange->end) {
            if((uint32_t)limit<=(algRange->end+1)) {
                enumAlgNames(algRange, start, limit, fn, context, nameChoice);
                return;
            }
            if(!enumAlgNames(algRange, start, (UChar32)algRange->end+1, fn, context, nameChoice)) {
                return;
            }
            start=(UChar32)algRange->end+1;
        }
        /* continue to the next algorithmic range (here: start<limit) */
        algRange=(AlgorithmicRange *)((uint8_t *)algRange+algRange->size);
        --i;
    }
    /* enumerate the character names after the last algorithmic range */
    enumNames(uCharNames, start, limit, fn, context, nameChoice);
}

U_CAPI int32_t U_EXPORT2
uprv_getMaxCharNameLength() {
    UErrorCode errorCode=U_ZERO_ERROR;
    if(calcNameSetsLengths(&errorCode)) {
        return gMaxNameLength;
    } else {
        return 0;
    }
}

#if 0
/* 
Currently not used but left for future use. Probably by UnicodeSet. 
urename.h and uprops.h changed accordingly. 
*/
U_CAPI int32_t U_EXPORT2
uprv_getMaxISOCommentLength() {
    UErrorCode errorCode=U_ZERO_ERROR;
    if(calcNameSetsLengths(&errorCode)) {
        return gMaxISOCommentLength;
    } else {
        return 0;
    }
}
#endif

/**
 * Converts the char set cset into a Unicode set uset.
 * @param cset Set of 256 bit flags corresponding to a set of chars.
 * @param uset USet to receive characters. Existing contents are deleted.
 */
static void
charSetToUSet(uint32_t cset[8], USetAdder *sa) {
    UChar us[256];
    char cs[256];

    int32_t i, length;
    UErrorCode errorCode;

    errorCode=U_ZERO_ERROR;

    if(!calcNameSetsLengths(&errorCode)) {
        return;
    }

    /* build a char string with all chars that are used in character names */
    length=0;
    for(i=0; i<256; ++i) {
        if(SET_CONTAINS(cset, i)) {
            cs[length++]=(char)i;
        }
    }

    /* convert the char string to a UChar string */
    u_charsToUChars(cs, us, length);

    /* add each UChar to the USet */
    for(i=0; i<length; ++i) {
        if(us[i]!=0 || cs[i]==0) { /* non-invariant chars become (UChar)0 */
            sa->add(sa->set, us[i]);
        }
    }
}

/**
 * Fills set with characters that are used in Unicode character names.
 * @param set USet to receive characters.
 */
U_CAPI void U_EXPORT2
uprv_getCharNameCharacters(USetAdder *sa) {
    charSetToUSet(gNameSet, sa);
}

#if 0
/* 
Currently not used but left for future use. Probably by UnicodeSet. 
urename.h and uprops.h changed accordingly. 
*/
/**
 * Fills set with characters that are used in Unicode character names.
 * @param set USetAdder to receive characters.
 */
U_CAPI void U_EXPORT2
uprv_getISOCommentCharacters(USetAdder *sa) {
    charSetToUSet(gISOCommentSet, sa);
}
#endif

/* data swapping ------------------------------------------------------------ */

/*
 * The token table contains non-negative entries for token bytes,
 * and -1 for bytes that represent themselves in the data file's charset.
 * -2 entries are used for lead bytes.
 *
 * Direct bytes (-1 entries) must be translated from the input charset family
 * to the output charset family.
 * makeTokenMap() writes a permutation mapping for this.
 * Use it once for single-/lead-byte tokens and once more for all trail byte
 * tokens. (';' is an unused trail byte marked with -1.)
 */
static void
makeTokenMap(const UDataSwapper *ds,
             int16_t tokens[], uint16_t tokenCount,
             uint8_t map[256],
             UErrorCode *pErrorCode) {
    UBool usedOutChar[256];
    uint16_t i, j;
    uint8_t c1, c2;

    if(U_FAILURE(*pErrorCode)) {
        return;
    }

    if(ds->inCharset==ds->outCharset) {
        /* Same charset family: identity permutation */
        for(i=0; i<256; ++i) {
            map[i]=(uint8_t)i;
        }
    } else {
        uprv_memset(map, 0, 256);
        uprv_memset(usedOutChar, 0, 256);

        if(tokenCount>256) {
            tokenCount=256;
        }

        /* set the direct bytes (byte 0 always maps to itself) */
        for(i=1; i<tokenCount; ++i) {
            if(tokens[i]==-1) {
                /* convert the direct byte character */
                c1=(uint8_t)i;
                ds->swapInvChars(ds, &c1, 1, &c2, pErrorCode);
                if(U_FAILURE(*pErrorCode)) {
                    udata_printError(ds, "unames/makeTokenMap() finds variant character 0x%02x used (input charset family %d) - %s\n",
                                     i, ds->inCharset, u_errorName(*pErrorCode));
                    return;
                }

                /* enter the converted character into the map and mark it used */
                map[c1]=c2;
                usedOutChar[c2]=TRUE;
            }
        }

        /* set the mappings for the rest of the permutation */
        for(i=j=1; i<tokenCount; ++i) {
            /* set mappings that were not set for direct bytes */
            if(map[i]==0) {
                /* set an output byte value that was not used as an output byte above */
                while(usedOutChar[j]) {
                    ++j;
                }
                map[i]=(uint8_t)j++;
            }
        }

        /*
         * leave mappings at tokenCount and above unset if tokenCount<256
         * because they won't be used
         */
    }
}

U_CAPI int32_t U_EXPORT2
uchar_swapNames(const UDataSwapper *ds,
                const void *inData, int32_t length, void *outData,
                UErrorCode *pErrorCode) {
    const UDataInfo *pInfo;
    int32_t headerSize;

    const uint8_t *inBytes;
    uint8_t *outBytes;

    uint32_t tokenStringOffset, groupsOffset, groupStringOffset, algNamesOffset,
             offset, i, count, stringsCount;

    const AlgorithmicRange *inRange;
    AlgorithmicRange *outRange;

    /* udata_swapDataHeader checks the arguments */
    headerSize=udata_swapDataHeader(ds, inData, length, outData, pErrorCode);
    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return 0;
    }

    /* check data format and format version */
    pInfo=(const UDataInfo *)((const char *)inData+4);
    if(!(
        pInfo->dataFormat[0]==0x75 &&   /* dataFormat="unam" */
        pInfo->dataFormat[1]==0x6e &&
        pInfo->dataFormat[2]==0x61 &&
        pInfo->dataFormat[3]==0x6d &&
        pInfo->formatVersion[0]==1
    )) {
        udata_printError(ds, "uchar_swapNames(): data format %02x.%02x.%02x.%02x (format version %02x) is not recognized as unames.icu\n",
                         pInfo->dataFormat[0], pInfo->dataFormat[1],
                         pInfo->dataFormat[2], pInfo->dataFormat[3],
                         pInfo->formatVersion[0]);
        *pErrorCode=U_UNSUPPORTED_ERROR;
        return 0;
    }

    inBytes=(const uint8_t *)inData+headerSize;
    outBytes=(uint8_t *)outData+headerSize;
    if(length<0) {
        algNamesOffset=ds->readUInt32(((const uint32_t *)inBytes)[3]);
    } else {
        length-=headerSize;
        if( length<20 ||
            (uint32_t)length<(algNamesOffset=ds->readUInt32(((const uint32_t *)inBytes)[3]))
        ) {
            udata_printError(ds, "uchar_swapNames(): too few bytes (%d after header) for unames.icu\n",
                             length);
            *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
            return 0;
        }
    }

    if(length<0) {
        /* preflighting: iterate through algorithmic ranges */
        offset=algNamesOffset;
        count=ds->readUInt32(*((const uint32_t *)(inBytes+offset)));
        offset+=4;

        for(i=0; i<count; ++i) {
            inRange=(const AlgorithmicRange *)(inBytes+offset);
            offset+=ds->readUInt16(inRange->size);
        }
    } else {
        /* swap data */
        const uint16_t *p;
        uint16_t *q, *temp;

        int16_t tokens[512];
        uint16_t tokenCount;

        uint8_t map[256], trailMap[256];

        /* copy the data for inaccessible bytes */
        if(inBytes!=outBytes) {
            uprv_memcpy(outBytes, inBytes, length);
        }

        /* the initial 4 offsets first */
        tokenStringOffset=ds->readUInt32(((const uint32_t *)inBytes)[0]);
        groupsOffset=ds->readUInt32(((const uint32_t *)inBytes)[1]);
        groupStringOffset=ds->readUInt32(((const uint32_t *)inBytes)[2]);
        ds->swapArray32(ds, inBytes, 16, outBytes, pErrorCode);

        /*
         * now the tokens table
         * it needs to be permutated along with the compressed name strings
         */
        p=(const uint16_t *)(inBytes+16);
        q=(uint16_t *)(outBytes+16);

        /* read and swap the tokenCount */
        tokenCount=ds->readUInt16(*p);
        ds->swapArray16(ds, p, 2, q, pErrorCode);
        ++p;
        ++q;

        /* read the first 512 tokens and make the token maps */
        if(tokenCount<=512) {
            count=tokenCount;
        } else {
            count=512;
        }
        for(i=0; i<count; ++i) {
            tokens[i]=udata_readInt16(ds, p[i]);
        }
        for(; i<512; ++i) {
            tokens[i]=0; /* fill the rest of the tokens array if tokenCount<512 */
        }
        makeTokenMap(ds, tokens, tokenCount, map, pErrorCode);
        makeTokenMap(ds, tokens+256, (uint16_t)(tokenCount>256 ? tokenCount-256 : 0), trailMap, pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            return 0;
        }

        /*
         * swap and permutate the tokens
         * go through a temporary array to support in-place swapping
         */
        temp=(uint16_t *)uprv_malloc(tokenCount*2);
        if(temp==NULL) {
            udata_printError(ds, "out of memory swapping %u unames.icu tokens\n",
                             tokenCount);
            *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }

        /* swap and permutate single-/lead-byte tokens */
        for(i=0; i<tokenCount && i<256; ++i) {
            ds->swapArray16(ds, p+i, 2, temp+map[i], pErrorCode);
        }

        /* swap and permutate trail-byte tokens */
        for(; i<tokenCount; ++i) {
            ds->swapArray16(ds, p+i, 2, temp+(i&0xffffff00)+trailMap[i&0xff], pErrorCode);
        }

        /* copy the result into the output and free the temporary array */
        uprv_memcpy(q, temp, tokenCount*2);
        uprv_free(temp);

        /*
         * swap the token strings but not a possible padding byte after
         * the terminating NUL of the last string
         */
        udata_swapInvStringBlock(ds, inBytes+tokenStringOffset, (int32_t)(groupsOffset-tokenStringOffset),
                                    outBytes+tokenStringOffset, pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            udata_printError(ds, "uchar_swapNames(token strings) failed - %s\n",
                             u_errorName(*pErrorCode));
            return 0;
        }

        /* swap the group table */
        count=ds->readUInt16(*((const uint16_t *)(inBytes+groupsOffset)));
        ds->swapArray16(ds, inBytes+groupsOffset, (int32_t)((1+count*3)*2),
                           outBytes+groupsOffset, pErrorCode);

        /*
         * swap the group strings
         * swap the string bytes but not the nibble-encoded string lengths
         */
        if(ds->inCharset!=ds->outCharset) {
            uint16_t offsets[LINES_PER_GROUP+1], lengths[LINES_PER_GROUP+1];

            const uint8_t *inStrings, *nextInStrings;
            uint8_t *outStrings;

            uint8_t c;

            inStrings=inBytes+groupStringOffset;
            outStrings=outBytes+groupStringOffset;

            stringsCount=algNamesOffset-groupStringOffset;

            /* iterate through string groups until only a few padding bytes are left */
            while(stringsCount>32) {
                nextInStrings=expandGroupLengths(inStrings, offsets, lengths);

                /* move past the length bytes */
                stringsCount-=(uint32_t)(nextInStrings-inStrings);
                outStrings+=nextInStrings-inStrings;
                inStrings=nextInStrings;

                count=offsets[31]+lengths[31]; /* total number of string bytes in this group */
                stringsCount-=count;

                /* swap the string bytes using map[] and trailMap[] */
                while(count>0) {
                    c=*inStrings++;
                    *outStrings++=map[c];
                    if(tokens[c]!=-2) {
                        --count;
                    } else {
                        /* token lead byte: swap the trail byte, too */
                        *outStrings++=trailMap[*inStrings++];
                        count-=2;
                    }
                }
            }
        }

        /* swap the algorithmic ranges */
        offset=algNamesOffset;
        count=ds->readUInt32(*((const uint32_t *)(inBytes+offset)));
        ds->swapArray32(ds, inBytes+offset, 4, outBytes+offset, pErrorCode);
        offset+=4;

        for(i=0; i<count; ++i) {
            if(offset>(uint32_t)length) {
                udata_printError(ds, "uchar_swapNames(): too few bytes (%d after header) for unames.icu algorithmic range %u\n",
                                 length, i);
                *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
                return 0;
            }

            inRange=(const AlgorithmicRange *)(inBytes+offset);
            outRange=(AlgorithmicRange *)(outBytes+offset);
            offset+=ds->readUInt16(inRange->size);

            ds->swapArray32(ds, inRange, 8, outRange, pErrorCode);
            ds->swapArray16(ds, &inRange->size, 2, &outRange->size, pErrorCode);
            switch(inRange->type) {
            case 0:
                /* swap prefix string */
                ds->swapInvChars(ds, inRange+1, uprv_strlen((const char *)(inRange+1)),
                                    outRange+1, pErrorCode);
                if(U_FAILURE(*pErrorCode)) {
                    udata_printError(ds, "uchar_swapNames(prefix string of algorithmic range %u) failed - %s\n",
                                     i, u_errorName(*pErrorCode));
                    return 0;
                }
                break;
            case 1:
                {
                    /* swap factors and the prefix and factor strings */
                    uint16_t factors[8];
                    uint32_t j, factorsCount;

                    factorsCount=inRange->variant;
                    if(factorsCount==0 || factorsCount>LENGTHOF(factors)) {
                        udata_printError(ds, "uchar_swapNames(): too many factors (%u) in algorithmic range %u\n",
                                         factorsCount, i);
                        *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
                        return 0;
                    }

                    /* read and swap the factors */
                    p=(const uint16_t *)(inRange+1);
                    q=(uint16_t *)(outRange+1);
                    for(j=0; j<factorsCount; ++j) {
                        factors[j]=ds->readUInt16(p[j]);
                    }
                    ds->swapArray16(ds, p, (int32_t)(factorsCount*2), q, pErrorCode);

                    /* swap the strings, up to the last terminating NUL */
                    p+=factorsCount;
                    q+=factorsCount;
                    stringsCount=(uint32_t)((inBytes+offset)-(const uint8_t *)p);
                    while(stringsCount>0 && ((const uint8_t *)p)[stringsCount-1]!=0) {
                        --stringsCount;
                    }
                    ds->swapInvChars(ds, p, (int32_t)stringsCount, q, pErrorCode);
                }
                break;
            default:
                udata_printError(ds, "uchar_swapNames(): unknown type %u of algorithmic range %u\n",
                                 inRange->type, i);
                *pErrorCode=U_UNSUPPORTED_ERROR;
                return 0;
            }
        }
    }

    return headerSize+(int32_t)offset;
}

/*
 * Hey, Emacs, please set the following:
 *
 * Local Variables:
 * indent-tabs-mode: nil
 * End:
 *
 */
