/*
*******************************************************************************
* Copyright (C) 1997-2010, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*
* File DCFMTSYM.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/19/97    aliu        Converted from java.
*   03/18/97    clhuang     Implemented with C++ APIs.
*   03/27/97    helena      Updated to pass the simple test after code review.
*   08/26/97    aliu        Added currency/intl currency symbol support.
*   07/20/98    stephen     Slightly modified initialization of monetarySeparator
********************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/dcfmtsym.h"
#include "unicode/ures.h"
#include "unicode/decimfmt.h"
#include "unicode/ucurr.h"
#include "unicode/choicfmt.h"
#include "unicode/unistr.h"
#include "unicode/numsys.h"
#include "ucurrimp.h"
#include "cstring.h"
#include "locbased.h"
#include "uresimp.h"
#include "ureslocs.h"

// *****************************************************************************
// class DecimalFormatSymbols
// *****************************************************************************

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DecimalFormatSymbols)

static const char gNumberElements[] = "NumberElements";
static const char gCurrencySpacingTag[] = "currencySpacing";
static const char gBeforeCurrencyTag[] = "beforeCurrency";
static const char gAfterCurrencyTag[] = "afterCurrency";
static const char gCurrencyMatchTag[] = "currencyMatch";
static const char gCurrencySudMatchTag[] = "surroundingMatch";
static const char gCurrencyInsertBtnTag[] = "insertBetween";


static const UChar INTL_CURRENCY_SYMBOL_STR[] = {0xa4, 0xa4, 0};

// -------------------------------------
// Initializes this with the decimal format symbols in the default locale.

DecimalFormatSymbols::DecimalFormatSymbols(UErrorCode& status)
    : UObject(),
    locale()
{
    initialize(locale, status);
}

// -------------------------------------
// Initializes this with the decimal format symbols in the desired locale.

DecimalFormatSymbols::DecimalFormatSymbols(const Locale& loc, UErrorCode& status)
    : UObject(),
    locale(loc)
{
    initialize(locale, status);
}

// -------------------------------------

DecimalFormatSymbols::~DecimalFormatSymbols()
{
}

// -------------------------------------
// copy constructor

DecimalFormatSymbols::DecimalFormatSymbols(const DecimalFormatSymbols &source)
    : UObject(source)
{
    *this = source;
}

// -------------------------------------
// assignment operator

DecimalFormatSymbols&
DecimalFormatSymbols::operator=(const DecimalFormatSymbols& rhs)
{
    if (this != &rhs) {
        for(int32_t i = 0; i < (int32_t)kFormatSymbolCount; ++i) {
            // fastCopyFrom is safe, see docs on fSymbols
            fSymbols[(ENumberFormatSymbol)i].fastCopyFrom(rhs.fSymbols[(ENumberFormatSymbol)i]);
        }
        for(int32_t i = 0; i < (int32_t)kCurrencySpacingCount; ++i) {
            currencySpcBeforeSym[i].fastCopyFrom(rhs.currencySpcBeforeSym[i]);
            currencySpcAfterSym[i].fastCopyFrom(rhs.currencySpcAfterSym[i]);
        }
        locale = rhs.locale;
        uprv_strcpy(validLocale, rhs.validLocale);
        uprv_strcpy(actualLocale, rhs.actualLocale);
    }
    return *this;
}

// -------------------------------------

UBool
DecimalFormatSymbols::operator==(const DecimalFormatSymbols& that) const
{
    if (this == &that) {
        return TRUE;
    }
    for(int32_t i = 0; i < (int32_t)kFormatSymbolCount; ++i) {
        if(fSymbols[(ENumberFormatSymbol)i] != that.fSymbols[(ENumberFormatSymbol)i]) {
            return FALSE;
        }
    }
    for(int32_t i = 0; i < (int32_t)kCurrencySpacingCount; ++i) {
        if(currencySpcBeforeSym[i] != that.currencySpcBeforeSym[i]) {
            return FALSE;
        }
        if(currencySpcAfterSym[i] != that.currencySpcAfterSym[i]) {
            return FALSE;
        }
    }
    return locale == that.locale &&
        uprv_strcmp(validLocale, that.validLocale) == 0 &&
        uprv_strcmp(actualLocale, that.actualLocale) == 0;
}

// -------------------------------------

void
DecimalFormatSymbols::initialize(const Locale& loc, UErrorCode& status)
{
    static const char *gNumberElementKeys[kFormatSymbolCount] = {
        "decimal",
        "group",
        "list",
        "percentSign",
        NULL, /* Native zero digit is deprecated from CLDR - get it from the numbering system */
        NULL, /* Pattern digit character is deprecated from CLDR - use # by default always */
        "minusSign",
        "plusSign",
        NULL, /* currency symbol - We don't really try to load this directly from CLDR until we know the currency */
        NULL, /* intl currency symbol - We don't really try to load this directly from CLDR until we know the currency */
        "currencyDecimal",
        "exponential",
        "perMille",
        NULL, /* Escape padding character - not in CLDR */
        "infinity",
        "nan",
        NULL, /* Significant digit symbol - not in CLDR */
        "currencyGroup",
    };

    static const char *gLatn =  "latn";
    static const char *gSymbols = "symbols";
    const char *nsName;
    const UChar *sym = NULL;
    int32_t len = 0;

    *validLocale = *actualLocale = 0;
    currPattern = NULL;
    if (U_FAILURE(status))
        return;

    const char* locStr = loc.getName();
    UResourceBundle numberElementsRes;
    ures_initStackObject(&numberElementsRes);
    ures_openFillIn(&numberElementsRes, NULL, locStr, &status);
    if (U_FAILURE(status)) {
        status = U_USING_DEFAULT_WARNING;
        initialize();
        return;
    }
    ures_getByKeyWithFallback(&numberElementsRes, gNumberElements, &numberElementsRes, &status);

    if (U_FAILURE(status)) {
        status = U_USING_DEFAULT_WARNING;
        initialize();
        return;
    } else {

        // First initialize all the symbols to the fallbacks for anything we can't find
        initialize();

        //
        // Next get the numbering system for this locale and set zero digit
        // and the digit string based on the numbering system for the locale
        //

        NumberingSystem* ns = NumberingSystem::createInstance(loc,status);
        if (U_SUCCESS(status) && ns->getRadix() == 10 && !ns->isAlgorithmic()) {
            nsName = ns->getName();
            UnicodeString *zeroDigit = new UnicodeString(ns->getDescription(),0,1);
            setSymbol(kZeroDigitSymbol,*zeroDigit);
            delete zeroDigit;
        } else {
            nsName = gLatn;
        }


        UResourceBundle latnSymbols;
        ures_initStackObject(&latnSymbols);
        ures_getByKeyWithFallback(&numberElementsRes, gLatn, &latnSymbols, &status);
        ures_getByKeyWithFallback(&latnSymbols, gSymbols, &latnSymbols, &status);
       
        UBool isLatn = !uprv_strcmp(nsName,gLatn);

        UErrorCode nlStatus = U_ZERO_ERROR;
        UResourceBundle *nonLatnSymbols = NULL;
        if ( !isLatn ) {
            nonLatnSymbols = ures_getByKeyWithFallback(&numberElementsRes, nsName, NULL, &nlStatus);
            ures_getByKeyWithFallback(nonLatnSymbols, gSymbols, nonLatnSymbols, &nlStatus);
        }


        UBool kMonetaryDecimalSet = FALSE;
        UBool kMonetaryGroupingSet = FALSE;
        for(int32_t i = 0; i<kFormatSymbolCount; i++) {
            if ( gNumberElementKeys[i] != NULL ) {
                UErrorCode localStatus = U_ZERO_ERROR;
                if ( !isLatn ) {
                    sym = ures_getStringByKeyWithFallback(nonLatnSymbols,gNumberElementKeys[i],&len,&localStatus);
                    // If we can't find the symbol in the numbering system specific resources,
                    // use the "latn" numbering system as the fallback.
                    if ( U_FAILURE(localStatus) ) {
                        localStatus = U_ZERO_ERROR;
                        sym = ures_getStringByKeyWithFallback(&latnSymbols,gNumberElementKeys[i],&len,&localStatus);
                    }
                } else {
                        sym = ures_getStringByKeyWithFallback(&latnSymbols,gNumberElementKeys[i],&len,&localStatus);
                }

                if ( U_SUCCESS(localStatus) ) {
                    setSymbol((ENumberFormatSymbol)i,sym);
                    if ( i == kMonetarySeparatorSymbol ) {
                        kMonetaryDecimalSet = TRUE;
                    } else if ( i == kMonetaryGroupingSeparatorSymbol ) {
                        kMonetaryGroupingSet = TRUE;
                    }
                }
            }
        }

        if ( !isLatn ) {
            ures_close(nonLatnSymbols);
        }

        // If monetary decimal or grouping were not explicitly set, then set them to be the
        // same as their non-monetary counterparts.

        if ( !kMonetaryDecimalSet ) {
            setSymbol(kMonetarySeparatorSymbol,fSymbols[kDecimalSeparatorSymbol]);
        }
        if ( !kMonetaryGroupingSet ) {
            setSymbol(kMonetaryGroupingSeparatorSymbol,fSymbols[kGroupingSeparatorSymbol]);
        }

        if (ns) {
            delete ns;
        }

        // Obtain currency data from the currency API.  This is strictly
        // for backward compatibility; we don't use DecimalFormatSymbols
        // for currency data anymore.
        UErrorCode internalStatus = U_ZERO_ERROR; // don't propagate failures out
        UChar curriso[4];
        UnicodeString tempStr;
        ucurr_forLocale(locStr, curriso, 4, &internalStatus);

        // Reuse numberElements[0] as a temporary buffer
        uprv_getStaticCurrencyName(curriso, locStr, tempStr, internalStatus);
        if (U_SUCCESS(internalStatus)) {
            fSymbols[kIntlCurrencySymbol] = curriso;
            fSymbols[kCurrencySymbol] = tempStr;
        }
        /* else use the default values. */

        U_LOCALE_BASED(locBased, *this);
        locBased.setLocaleIDs(ures_getLocaleByType(&numberElementsRes,
                              ULOC_VALID_LOCALE, &status),
                              ures_getLocaleByType(&numberElementsRes,
                              ULOC_ACTUAL_LOCALE, &status));
        
        //load the currency data
        UChar ucc[4]={0}; //Currency Codes are always 3 chars long
        int32_t uccLen = 4;
        const char* locName = loc.getName();
        UErrorCode localStatus = U_ZERO_ERROR;
        uccLen = ucurr_forLocale(locName, ucc, uccLen, &localStatus);

        if(U_SUCCESS(localStatus) && uccLen > 0) {
            char cc[4]={0};
            u_UCharsToChars(ucc, cc, uccLen);
            /* An explicit currency was requested */
            UResourceBundle *currency;
            currency = ures_open(U_ICUDATA_CURR, locStr, &localStatus);
            ures_getByKeyWithFallback(currency, "Currencies", currency, &localStatus);
            ures_getByKeyWithFallback(currency, cc, currency, &localStatus);
            if(U_SUCCESS(localStatus) && ures_getSize(currency)>2) { // the length is 3 if more data is present
                ures_getByIndex(currency, 2, currency, &localStatus);
                int32_t currPatternLen = 0;
                currPattern = ures_getStringByIndex(currency, (int32_t)0, &currPatternLen, &localStatus);
                UnicodeString decimalSep = ures_getStringByIndex(currency, (int32_t)1, NULL, &localStatus);
                UnicodeString groupingSep = ures_getStringByIndex(currency, (int32_t)2, NULL, &localStatus);
                if(U_SUCCESS(localStatus)){
                    fSymbols[kMonetaryGroupingSeparatorSymbol] = groupingSep;
                    fSymbols[kMonetarySeparatorSymbol] = decimalSep;
                    //pattern.setTo(TRUE, currPattern, currPatternLen);
                    status = localStatus;
                }
            }
            ures_close(currency);
            /* else An explicit currency was requested and is unknown or locale data is malformed. */
            /* ucurr_* API will get the correct value later on. */
        }
            // else ignore the error if no currency

        // Currency Spacing.
        localStatus = U_ZERO_ERROR;
        UResourceBundle currencySpcRes;
        ures_initStackObject(&currencySpcRes);
        ures_openFillIn(&currencySpcRes, U_ICUDATA_CURR, locStr, &localStatus);
        ures_getByKeyWithFallback(&currencySpcRes, gCurrencySpacingTag, &currencySpcRes, &localStatus);

        if (U_SUCCESS(localStatus)) {
            const char* keywords[kCurrencySpacingCount] = {
                gCurrencyMatchTag, gCurrencySudMatchTag, gCurrencyInsertBtnTag
            };
            localStatus = U_ZERO_ERROR;
            UResourceBundle dataRes;
            ures_initStackObject(&dataRes);
            ures_getByKeyWithFallback(&currencySpcRes, gBeforeCurrencyTag, &dataRes, &localStatus);
            if (U_SUCCESS(localStatus)) {
                localStatus = U_ZERO_ERROR;
                for (int32_t i = 0; i < kCurrencySpacingCount; i++) {
                  currencySpcBeforeSym[i] = ures_getStringByKey(&dataRes, keywords[i],
                                                            NULL, &localStatus);
                }
            }
            
            ures_getByKeyWithFallback(&currencySpcRes, gAfterCurrencyTag, &dataRes, &localStatus);
            if (U_SUCCESS(localStatus)) {
                localStatus = U_ZERO_ERROR;
                for (int32_t i = 0; i < kCurrencySpacingCount; i++) {
                  currencySpcAfterSym[i] = ures_getStringByKey(&dataRes, keywords[i],
                                                                NULL, &localStatus);
                }
            }
        }
    }
}

