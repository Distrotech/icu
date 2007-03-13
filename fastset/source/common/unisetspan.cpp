/*
******************************************************************************
*
*   Copyright (C) 2007, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  unisetspan.cpp
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2007mar01
*   created by: Markus W. Scherer
*/

#include "unicode/utypes.h"
#include "unicode/uniset.h"
#include "unicode/ustring.h"
#include "cmemory.h"
#include "uvector.h"
#include "unisetspan.h"

U_NAMESPACE_BEGIN

/*
 * List of offsets from the current position from where to try matching
 * a code point or a string.
 * Store offsets rather than indexes to simplify the code and use the same list
 * for both increments (in span()) and decrements (in spanBack()).
 *
 * Assumption: The maximum offset is limited, and the offsets that are stored
 * at any one time are relatively dense, that is, there are normally no gaps of
 * hundreds or thousands of offset values.
 *
 * The implementation uses a circular buffer of byte flags,
 * each indicating whether the corresponding offset is in the list.
 * This avoids inserting into a sorted list of offsets (or absolute indexes) and
 * physically moving part of the list.
 *
 * Note: In principle, the caller should setMaxLength() to the maximum of the
 * max string length and U16_LENGTH/U8_LENGTH to account for
 * "long" single code points.
 * However, this implementation uses at least a staticList with more than
 * U8_LENGTH entries anyway.
 *
 * Note: If maxLength were guaranteed to be no more than 32 or 64,
 * the list could be stored as bit flags in a single integer.
 * Rather than handling a circular buffer with a start list index,
 * the integer would simply be shifted when lower offsets are removed.
 * UnicodeSet does not have a limit on the lengths of strings.
 */
class OffsetList {  // Only ever stack-allocated, does not need to inherit UMemory.
public:
    OffsetList() : list(staticList), capacity(0), length(0), start(0) {}

    ~OffsetList() {
        if(list!=staticList) {
            uprv_free(list);
        }
    }

    // Call exactly once if the list is to be used.
    void setMaxLength(int32_t maxLength) {
        if(maxLength<=sizeof(staticList)) {
            capacity=(int32_t)sizeof(staticList);
        } else {
            UBool *l=(UBool *)uprv_malloc(maxLength);
            if(l!=NULL) {
                list=l;
                capacity=maxLength;
            }
        }
        uprv_memset(list, 0, capacity);
    }

    void clear() {
        uprv_memset(list, 0, capacity);
        start=length=0;
    }

    UBool isEmpty() const {
        return (UBool)(length==0);
    }

    // Reduce all stored offsets by delta, used when the current position
    // moves by delta.
    // There must not be any offsets lower than delta.
    // If there is an offset equal to delta, it is removed.
    // delta=[1..maxLength]
    void shift(int32_t delta) {
        int32_t i=start+delta;
        if(i>=capacity) {
            i-=capacity;
        }
        if(list[i]) {
            list[i]=FALSE;
            --length;
        }
        start=i;
    }

    // Add an offset. The list must not contain it yet.
    // offset=[1..maxLength]
    void addOffset(int32_t offset) {
        int32_t i=start+offset;
        if(i>=capacity) {
            i-=capacity;
        }
        list[i]=TRUE;
        ++length;
    }

    // offset=[1..maxLength]
    UBool containsOffset(int32_t offset) const {
        int32_t i=start+offset;
        if(i>=capacity) {
            i-=capacity;
        }
        return list[i];
    }

    // Find the lowest stored offset from a non-empty list, remove it,
    // and reduce all other offsets by this minimum.
    // Returns [1..maxLength].
    int32_t popMinimum() {
        // Look for the next offset in list[start+1..capacity-1].
        int32_t i=start, result;
        while(++i<capacity) {
            if(list[i]) {
                list[i]=FALSE;
                --length;
                result=i-start;
                start=i;
                return result;
            }
        }
        // i==capacity

        // Wrap around and look for the next offset in list[0..start].
        // Since the list is not empty, there will be one.
        result=capacity-start;
        i=0;
        while(!list[i]) {
            ++i;
        }
        list[i]=FALSE;
        --length;
        start=i;
        return result+=i;
    }

private:
    UBool *list;
    int32_t capacity;
    int32_t length;
    int32_t start;

    UBool staticList[16];
};

// Get the number of UTF-8 bytes for a UTF-16 (sub)string.
static int32_t
getUTF8Length(const UChar *s, int32_t length) {
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length8=0;
    u_strToUTF8(NULL, 0, &length8, s, length, &errorCode);
    if(U_SUCCESS(errorCode) || errorCode==U_BUFFER_OVERFLOW_ERROR) {
        return length8;
    } else {
        // The string contains an unpaired surrogate.
        // Ignore this string.
        return 0;
    }
}

// Append the UTF-8 version of the string to t and return the appended UTF-8 length.
static int32_t
appendUTF8(const UChar *s, int32_t length, uint8_t *t, int32_t capacity) {
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length8=0;
    u_strToUTF8((char *)t, capacity, &length8, s, length, &errorCode);
    if(U_SUCCESS(errorCode)) {
        return length8;
    } else {
        // The string contains an unpaired surrogate.
        // Ignore this string.
        return 0;
    }
}

