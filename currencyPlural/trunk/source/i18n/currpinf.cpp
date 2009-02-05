/*
 *******************************************************************************
 * Copyright (C) 2009, International Business Machines Corporation and         *
 * others. All Rights Reserved.                                                *
 *******************************************************************************
 */

#include "unicode/currpinf.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/locid.h"
#include "unicode/plurrule.h"
#include "unicode/ures.h"
#include "cstring.h"
#include "hash.h"
#include "uresimp.h"

U_NAMESPACE_BEGIN

U_CDECL_BEGIN

/**
 * @internal ICU 4.2
 */
static UBool U_CALLCONV ValueComparator(UHashTok val1, UHashTok val2);

U_CDECL_END

UBool
U_CALLCONV ValueComparator(UHashTok val1, UHashTok val2) {
    const UnicodeString* affix_1 = (UnicodeString*)val1.pointer;
    const UnicodeString* affix_2 = (UnicodeString*)val2.pointer;
    return  *affix_1 == *affix_2;
}

//#define CURRPINF_DEBUG

#ifdef CURRPINF_DEBUG
#include "stdio.h"
#endif

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(CurrencyPluralInfo)

static const UChar gDefaultCurrencyPluralPattern[] = {'0', '.', '#', '#', ' ', 0xA4, 0xA4, 0xA4, 0};
static const UChar gTripleCurrencySign[] = {0xA4, 0xA4, 0xA4, 0};
static const UChar gPluralCountOther[] = {0x6F, 0x74, 0x68, 0x65, 0x72, 0};
static const UChar gPart0[] = {0x7B, 0x30, 0x7D, 0};
static const UChar gPart1[] = {0x7B, 0x31, 0x7D, 0};

static const char gNumberPatternsTag[]="NumberPatterns";
static const char gCurrUnitPtnTag[]="CurrencyUnitPatterns";

CurrencyPluralInfo::CurrencyPluralInfo(UErrorCode& status)
:   fPluralCountToCurrencyUnitPattern(NULL),
    fPluralRules(NULL),
    fLocale(NULL) {
    initialize(Locale::getDefault(), status);
}

CurrencyPluralInfo::CurrencyPluralInfo(const Locale& locale, UErrorCode& status)
:   fPluralCountToCurrencyUnitPattern(NULL),
    fPluralRules(NULL),
    fLocale(NULL) {
    initialize(locale, status);
}

CurrencyPluralInfo::CurrencyPluralInfo(const CurrencyPluralInfo& info) 
:   UObject(info),
    fPluralCountToCurrencyUnitPattern(NULL),
    fPluralRules(NULL),
    fLocale(NULL) {
    *this = info;
}


CurrencyPluralInfo&
CurrencyPluralInfo::operator=(const CurrencyPluralInfo& info) {
    if (this == &info) {
        return *this;
    }

    deleteHash(fPluralCountToCurrencyUnitPattern);
    UErrorCode status = U_ZERO_ERROR;
    fPluralCountToCurrencyUnitPattern = initHash(status);
    copyHash(info.fPluralCountToCurrencyUnitPattern, 
             fPluralCountToCurrencyUnitPattern, status);
    if ( U_FAILURE(status) ) {
        return *this;
    }

    delete fPluralRules;
    delete fLocale;
    if (info.fPluralRules) {
        fPluralRules = info.fPluralRules->clone();
    } else {
        fPluralRules = NULL;
    }
    if (info.fLocale) {
        fLocale = info.fLocale->clone();
    } else {
        fLocale = NULL;
    }
    return *this;
}


CurrencyPluralInfo::~CurrencyPluralInfo() {
    deleteHash(fPluralCountToCurrencyUnitPattern);
    fPluralCountToCurrencyUnitPattern = NULL;
    delete fPluralRules;
    delete fLocale;
    fPluralRules = NULL;
    fLocale = NULL;
}

UBool
CurrencyPluralInfo::operator==(const CurrencyPluralInfo& info) const {
    return *fPluralRules == *info.fPluralRules &&
           *fLocale == *info.fLocale &&
           fPluralCountToCurrencyUnitPattern->equals(*info.fPluralCountToCurrencyUnitPattern);
}


CurrencyPluralInfo*
CurrencyPluralInfo::clone() const {
    return new CurrencyPluralInfo(*this);
}

const PluralRules* 
CurrencyPluralInfo::getPluralRules() const {
    return fPluralRules;
}

UnicodeString&
CurrencyPluralInfo::getCurrencyPluralPattern(const UnicodeString&  pluralCount,
                                             UnicodeString& result) const {
    const UnicodeString* currencyPluralPattern = 
        (UnicodeString*)fPluralCountToCurrencyUnitPattern->get(pluralCount);
    if (currencyPluralPattern == NULL) {
        // fall back to "other"
        if (pluralCount.compare(gPluralCountOther)) {
            currencyPluralPattern = 
                (UnicodeString*)fPluralCountToCurrencyUnitPattern->get(gPluralCountOther);
        }
        if (currencyPluralPattern == NULL) {
            // no currencyUnitPatterns defined, 
            // fallback to predefined defult.
            // This should never happen when ICU resource files are
            // available, since currencyUnitPattern of "other" is always
            // defined in root.
            result = UnicodeString(gDefaultCurrencyPluralPattern);
            return result;
        }
    }
    result = *currencyPluralPattern;
    return result;
}