void
DecimalFormatSymbols::initialize() {
    /*
     * These strings used to be in static arrays, but the HP/UX aCC compiler
     * cannot initialize a static array with class constructors.
     *  markus 2000may25
     */
    fSymbols[kDecimalSeparatorSymbol] = (UChar)0x2e;    // '.' decimal separator
    fSymbols[kGroupingSeparatorSymbol].remove();        //     group (thousands) separator
    fSymbols[kPatternSeparatorSymbol] = (UChar)0x3b;    // ';' pattern separator
    fSymbols[kPercentSymbol] = (UChar)0x25;             // '%' percent sign
    fSymbols[kZeroDigitSymbol] = (UChar)0x30;           // '0' native 0 digit
    fSymbols[kDigitSymbol] = (UChar)0x23;               // '#' pattern digit
    fSymbols[kPlusSignSymbol] = (UChar)0x002b;          // '+' plus sign
    fSymbols[kMinusSignSymbol] = (UChar)0x2d;           // '-' minus sign
    fSymbols[kCurrencySymbol] = (UChar)0xa4;            // 'OX' currency symbol
    fSymbols[kIntlCurrencySymbol] = INTL_CURRENCY_SYMBOL_STR;
    fSymbols[kMonetarySeparatorSymbol] = (UChar)0x2e;   // '.' monetary decimal separator
    fSymbols[kExponentialSymbol] = (UChar)0x45;         // 'E' exponential
    fSymbols[kPerMillSymbol] = (UChar)0x2030;           // '%o' per mill
    fSymbols[kPadEscapeSymbol] = (UChar)0x2a;           // '*' pad escape symbol
    fSymbols[kInfinitySymbol] = (UChar)0x221e;          // 'oo' infinite
    fSymbols[kNaNSymbol] = (UChar)0xfffd;               // SUB NaN
    fSymbols[kSignificantDigitSymbol] = (UChar)0x0040;  // '@' significant digit
    fSymbols[kMonetaryGroupingSeparatorSymbol].remove(); // 
}

Locale
DecimalFormatSymbols::getLocale(ULocDataLocaleType type, UErrorCode& status) const {
    U_LOCALE_BASED(locBased, *this);
    return locBased.getLocale(type, status);
}

const UnicodeString&
DecimalFormatSymbols::getPatternForCurrencySpacing(ECurrencySpacing type,
                                                 UBool beforeCurrency,
                                                 UErrorCode& status) const {
    if (U_FAILURE(status)) {
      return fNoSymbol;  // always empty.
    }
    if (beforeCurrency) {
      return currencySpcBeforeSym[(int32_t)type];
    } else {
      return currencySpcAfterSym[(int32_t)type];
    }
}

void
DecimalFormatSymbols::setPatternForCurrencySpacing(ECurrencySpacing type,
                                                   UBool beforeCurrency,
                                             const UnicodeString& pattern) {
  if (beforeCurrency) {
    currencySpcBeforeSym[(int32_t)type] = pattern;
  } else {
    currencySpcAfterSym[(int32_t)type] =  pattern;
  }
}
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