static inline uint8_t
makeSpanLengthByte(int32_t spanLength) {
    // 0xfe==UnicodeSetStringSpan::LONG_SPAN
    return spanLength<0xfe ? (uint8_t)spanLength : (uint8_t)0xfe;
}

// Construct for all variants of span(), or only for any one variant.
// Initialize as little as possible, for single use.
UnicodeSetStringSpan::UnicodeSetStringSpan(const UnicodeSet &set,
                                           const UVector &setStrings,
                                           uint32_t which)
        : spanSet(0, 0x10ffff), pSpanNotSet(NULL), strings(setStrings),
          utf8Lengths(NULL), spanLengths(NULL), utf8(NULL),
          maxLength16(0), maxLength8(0),
          all((UBool)(which==ALL)) {
    spanSet.retainAll(set);
    if(which&NOT_CONTAINED) {
        // Default to the same sets.
        // addToSpanNotSet() will create a separate set if necessary.
        pSpanNotSet=&spanSet;
    }

    // Determine if the strings even need to be taken into account at all for span() etc.
    // Also count the lengths of the UTF-8 versions of the strings for memory allocation.
    int32_t stringsLength=strings.size();

    int32_t utf8Length=0;  // Length of all UTF-8 versions of relevant strings.

    int32_t i, spanLength;
    for(i=0; i<stringsLength; ++i) {
        const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
        const UChar *s16=string.getBuffer();
        int32_t length16=string.length();
        spanLength=spanSet.span(s16, length16, USET_SPAN_WHILE_CONTAINED);
        if(spanLength<length16) {  // Relevant string.
            if((which&UTF16) && length16>maxLength16) {
                maxLength16=length16;
            }
            if(which&UTF8) {
                int32_t length8=getUTF8Length(s16, length16);
                utf8Length+=length8;
                if(length8>maxLength8) {
                    maxLength8=length8;
                }
            }
        }
    }
    if((maxLength16|maxLength8)==0) {
        return;
    }

    // Freeze after checking for the need to use strings at all because freezing
    // a set takes some time and memory which are wasted if there are no relevant strings.
    if(all) {
        spanSet.freeze();
    }

    uint8_t *spanBackLengths;
    uint8_t *spanUTF8Lengths;
    uint8_t *spanBackUTF8Lengths;

    // Allocate a block of meta data.
    int32_t allocSize;
    if(all) {
        // UTF-8 lengths, 4 sets of span lengths, UTF-8 strings.
        allocSize=stringsLength*(4+1+1+1+1)+utf8Length;
    } else {
        allocSize=stringsLength;  // One set of span lengths.
        if(which&UTF8) {
            // UTF-8 lengths and UTF-8 strings.
            allocSize+=stringsLength*4+utf8Length;
        }
    }
    if(allocSize<=sizeof(staticLengths)) {
        utf8Lengths=staticLengths;
    } else {
        utf8Lengths=(int32_t *)uprv_malloc(allocSize);
        if(utf8Lengths==NULL) {
            maxLength16=maxLength8=0;  // Prevent usage by making needsStringSpanUTF16/8() return FALSE.
            return;  // Out of memory.
        }
    }

    if(all) {
        // Store span lengths for all span() variants.
        spanLengths=(uint8_t *)(utf8Lengths+stringsLength);
        spanBackLengths=spanLengths+stringsLength;
        spanUTF8Lengths=spanBackLengths+stringsLength;
        spanBackUTF8Lengths=spanUTF8Lengths+stringsLength;
        utf8=spanBackUTF8Lengths+stringsLength;
    } else {
        // Store span lengths for only one span() variant.
        if(which&UTF8) {
            spanLengths=(uint8_t *)(utf8Lengths+stringsLength);
            utf8=spanLengths+stringsLength;
        } else {
            spanLengths=(uint8_t *)utf8Lengths;
        }
        spanBackLengths=spanUTF8Lengths=spanBackUTF8Lengths=spanLengths;
    }

    // Set the meta data and pSpanNotSet and write the UTF-8 strings.
    int32_t utf8Count=0;  // Count UTF-8 bytes written so far.

    for(i=0; i<stringsLength; ++i) {
        const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
        const UChar *s16=string.getBuffer();
        int32_t length16=string.length();
        spanLength=spanSet.span(s16, length16, USET_SPAN_WHILE_CONTAINED);
        if(spanLength<length16) {  // Relevant string.
            if(which&UTF16) {
                if(which&CONTAINED) {
                    if(which&FWD) {
                        spanLengths[i]=makeSpanLengthByte(spanLength);
                    }
                    if(which&BACK) {
                        spanLength=length16-spanSet.spanBack(s16, length16, USET_SPAN_WHILE_CONTAINED);
                        spanBackLengths[i]=makeSpanLengthByte(spanLength);
                    }
                } else /* not CONTAINED, not all, but NOT_CONTAINED */ {
                    spanLengths[i]=spanBackLengths[i]=0;  // Only store a relevant/irrelevant flag.
                }
            }
            if(which&UTF8) {
                uint8_t *s8=utf8+utf8Count;
                int32_t length8=appendUTF8(s16, length16, s8, utf8Length-utf8Count);
                utf8Count+=utf8Lengths[i]=length8;
                if(length8==0) {  // Irrelevant for UTF-8 because not representable in UTF-8.
                    spanUTF8Lengths[i]=spanBackUTF8Lengths[i]=(uint8_t)ALL_CP_CONTAINED;
                } else {  // Relevant for UTF-8.
                    if(which&CONTAINED) {
                        if(which&FWD) {
                            spanLength=spanSet.spanUTF8((const char *)s8, length8, USET_SPAN_WHILE_CONTAINED);
                            spanUTF8Lengths[i]=makeSpanLengthByte(spanLength);
                        }
                        if(which&BACK) {
                            spanLength=length8-spanSet.spanBackUTF8((const char *)s8, length8, USET_SPAN_WHILE_CONTAINED);
                            spanBackUTF8Lengths[i]=makeSpanLengthByte(spanLength);
                        }
                    } else /* not CONTAINED, not all, but NOT_CONTAINED */ {
                        spanUTF8Lengths[i]=spanBackUTF8Lengths[i]=0;  // Only store a relevant/irrelevant flag.
                    }
                }
            }
            if(which&NOT_CONTAINED) {
                // Add string start and end code points to the spanNotSet so that
                // a span(while not contained) stops before any string.
                UChar32 c;
                if(which&FWD) {
                    int32_t len=0;
                    U16_NEXT(s16, len, length16, c);
                    addToSpanNotSet(c);
                }
                if(which&BACK) {
                    int32_t len=length16;
                    U16_PREV(s16, 0, len, c);
                    addToSpanNotSet(c);
                }
            }
        } else {  // Irrelevant string.
            if(all) {
                utf8Lengths[i]=0;
                spanLengths[i]=spanBackLengths[i]=
                    spanUTF8Lengths[i]=spanBackUTF8Lengths[i]=
                        (uint8_t)ALL_CP_CONTAINED;
            } else {
                if(which&UTF8) {
                    utf8Lengths[i]=0;
                }
                // All spanXYZLengths pointers contain the same address.
                spanLengths[i]=(uint8_t)ALL_CP_CONTAINED;
            }
        }
    }

    // Finish.
    if(all) {
        pSpanNotSet->freeze();
    }
}