const Locale&
CurrencyPluralInfo::getLocale() const {
    return *fLocale;
}

void
CurrencyPluralInfo::setPluralRules(const UnicodeString& ruleDescription,
                                   UErrorCode& status) {
    if (U_SUCCESS(status)) {
        fPluralRules = PluralRules::createRules(ruleDescription, status);
    }
}


void
CurrencyPluralInfo::setCurrencyPluralPattern(const UnicodeString& pluralCount,
                                             const UnicodeString& pattern,
                                             UErrorCode& status) {
    if (U_SUCCESS(status)) {
        fPluralCountToCurrencyUnitPattern->put(pluralCount, new UnicodeString(pattern), status);
    }
}


void
CurrencyPluralInfo::setLocale(const Locale& loc, UErrorCode& status) {
    initialize(loc, status);
}


void 
CurrencyPluralInfo::initialize(const Locale& loc, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    delete fLocale;
    fLocale = loc.clone();
    fPluralRules = PluralRules::forLocale(loc, status);
    setupCurrencyPluralPattern(loc, status);
}

   
void
CurrencyPluralInfo::setupCurrencyPluralPattern(const Locale& loc, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    fPluralCountToCurrencyUnitPattern = initHash(status);
    if (U_FAILURE(status)) {
        return;
    }

    UErrorCode ec = U_ZERO_ERROR;
    UResourceBundle *rb = ures_open(NULL, loc.getName(), &ec);
    UResourceBundle *numberPatterns = ures_getByKey(rb, gNumberPatternsTag, NULL, &ec);
    int32_t ptnLen;
    // TODO: 0 to be NumberFormat::fNumberStyle
    const UChar* numberStylePattern = ures_getStringByIndex(numberPatterns, 0, 
                                                            &ptnLen, &ec);
    ures_close(numberPatterns);

    if (U_FAILURE(ec)) {
        ures_close(rb);
        return;
    }

    UResourceBundle *currencyRes = ures_getByKeyWithFallback(rb, gCurrUnitPtnTag, NULL, &ec);
    
    StringEnumeration* keywords = fPluralRules->getKeywords(ec);
    if (U_SUCCESS(ec)) {
        const char* pluralCount;
        while ((pluralCount = keywords->next(NULL, ec)) != NULL) {
            if ( U_SUCCESS(ec) ) {
                int32_t ptnLen;
                UErrorCode err = U_ZERO_ERROR;
                const UChar* patternChars = ures_getStringByKeyWithFallback(
                    currencyRes, pluralCount, &ptnLen, &err);
                if (U_SUCCESS(err) && ptnLen > 0) {
                    UnicodeString* pattern = new UnicodeString(patternChars, ptnLen);
#ifdef CURRPINF_DEBUG
                    char result_1[1000];
                    pattern->extract(0, pattern->length(), result_1, "UTF-8");
                    printf("pluralCount: %s; pattern: %s\n", pluralCount, result_1);
#endif
                    pattern->findAndReplace(gPart0, numberStylePattern);
                    pattern->findAndReplace(gPart1, gTripleCurrencySign);

#ifdef CURRPINF_DEBUG
                    pattern->extract(0, pattern->length(), result_1, "UTF-8");
                    printf("pluralCount: %s; pattern: %s\n", pluralCount, result_1);
#endif

                    fPluralCountToCurrencyUnitPattern->put(UnicodeString(pluralCount), pattern, status);
                }
            }
        }
    }
    delete keywords;
    ures_close(currencyRes);
    ures_close(rb);
}



void
CurrencyPluralInfo::deleteHash(Hashtable* hTable) 
{
    if ( hTable == NULL ) {
        return;
    }
    int32_t pos = -1;
    const UHashElement* element = NULL;
    while ( (element = hTable->nextElement(pos)) != NULL ) {
        const UHashTok keyTok = element->key;
        const UHashTok valueTok = element->value;
        const UnicodeString* value = (UnicodeString*)valueTok.pointer;
        delete value;
    }
    delete hTable;
    hTable = NULL;
}


Hashtable*
CurrencyPluralInfo::initHash(UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return NULL;
    }
    Hashtable* hTable;
    if ( (hTable = new Hashtable(TRUE, status)) == NULL ) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    hTable->setValueCompartor(ValueComparator);
    return hTable;
}


void
CurrencyPluralInfo::copyHash(const Hashtable* source,
                           Hashtable* target,
                           UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return;
    }
    int32_t pos = -1;
    const UHashElement* element = NULL;
    if ( source ) {
        while ( (element = source->nextElement(pos)) != NULL ) {
            const UHashTok keyTok = element->key;
            const UnicodeString* key = (UnicodeString*)keyTok.pointer;
            const UHashTok valueTok = element->value;
            const UnicodeString* value = (UnicodeString*)valueTok.pointer;
            UnicodeString* copy = new UnicodeString(*value);
            target->put(UnicodeString(*key), copy, status);
            if ( U_FAILURE(status) ) {
                return;
            }
        }
    }
}


U_NAMESPACE_END

#endif