UnicodeSetStringSpan::~UnicodeSetStringSpan() {
    if(pSpanNotSet!=NULL && pSpanNotSet!=&spanSet) {
        delete pSpanNotSet;
    }
    if(utf8Lengths!=NULL && utf8Lengths!=staticLengths) {
        uprv_free(utf8Lengths);
    }
}

void UnicodeSetStringSpan::addToSpanNotSet(UChar32 c) {
    if(pSpanNotSet==NULL || pSpanNotSet==&spanSet) {
        if(spanSet.contains(c)) {
            return;  // Nothing to do.
        }
        UnicodeSet *newSet=(UnicodeSet *)spanSet.cloneAsThawed();
        if(newSet==NULL) {
            return;  // Out of memory.
        } else {
            pSpanNotSet=newSet;
        }
    }
    pSpanNotSet->add(c);
}

// Compare strings without any argument checks. Requires length>0.
static inline UBool
matches16(const UChar *s, const UChar *t, int32_t length) {
    do {
        if(*s++!=*t++) {
            return FALSE;
        }
    } while(--length>0);
    return TRUE;
}

static inline UBool
matches8(const uint8_t *s, const uint8_t *t, int32_t length) {
    do {
        if(*s++!=*t++) {
            return FALSE;
        }
    } while(--length>0);
    return TRUE;
}

// Does the set contain the next code point?
// If so, return its length; otherwise return its negative length.
static inline int32_t
spanOne(const UnicodeSet &set, const UChar *s, int32_t length) {
    UChar c=*s, c2;
    if(c>=0xd800 && c<=0xdbff && length>=2 && U16_IS_TRAIL(c2=s[1])) {
        return set.contains(U16_GET_SUPPLEMENTARY(c, c2)) ? 2 : -2;
    }
    return set.contains(c) ? 1 : -1;
}

static inline int32_t
spanOneBack(const UnicodeSet &set, const UChar *s, int32_t length) {
    UChar c=s[length-1], c2;
    if(c>=0xdc00 && c<=0xdfff && length>=2 && U16_IS_TRAIL(c2=s[length-2])) {
        return set.contains(U16_GET_SUPPLEMENTARY(c2, c)) ? 2 : -2;
    }
    return set.contains(c) ? 1 : -1;
}

static inline int32_t
spanOneUTF8(const UnicodeSet &set, const uint8_t *s, int32_t length) {
    UChar32 c=*s;
    if((int8_t)c>=0) {
        return set.contains(c) ? 1 : -1;
    }
    // Take advantage of non-ASCII fastpaths in U8_NEXT().
    int32_t i=0;
    U8_NEXT(s, i, length, c);
    return set.contains(c) ? i : -i;
}

static inline int32_t
spanOneBackUTF8(const UnicodeSet &set, const uint8_t *s, int32_t length) {
    UChar32 c=s[length-1];
    if((int8_t)c>=0) {
        return set.contains(c) ? 1 : -1;
    }
    int32_t i=length-1;
    c=utf8_prevCharSafeBody(s, 0, &i, c, -1);
    length-=i;
    return set.contains(c) ? length : -length;
}

/*
 * Note: In span() when spanLength==0 (after a string match, or at the beginning
 * after an empty code point span) and in spanNot() and spanNotUTF8(),
 * string matching could use a binary search
 * because all string matches are done from the same start index.
 *
 * For UTF-8, this would require a comparison function that returns UTF-16 order.
 * This should not be necessary for normal UnicodeSets because
 * most sets have no strings, and most sets with strings have
 * very few very short strings.
 * For cases with many strings, it might be better to use a different API
 * and implementation with a DFA (state machine).
 */

/*
 * TODO: span algorithm
 */

int32_t UnicodeSetStringSpan::span(const UChar *s, int32_t length, USetSpanCondition spanCondition) const {
    if(spanCondition==USET_SPAN_WHILE_NOT_CONTAINED) {
        return spanNot(s, length);
    }
    int32_t spanLength=spanSet.span(s, length, USET_SPAN_WHILE_CONTAINED);
    if(spanLength==length) {
        return length;
    }

    // Consider strings; they may overlap with the span.
    OffsetList offsets;
    int32_t maxInc, maxIncReset;
    if(spanCondition==USET_SPAN_WHILE_CONTAINED) {
        // Use index list to try all possibilities.
        offsets.setMaxLength(maxLength16);
        maxIncReset=-1;
    } else /* USET_SPAN_WHILE_LONGEST_MATCH */ {
        // Use longest match.
        maxIncReset=0;
    }
    int32_t pos=spanLength, rest=length-pos;
    int32_t i, stringsLength=strings.size();
    for(;;) {
        maxInc=maxIncReset;
        for(i=0; i<stringsLength; ++i) {
            int32_t overlap=spanLengths[i];
            if(overlap==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
            const UChar *s16=string.getBuffer();
            int32_t length16=string.length();

            // Try to match this string at pos-overlap..pos.
            if(overlap==LONG_SPAN) {
                overlap=length16;  // Length of the string minus the last code point.
                U16_BACK_1(s16, 0, overlap);
            }
            if(overlap>spanLength) {
                overlap=spanLength;
            }
            int32_t inc=length16-overlap;  // Keep overlap+inc==length16.
            for(;;) {
                if(inc>rest) {
                    break;
                }
                // Try to match if the increment is not listed already.
                // (The string might start with a trail surrogate. Make sure to not overlap
                // into a surrogate pair.)
                if( (maxInc>=0 ? inc>maxInc : !offsets.containsOffset(inc)) &&
                    matches16(s+pos-overlap, s16, length16) &&
                    !(overlap>0 && U16_IS_TRAIL(s[pos-overlap]) &&
                      overlap<pos && U16_IS_LEAD(s[pos-overlap-1]))
                ) {
                    if(inc==rest) {
                        return length;  // Reached the end of the string.
                    }
                    if(maxInc>=0) {
                        maxInc=inc;  // Longest match.
                    } else {
                        offsets.addOffset(inc);
                    }
                }
                if(overlap==0) {
                    break;
                }
                --overlap;
                ++inc;
            }
        }
        // Finished trying to match all strings at pos.

        if(maxInc>0) {
            // Longest-match algorithm, and there was a string match.
            // Simply continue after it.
            pos+=maxInc;
            rest-=maxInc;
            spanLength=0;  // Match strings from after a string match.
            continue;
        }
        // if(maxInc==0) then indexes is unused and empty. (No string match.)
        // if(maxInc<0) then indexes is used, and checked for string matches.

        if(spanLength!=0 || pos==0) {
            // The position is after an unlimited code point span (spanLength!=0),
            // not after a string match.
            // The only position where spanLength==0 after a span is pos==0.
            // Otherwise, an unlimited code point span is only tried again when no
            // strings match, and if such a non-initial span fails we stop.
            if(offsets.isEmpty()) {
                return pos;  // No strings matched after a span.
            }
            // Match strings from after the next string match.
        } else {
            // The position is after a string match (or a single code point).
            if(offsets.isEmpty()) {
                // No more strings matched after a previous string match.
                // Try another code point span from after the last string match.
                spanLength=spanSet.span(s+pos, rest, USET_SPAN_WHILE_CONTAINED);
                pos+=spanLength;
                if( pos==length ||      // Reached the end of the string, or
                    spanLength==0       // neither strings nor span progressed.
                ) {
                    return pos;
                }
                continue;  // spanLength>0: Match strings from after a span.
            } else {
                // Try to match only one code point from after a string match if some
                // string matched beyond it, so that we try all possible positions
                // and don't overshoot.
                spanLength=spanOne(spanSet, s+pos, rest);
                if(spanLength>0) {
                    if(spanLength==rest) {
                        return length;  // Reached the end of the string.
                    }
                    // Match strings after this code point.
                    // There cannot be any increments below it because UnicodeSet strings
                    // contain multiple code points.
                    pos+=spanLength;
                    offsets.shift(spanLength);
                    spanLength=0;
                    continue;  // Match strings from after a single code point.
                }
                // Match strings from after the next string match.
            }
        }
        pos+=offsets.popMinimum();
        rest=length-pos;
        spanLength=0;  // Match strings from after a string match.
    }
}

int32_t UnicodeSetStringSpan::spanBack(const UChar *s, int32_t length, USetSpanCondition spanCondition) const {
    if(spanCondition==USET_SPAN_WHILE_NOT_CONTAINED) {
        return spanNotBack(s, length);
    }
    int32_t pos=spanSet.spanBack(s, length, USET_SPAN_WHILE_CONTAINED);
    if(pos==0) {
        return 0;
    }
    int32_t spanLength=length-pos;

    // Consider strings; they may overlap with the span.
    OffsetList offsets;
    int32_t maxDec, maxDecReset;
    if(spanCondition==USET_SPAN_WHILE_CONTAINED) {
        // Use index list to try all possibilities.
        offsets.setMaxLength(maxLength16);
        maxDecReset=-1;
    } else /* USET_SPAN_WHILE_LONGEST_MATCH */ {
        // Use longest match.
        maxDecReset=0;
    }
    int32_t i, stringsLength=strings.size();
    uint8_t *spanBackLengths=spanLengths;
    if(all) {
        spanBackLengths+=stringsLength;
    }
    for(;;) {
        maxDec=maxDecReset;
        for(i=0; i<stringsLength; ++i) {
            int32_t overlap=spanBackLengths[i];
            if(overlap==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
            const UChar *s16=string.getBuffer();
            int32_t length16=string.length();

            // Try to match this string at pos-(length16-overlap)..pos-length16.
            int32_t dec;
            if(overlap==LONG_SPAN) {
                dec=0;
                U16_FWD_1(s16, dec, length16);
                overlap=(length16-dec);  // Length of the string minus the first code point.
            }
            if(overlap>spanLength) {
                overlap=spanLength;
            }
            dec=length16-overlap;  // Keep dec+overlap==length16.
            for(;;) {
                if(dec>pos) {
                    break;
                }
                // Try to match if the decrement is not listed already.
                // (The string might end with a lead surrogate. Make sure to not overlap
                // into a surrogate pair.)
                if( (maxDec>=0 ? dec>maxDec : !offsets.containsOffset(dec)) &&
                    matches16(s+pos-dec, s16, length16) &&
                    !(overlap>0 && U16_IS_LEAD(s[pos+overlap-1]) &&
                      (pos+overlap)<length && U16_IS_TRAIL(s[pos+overlap]))
                ) {
                    if(dec==pos) {
                        return 0;  // Reached the start of the string.
                    }
                    if(maxDec>=0) {
                        maxDec=dec;  // Longest match.
                    } else {
                        offsets.addOffset(dec);
                    }
                }
                if(overlap==0) {
                    break;
                }
                --overlap;
                ++dec;
            }
        }
        // Finished trying to match all strings at pos.

        if(maxDec>0) {
            // Longest-match algorithm, and there was a string match.
            // Simply continue after it.
            pos-=maxDec;
            spanLength=0;  // Match strings from after a string match.
            continue;
        }
        // if(maxDec==0) then indexes is unused and empty. (No string match.)
        // if(maxDec<0) then indexes is used, and checked for string matches.

        if(spanLength!=0 || pos==length) {
            // The position is before an unlimited code point span (spanLength!=0),
            // not before a string match.
            // The only position where spanLength==0 before a span is pos==length.
            // Otherwise, an unlimited code point span is only tried again when no
            // strings match, and if such a non-initial span fails we stop.
            if(offsets.isEmpty()) {
                return pos;  // No strings matched before a span.
            }
            // Match strings from before the next string match.
        } else {
            // The position is before a string match (or a single code point).
            if(offsets.isEmpty()) {
                // No more strings matched before a previous string match.
                // Try another code point span from before the last string match.
                int32_t oldPos=pos;
                pos=spanSet.spanBack(s, oldPos, USET_SPAN_WHILE_CONTAINED);
                spanLength=oldPos-pos;
                if( pos==0 ||           // Reached the start of the string, or
                    spanLength==0       // neither strings nor span progressed.
                ) {
                    return pos;
                }
                continue;  // spanLength>0: Match strings from before a span.
            } else {
                // Try to match only one code point from before a string match if some
                // string matched beyond it, so that we try all possible positions
                // and don't overshoot.
                spanLength=spanOneBack(spanSet, s, pos);
                if(spanLength>0) {
                    if(spanLength==pos) {
                        return 0;  // Reached the start of the string.
                    }
                    // Match strings before this code point.
                    // There cannot be any decrements below it because UnicodeSet strings
                    // contain multiple code points.
                    pos-=spanLength;
                    offsets.shift(spanLength);
                    spanLength=0;
                    continue;  // Match strings from before a single code point.
                }
                // Match strings from before the next string match.
            }
        }
        pos-=offsets.popMinimum();
        spanLength=0;  // Match strings from before a string match.
    }
}

int32_t UnicodeSetStringSpan::spanUTF8(const uint8_t *s, int32_t length, USetSpanCondition spanCondition) const {
    if(spanCondition==USET_SPAN_WHILE_NOT_CONTAINED) {
        return spanNotUTF8(s, length);
    }
    int32_t spanLength=spanSet.spanUTF8((const char *)s, length, USET_SPAN_WHILE_CONTAINED);
    if(spanLength==length) {
        return length;
    }

    // Consider strings; they may overlap with the span.
    OffsetList offsets;
    int32_t maxInc, maxIncReset;
    if(spanCondition==USET_SPAN_WHILE_CONTAINED) {
        // Use index list to try all possibilities.
        offsets.setMaxLength(maxLength8);
        maxIncReset=-1;
    } else /* USET_SPAN_WHILE_LONGEST_MATCH */ {
        // Use longest match.
        maxIncReset=0;
    }
    int32_t pos=spanLength, rest=length-pos;
    int32_t i, stringsLength=strings.size();
    uint8_t *spanUTF8Lengths=spanLengths;
    if(all) {
        spanUTF8Lengths+=2*stringsLength;
    }
    for(;;) {
        const uint8_t *s8=utf8;
        int32_t length8;
        maxInc=maxIncReset;
        for(i=0; i<stringsLength; ++i) {
            int32_t overlap=spanUTF8Lengths[i];
            if(overlap==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            length8=utf8Lengths[i];

            // Try to match this string at pos-overlap..pos.
            if(overlap==LONG_SPAN) {
                overlap=length8;  // Length of the string minus the last code point.
                U8_BACK_1(s8, 0, overlap);
            }
            if(overlap>spanLength) {
                overlap=spanLength;
            }
            int32_t inc=length8-overlap;  // Keep overlap+inc==length8.
            for(;;) {
                if(inc>rest) {
                    break;
                }
                // Try to match if the increment is not listed already.
                // Match at code point boundaries. (The UTF-8 strings were converted
                // from UTF-16 and are guaranteed to be well-formed.)
                if( !U8_IS_TRAIL(s[pos-overlap]) &&
                    (maxInc>=0 ? inc>maxInc : !offsets.containsOffset(inc)) &&
                    matches8(s+pos-overlap, s8, length8)
                    
                ) {
                    if(inc==rest) {
                        return length;  // Reached the end of the string.
                    }
                    if(maxInc>=0) {
                        maxInc=inc;  // Longest match.
                    } else {
                        offsets.addOffset(inc);
                    }
                }
                if(overlap==0) {
                    break;
                }
                --overlap;
                ++inc;
            }
            s8+=length8;
        }
        // Finished trying to match all strings at pos.

        if(maxInc>0) {
            // Longest-match algorithm, and there was a string match.
            // Simply continue after it.
            pos+=maxInc;
            rest-=maxInc;
            spanLength=0;  // Match strings from after a string match.
            continue;
        }
        // if(maxInc==0) then indexes is unused and empty. (No string match.)
        // if(maxInc<0) then indexes is used, and checked for string matches.

        if(spanLength!=0 || pos==0) {
            // The position is after an unlimited code point span (spanLength!=0),
            // not after a string match.
            // The only position where spanLength==0 after a span is pos==0.
            // Otherwise, an unlimited code point span is only tried again when no
            // strings match, and if such a non-initial span fails we stop.
            if(offsets.isEmpty()) {
                return pos;  // No strings matched after a span.
            }
            // Match strings from after the next string match.
        } else {
            // The position is after a string match (or a single code point).
            if(offsets.isEmpty()) {
                // No more strings matched after a previous string match.
                // Try another code point span from after the last string match.
                spanLength=spanSet.spanUTF8((const char *)s+pos, rest, USET_SPAN_WHILE_CONTAINED);
                pos+=spanLength;
                if( pos==length ||      // Reached the end of the string, or
                    spanLength==0       // neither strings nor span progressed.
                ) {
                    return pos;
                }
                continue;  // spanLength>0: Match strings from after a span.
            } else {
                // Try to match only one code point from after a string match if some
                // string matched beyond it, so that we try all possible positions
                // and don't overshoot.
                spanLength=spanOneUTF8(spanSet, s+pos, rest);
                if(spanLength>0) {
                    if(spanLength==rest) {
                        return length;  // Reached the end of the string.
                    }
                    // Match strings after this code point.
                    // There cannot be any increments below it because UnicodeSet strings
                    // contain multiple code points.
                    pos+=spanLength;
                    offsets.shift(spanLength);
                    spanLength=0;
                    continue;  // Match strings from after a single code point.
                }
                // Match strings from after the next string match.
            }
        }
        pos+=offsets.popMinimum();
        rest=length-pos;
        spanLength=0;  // Match strings from after a string match.
    }
}

int32_t UnicodeSetStringSpan::spanBackUTF8(const uint8_t *s, int32_t length, USetSpanCondition spanCondition) const {
    if(spanCondition==USET_SPAN_WHILE_NOT_CONTAINED) {
        return spanNotBackUTF8(s, length);
    }
    int32_t pos=spanSet.spanBackUTF8((const char *)s, length, USET_SPAN_WHILE_CONTAINED);
    if(pos==0) {
        return 0;
    }
    int32_t spanLength=length-pos;

    // Consider strings; they may overlap with the span.
    OffsetList offsets;
    int32_t maxDec, maxDecReset;
    if(spanCondition==USET_SPAN_WHILE_CONTAINED) {
        // Use index list to try all possibilities.
        offsets.setMaxLength(maxLength8);
        maxDecReset=-1;
    } else /* USET_SPAN_WHILE_LONGEST_MATCH */ {
        // Use longest match.
        maxDecReset=0;
    }
    int32_t i, stringsLength=strings.size();
    uint8_t *spanBackUTF8Lengths=spanLengths;
    if(all) {
        spanBackUTF8Lengths+=3*stringsLength;
    }
    for(;;) {
        const uint8_t *s8=utf8;
        int32_t length8;
        maxDec=maxDecReset;
        for(i=0; i<stringsLength; ++i) {
            int32_t overlap=spanBackUTF8Lengths[i];
            if(overlap==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            length8=utf8Lengths[i];

            // Try to match this string at pos-(length8-overlap)..pos-length8.
            int32_t dec;
            if(overlap==LONG_SPAN) {
                dec=0;
                U8_FWD_1(s8, dec, length8);
                overlap=(length8-dec);  // Length of the string minus the first code point.
            }
            if(overlap>spanLength) {
                overlap=spanLength;
            }
            dec=length8-overlap;  // Keep dec+overlap==length8.
            for(;;) {
                if(dec>pos) {
                    break;
                }
                // Try to match if the decrement is not listed already.
                // Match at code point boundaries. (The UTF-8 strings were converted
                // from UTF-16 and are guaranteed to be well-formed.)
                if( !U8_IS_TRAIL(s[pos-dec]) &&
                    (maxDec>=0 ? dec>maxDec : !offsets.containsOffset(dec)) &&
                    matches8(s+pos-dec, s8, length8)
                ) {
                    if(dec==pos) {
                        return 0;  // Reached the start of the string.
                    }
                    if(maxDec>=0) {
                        maxDec=dec;  // Longest match.
                    } else {
                        offsets.addOffset(dec);
                    }
                }
                if(overlap==0) {
                    break;
                }
                --overlap;
                ++dec;
            }
            s8+=length8;
        }
        // Finished trying to match all strings at pos.

        if(maxDec>0) {
            // Longest-match algorithm, and there was a string match.
            // Simply continue after it.
            pos-=maxDec;
            spanLength=0;  // Match strings from after a string match.
            continue;
        }
        // if(maxDec==0) then indexes is unused and empty. (No string match.)
        // if(maxDec<0) then indexes is used, and checked for string matches.

        if(spanLength!=0 || pos==length) {
            // The position is before an unlimited code point span (spanLength!=0),
            // not before a string match.
            // The only position where spanLength==0 before a span is pos==length.
            // Otherwise, an unlimited code point span is only tried again when no
            // strings match, and if such a non-initial span fails we stop.
            if(offsets.isEmpty()) {
                return pos;  // No strings matched before a span.
            }
            // Match strings from before the next string match.
        } else {
            // The position is before a string match (or a single code point).
            if(offsets.isEmpty()) {
                // No more strings matched before a previous string match.
                // Try another code point span from before the last string match.
                int32_t oldPos=pos;
                pos=spanSet.spanBackUTF8((const char *)s, oldPos, USET_SPAN_WHILE_CONTAINED);
                spanLength=oldPos-pos;
                if( pos==0 ||           // Reached the start of the string, or
                    spanLength==0       // neither strings nor span progressed.
                ) {
                    return pos;
                }
                continue;  // spanLength>0: Match strings from before a span.
            } else {
                // Try to match only one code point from before a string match if some
                // string matched beyond it, so that we try all possible positions
                // and don't overshoot.
                spanLength=spanOneBackUTF8(spanSet, s, pos);
                if(spanLength>0) {
                    if(spanLength==pos) {
                        return 0;  // Reached the start of the string.
                    }
                    // Match strings before this code point.
                    // There cannot be any decrements below it because UnicodeSet strings
                    // contain multiple code points.
                    pos-=spanLength;
                    offsets.shift(spanLength);
                    spanLength=0;
                    continue;  // Match strings from before a single code point.
                }
                // Match strings from before the next string match.
            }
        }
        pos-=offsets.popMinimum();
        spanLength=0;  // Match strings from before a string match.
    }
}

/*
 * TODO: spanNot algorithm
 */

int32_t UnicodeSetStringSpan::spanNot(const UChar *s, int32_t length) const {
    int32_t pos=0, rest=length;
    int32_t i, stringsLength=strings.size();
    do {
        // Span until we find a code point from the set,
        // or a code point that starts or ends some string.
        i=pSpanNotSet->span(s+pos, rest, USET_SPAN_WHILE_NOT_CONTAINED);
        if(i==rest) {
            return length;  // Reached the end of the string.
        }
        pos+=i;
        rest-=i;

        // Try to match the strings at pos.
        for(i=0; i<stringsLength; ++i) {
            if(spanLengths[i]==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
            const UChar *s16=string.getBuffer();
            int32_t length16=string.length();
            if(length16<=rest && matches16(s+pos, s16, length16)) {
                return pos;  // There is a set element at pos.
            }
        }

        // Check whether the current code point is in the original set,
        // without the string starts and ends.
        i=spanOne(spanSet, s+pos, rest);
        if(i>0) {
            return pos;  // There is a set element at pos.
        } else /* i<0 */ {
            // The span(while not contained) ended on a string start/end which is
            // not in the original set. Skip this code point and continue.
            pos-=i;
            rest+=i;
        }
    } while(rest!=0);
    return length;  // Reached the end of the string.
}

int32_t UnicodeSetStringSpan::spanNotBack(const UChar *s, int32_t length) const {
    int32_t pos=length;
    int32_t i, stringsLength=strings.size();
    do {
        // Span until we find a code point from the set,
        // or a code point that starts or ends some string.
        pos=pSpanNotSet->spanBack(s, pos, USET_SPAN_WHILE_NOT_CONTAINED);
        if(pos==0) {
            return 0;  // Reached the start of the string.
        }

        // Try to match the strings at pos.
        for(i=0; i<stringsLength; ++i) {
            // Use spanLengths rather than a spanBackLengths pointer because
            // it is easier and we only need to know whether the string is irrelevant
            // which is the same in either array.
            if(spanLengths[i]==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            const UnicodeString &string=*(const UnicodeString *)strings.elementAt(i);
            const UChar *s16=string.getBuffer();
            int32_t length16=string.length();
            if(length16<=pos && matches16(s+pos-length16, s16, length16)) {
                return pos;  // There is a set element at pos.
            }
        }

        // Check whether the current code point is in the original set,
        // without the string starts and ends.
        i=spanOneBack(spanSet, s, pos);
        if(i>0) {
            return pos;  // There is a set element at pos.
        } else /* i<0 */ {
            // The span(while not contained) ended on a string start/end which is
            // not in the original set. Skip this code point and continue.
            pos+=i;
        }
    } while(pos!=0);
    return length;  // Reached the start of the string.
}

int32_t UnicodeSetStringSpan::spanNotUTF8(const uint8_t *s, int32_t length) const {
    int32_t pos=0, rest=length;
    int32_t i, stringsLength=strings.size();
    uint8_t *spanUTF8Lengths=spanLengths;
    if(all) {
        spanUTF8Lengths+=2*stringsLength;
    }
    do {
        // Span until we find a code point from the set,
        // or a code point that starts or ends some string.
        i=pSpanNotSet->spanUTF8((const char *)s+pos, rest, USET_SPAN_WHILE_NOT_CONTAINED);
        if(i==rest) {
            return length;  // Reached the end of the string.
        }
        pos+=i;
        rest-=i;

        // Try to match the strings at pos.
        const uint8_t *s8=utf8;
        int32_t length8;
        for(i=0; i<stringsLength; ++i) {
            if(spanUTF8Lengths[i]==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            length8=utf8Lengths[i];
            if(length8<=rest && matches8(s+pos, s8, length8)) {
                return pos;  // There is a set element at pos.
            }
            s8+=length8;
        }

        // Check whether the current code point is in the original set,
        // without the string starts and ends.
        i=spanOneUTF8(spanSet, s+pos, rest);
        if(i>0) {
            return pos;  // There is a set element at pos.
        } else /* i<0 */ {
            // The span(while not contained) ended on a string start/end which is
            // not in the original set. Skip this code point and continue.
            pos-=i;
            rest+=i;
        }
    } while(rest!=0);
    return length;  // Reached the end of the string.
}

int32_t UnicodeSetStringSpan::spanNotBackUTF8(const uint8_t *s, int32_t length) const {
    int32_t pos=length;
    int32_t i, stringsLength=strings.size();
    uint8_t *spanBackUTF8Lengths=spanLengths;
    if(all) {
        spanBackUTF8Lengths+=3*stringsLength;
    }
    do {
        // Span until we find a code point from the set,
        // or a code point that starts or ends some string.
        pos=pSpanNotSet->spanBackUTF8((const char *)s, pos, USET_SPAN_WHILE_NOT_CONTAINED);
        if(pos==0) {
            return 0;  // Reached the start of the string.
        }

        // Try to match the strings at pos.
        const uint8_t *s8=utf8;
        int32_t length8;
        for(i=0; i<stringsLength; ++i) {
            if(spanBackUTF8Lengths[i]==ALL_CP_CONTAINED) {
                continue;  // Irrelevant string.
            }
            length8=utf8Lengths[i];
            if(length8<=pos && matches8(s+pos-length8, s8, length8)) {
                return pos;  // There is a set element at pos.
            }
            s8+=length8;
        }

        // Check whether the current code point is in the original set,
        // without the string starts and ends.
        i=spanOneBackUTF8(spanSet, s, pos);
        if(i>0) {
            return pos;  // There is a set element at pos.
        } else /* i<0 */ {
            // The span(while not contained) ended on a string start/end which is
            // not in the original set. Skip this code point and continue.
            pos+=i;
        }
    } while(pos!=0);
    return length;  // Reached the start of the string.
}

U_NAMESPACE_END
