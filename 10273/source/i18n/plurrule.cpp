/*
*******************************************************************************
* Copyright (C) 2007-2013, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File plurrule.cpp
*/

#include <math.h>
#include <stdio.h>

#include "unicode/utypes.h"
#include "unicode/localpointer.h"
#include "unicode/plurrule.h"
#include "unicode/upluralrules.h"
#include "unicode/ures.h"
#include "cmemory.h"
#include "cstring.h"
#include "hash.h"
#include "mutex.h"
#include "patternprops.h"
#include "plurrule_impl.h"
#include "putilimp.h"
#include "ucln_in.h"
#include "ustrfmt.h"
#include "locutil.h"
#include "uassert.h"
#include "uvectr32.h"

#if !UCONFIG_NO_FORMATTING

U_NAMESPACE_BEGIN

#define ARRAY_SIZE(array) (int32_t)(sizeof array  / sizeof array[0])

static const UChar PLURAL_KEYWORD_OTHER[]={LOW_O,LOW_T,LOW_H,LOW_E,LOW_R,0};
static const UChar PLURAL_DEFAULT_RULE[]={LOW_O,LOW_T,LOW_H,LOW_E,LOW_R,COLON,SPACE,LOW_N,0};
static const UChar PK_IN[]={LOW_I,LOW_N,0};
static const UChar PK_NOT[]={LOW_N,LOW_O,LOW_T,0};
static const UChar PK_IS[]={LOW_I,LOW_S,0};
static const UChar PK_MOD[]={LOW_M,LOW_O,LOW_D,0};
static const UChar PK_AND[]={LOW_A,LOW_N,LOW_D,0};
static const UChar PK_OR[]={LOW_O,LOW_R,0};
static const UChar PK_VAR_N[]={LOW_N,0};
static const UChar PK_VAR_I[]={LOW_I,0};
static const UChar PK_VAR_F[]={LOW_F,0};
static const UChar PK_VAR_T[]={LOW_T,0};
static const UChar PK_VAR_V[]={LOW_V,0};
static const UChar PK_VAR_J[]={LOW_J,0};
static const UChar PK_WITHIN[]={LOW_W,LOW_I,LOW_T,LOW_H,LOW_I,LOW_N,0};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(PluralRules)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(PluralKeywordEnumeration)

PluralRules::PluralRules(UErrorCode& status)
:   UObject(),
    mRules(NULL),
    mParser(NULL)
{
    if (U_FAILURE(status)) {
        return;
    }
    mParser = new RuleParser();
    if (mParser==NULL) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
}

PluralRules::PluralRules(const PluralRules& other)
: UObject(other),
    mRules(NULL),
  mParser(NULL)
{
    *this=other;
}

PluralRules::~PluralRules() {
    delete mRules;
    delete mParser;
}

PluralRules*
PluralRules::clone() const {
    return new PluralRules(*this);
}

PluralRules&
PluralRules::operator=(const PluralRules& other) {
    if (this != &other) {
        delete mRules;
        if (other.mRules==NULL) {
            mRules = NULL;
        }
        else {
            mRules = new RuleChain(*other.mRules);
        }
        delete mParser;
        mParser = new RuleParser();
    }

    return *this;
}

StringEnumeration* PluralRules::getAvailableLocales(UErrorCode &status) {
    StringEnumeration *result = new PluralAvailableLocalesEnumeration(status);
    if (result == NULL && U_SUCCESS(status)) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    if (U_FAILURE(status)) {
        delete result;
        result = NULL;
    }
    return result;
}


PluralRules* U_EXPORT2
PluralRules::createRules(const UnicodeString& description, UErrorCode& status) {
    RuleChain   rules;

    if (U_FAILURE(status)) {
        return NULL;
    }
    PluralRules *newRules = new PluralRules(status);
    if ( (newRules != NULL)&& U_SUCCESS(status) ) {
        newRules->parseDescription((UnicodeString &)description, rules, status);
        if (U_SUCCESS(status)) {
            newRules->addRules(rules);
        }
    }
    if (U_FAILURE(status)) {
        delete newRules;
        return NULL;
    }
    else {
        return newRules;
    }
}

PluralRules* U_EXPORT2
PluralRules::createDefaultRules(UErrorCode& status) {
    return createRules(UnicodeString(TRUE, PLURAL_DEFAULT_RULE, -1), status);
}

PluralRules* U_EXPORT2
PluralRules::forLocale(const Locale& locale, UErrorCode& status) {
    return forLocale(locale, UPLURAL_TYPE_CARDINAL, status);
}

PluralRules* U_EXPORT2
PluralRules::forLocale(const Locale& locale, UPluralType type, UErrorCode& status) {
    RuleChain   rChain;
    if (U_FAILURE(status)) {
        return NULL;
    }
    if (type >= UPLURAL_TYPE_COUNT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    PluralRules *newObj = new PluralRules(status);
    if (newObj==NULL || U_FAILURE(status)) {
        delete newObj;
        return NULL;
    }
    UnicodeString locRule = newObj->getRuleFromResource(locale, type, status);
    if ((locRule.length() != 0) && U_SUCCESS(status)) {
        newObj->parseDescription(locRule, rChain, status);
        if (U_SUCCESS(status)) {
            newObj->addRules(rChain);
        }
    }
    if (U_FAILURE(status)||(locRule.length() == 0)) {
        // use default plural rule
        status = U_ZERO_ERROR;
        UnicodeString defRule = UnicodeString(PLURAL_DEFAULT_RULE);
        newObj->parseDescription(defRule, rChain, status);
        newObj->addRules(rChain);
    }

    return newObj;
}

UnicodeString
PluralRules::select(int32_t number) const {
    return select(NumberInfo(number));
}

UnicodeString
PluralRules::select(double number) const {
    return select(NumberInfo(number));
}

UnicodeString
PluralRules::select(const NumberInfo &number) const {
    if (mRules == NULL) {
        return UnicodeString(TRUE, PLURAL_DEFAULT_RULE, -1);
    }
    else {
        return mRules->select(number);
    }
}

StringEnumeration*
PluralRules::getKeywords(UErrorCode& status) const {
    if (U_FAILURE(status))  return NULL;
    StringEnumeration* nameEnumerator = new PluralKeywordEnumeration(mRules, status);
    if (U_FAILURE(status)) {
      delete nameEnumerator;
      return NULL;
    }

    return nameEnumerator;
}

double
PluralRules::getUniqueKeywordValue(const UnicodeString& /* keyword */) {
  // Not Implemented.
  return UPLRULES_NO_UNIQUE_VALUE;
}

int32_t
PluralRules::getAllKeywordValues(const UnicodeString & /* keyword */, double * /* dest */,
                                 int32_t /* destCapacity */, UErrorCode& error) {
    error = U_UNSUPPORTED_ERROR;
    return 0;
}

int32_t
PluralRules::getSamples(const UnicodeString & /* keyword */, double * /* dest */,
                        int32_t /* destCapacity */, UErrorCode& status) {
    status = U_UNSUPPORTED_ERROR;
    return 0;
}


UBool
PluralRules::isKeyword(const UnicodeString& keyword) const {
    if (0 == keyword.compare(PLURAL_KEYWORD_OTHER, 5)) {
        return true;
    }
    else {
        if (mRules==NULL) {
            return false;
        }
        else {
            return mRules->isKeyword(keyword);
        }
    }
}

UnicodeString
PluralRules::getKeywordOther() const {
    return UnicodeString(TRUE, PLURAL_KEYWORD_OTHER, 5);
}

UBool
PluralRules::operator==(const PluralRules& other) const  {
    const UnicodeString *ptrKeyword;
    UErrorCode status= U_ZERO_ERROR;

    if ( this == &other ) {
        return TRUE;
    }
    LocalPointer<StringEnumeration> myKeywordList(getKeywords(status));
    LocalPointer<StringEnumeration> otherKeywordList(other.getKeywords(status));
    if (U_FAILURE(status)) {
        return FALSE;
    }

    if (myKeywordList->count(status)!=otherKeywordList->count(status)) {
        return FALSE;
    }
    myKeywordList->reset(status);
    while ((ptrKeyword=myKeywordList->snext(status))!=NULL) {
        if (!other.isKeyword(*ptrKeyword)) {
            return FALSE;
        }
    }
    otherKeywordList->reset(status);
    while ((ptrKeyword=otherKeywordList->snext(status))!=NULL) {
        if (!this->isKeyword(*ptrKeyword)) {
            return FALSE;
        }
    }
    if (U_FAILURE(status)) {
        return FALSE;
    }

    return TRUE;
}

void
PluralRules::parseDescription(UnicodeString& data, RuleChain& rules, UErrorCode &status)
{
    int32_t ruleIndex=0;
    UnicodeString token;
    tokenType type;
    tokenType prevType=none;
    RuleChain *ruleChain=NULL;
    AndConstraint *curAndConstraint=NULL;
    OrConstraint *orNode=NULL;
    RuleChain *lastChain=NULL;
    int32_t  rangeLowIdx = -1;   // Indices in the UVector of ranges of the
    int32_t  rangeHiIdx  = -1;   //    low and hi values currently being parsed.

    if (U_FAILURE(status)) {
        return;
    }
    UnicodeString ruleData = data.toLower("");
    while (ruleIndex< ruleData.length()) {
        mParser->getNextToken(ruleData, &ruleIndex, token, type, status);
        if (U_FAILURE(status)) {
            return;
        }
        mParser->checkSyntax(prevType, type, status);
        if (U_FAILURE(status)) {
            return;
        }
        switch (type) {
        case tAnd:
            U_ASSERT(curAndConstraint != NULL);
            curAndConstraint = curAndConstraint->add();
            break;
        case tOr:
            lastChain = &rules;
            while (lastChain->next !=NULL) {
                lastChain = lastChain->next;
            }
            orNode=lastChain->ruleHeader;
            while (orNode->next != NULL) {
                orNode = orNode->next;
            }
            orNode->next= new OrConstraint();
            orNode=orNode->next;
            orNode->next=NULL;
            curAndConstraint = orNode->add();
            break;
        case tIs:
            U_ASSERT(curAndConstraint != NULL);
            U_ASSERT(curAndConstraint->value == -1);
            U_ASSERT(curAndConstraint->rangeList == NULL);
            break;
        case tNot:
            U_ASSERT(curAndConstraint != NULL);
            curAndConstraint->negated=TRUE;
            break;
        case tIn:
        case tWithin:
            U_ASSERT(curAndConstraint != NULL);
            curAndConstraint->rangeList = new UVector32(status);
            curAndConstraint->rangeList->addElement(-1, status);  // range Low
            curAndConstraint->rangeList->addElement(-1, status);  // range Hi
            rangeLowIdx = 0;
            rangeHiIdx  = 1;
            curAndConstraint->value=PLURAL_RANGE_HIGH;
            curAndConstraint->integerOnly = (type == tIn);
            break;
        case tNumber:
            U_ASSERT(curAndConstraint != NULL);
            if ( (curAndConstraint->op==AndConstraint::MOD)&&
                 (curAndConstraint->opNum == -1 ) ) {
                curAndConstraint->opNum=getNumberValue(token);
            }
            else {
                if (curAndConstraint->rangeList == NULL) {
                    // this is for an 'is' rule
                    curAndConstraint->value = getNumberValue(token);
                } else {
                    // this is for an 'in' or 'within' rule
                    if (curAndConstraint->rangeList->elementAti(rangeLowIdx) == -1) {
                        curAndConstraint->rangeList->setElementAt(getNumberValue(token), rangeLowIdx);
                        curAndConstraint->rangeList->setElementAt(getNumberValue(token), rangeHiIdx);
                    }
                    else {
                        curAndConstraint->rangeList->setElementAt(getNumberValue(token), rangeHiIdx);
                        if (curAndConstraint->rangeList->elementAti(rangeLowIdx) > 
                                curAndConstraint->rangeList->elementAti(rangeHiIdx)) {
                            // Range Lower bound > Range Upper bound.
                            // U_UNEXPECTED_TOKEN seems a little funny, but it is consistently
                            // used for all plural rule parse errors.
                            status = U_UNEXPECTED_TOKEN;
                            break;
                        }
                    }
                }
            }
            break;
        case tComma:
            // TODO: rule syntax checking is inadequate, can happen with badly formed rules.
            //       Catch cases like "n mod 10, is 1" here instead.
            if (curAndConstraint == NULL || curAndConstraint->rangeList == NULL) {
                status = U_UNEXPECTED_TOKEN;
                break;
            }
            U_ASSERT(curAndConstraint->rangeList->size() >= 2);
            rangeLowIdx = curAndConstraint->rangeList->size();
            curAndConstraint->rangeList->addElement(-1, status);  // range Low
            rangeHiIdx = curAndConstraint->rangeList->size();
            curAndConstraint->rangeList->addElement(-1, status);  // range Hi
            break;
        case tMod:
            U_ASSERT(curAndConstraint != NULL);
            curAndConstraint->op=AndConstraint::MOD;
            break;
        case tVariableN:
        case tVariableI:
        case tVariableF:
        case tVariableT:
        case tVariableV:
        case tVariableJ:
            U_ASSERT(curAndConstraint != NULL);
            curAndConstraint->digitsType = type;
            break;
        case tKeyword:
            if (ruleChain==NULL) {
                ruleChain = &rules;
            }
            else {
                while (ruleChain->next!=NULL){
                    ruleChain=ruleChain->next;
                }
                ruleChain=ruleChain->next=new RuleChain();
            }
            if (ruleChain->ruleHeader != NULL) {
                delete ruleChain->ruleHeader;
            }
            orNode = ruleChain->ruleHeader = new OrConstraint();
            curAndConstraint = orNode->add();
            ruleChain->keyword = token;
            break;
        default:
            break;
        }
        prevType=type;
        if (U_FAILURE(status)) {
            break;
        }
    }
}

int32_t
PluralRules::getNumberValue(const UnicodeString& token) const {
    int32_t i;
    char digits[128];

    i = token.extract(0, token.length(), digits, ARRAY_SIZE(digits), US_INV);
    digits[i]='\0';

    return((int32_t)atoi(digits));
}


void
PluralRules::getNextLocale(const UnicodeString& localeData, int32_t* curIndex, UnicodeString& localeName) {
    int32_t i=*curIndex;

    localeName.remove();
    while (i< localeData.length()) {
       if ( (localeData.charAt(i)!= SPACE) && (localeData.charAt(i)!= COMMA) ) {
           break;
       }
       i++;
    }

    while (i< localeData.length()) {
       if ( (localeData.charAt(i)== SPACE) || (localeData.charAt(i)== COMMA) ) {
           break;
       }
       localeName+=localeData.charAt(i++);
    }
    *curIndex=i;
}


int32_t
PluralRules::getKeywordIndex(const UnicodeString& keyword,
                             UErrorCode& status) const {
    if (U_SUCCESS(status)) {
        int32_t n = 0;
        RuleChain* rc = mRules;
        while (rc != NULL) {
            if (rc->ruleHeader != NULL) {
                if (rc->keyword == keyword) {
                    return n;
                }
                ++n;
            }
            rc = rc->next;
        }
        if (0 == keyword.compare(PLURAL_KEYWORD_OTHER, 5)) {
            return n;
        }
    }
    return -1;
}


void
PluralRules::addRules(RuleChain& rules) {
    RuleChain *newRule = new RuleChain(rules);
    U_ASSERT(this->mRules == NULL);
    this->mRules=newRule;
}

UnicodeString
PluralRules::getRuleFromResource(const Locale& locale, UPluralType type, UErrorCode& errCode) {
    UnicodeString emptyStr;

    if (U_FAILURE(errCode)) {
        return emptyStr;
    }
    LocalUResourceBundlePointer rb(ures_openDirect(NULL, "plurals", &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    const char *typeKey;
    switch (type) {
    case UPLURAL_TYPE_CARDINAL:
        typeKey = "locales";
        break;
    case UPLURAL_TYPE_ORDINAL:
        typeKey = "locales_ordinals";
        break;
    default:
        // Must not occur: The caller should have checked for valid types.
        errCode = U_ILLEGAL_ARGUMENT_ERROR;
        return emptyStr;
    }
    LocalUResourceBundlePointer locRes(ures_getByKey(rb.getAlias(), typeKey, NULL, &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    int32_t resLen=0;
    const char *curLocaleName=locale.getName();
    const UChar* s = ures_getStringByKey(locRes.getAlias(), curLocaleName, &resLen, &errCode);

    if (s == NULL) {
        // Check parent locales.
        UErrorCode status = U_ZERO_ERROR;
        char parentLocaleName[ULOC_FULLNAME_CAPACITY];
        const char *curLocaleName=locale.getName();
        uprv_strcpy(parentLocaleName, curLocaleName);

        while (uloc_getParent(parentLocaleName, parentLocaleName,
                                       ULOC_FULLNAME_CAPACITY, &status) > 0) {
            resLen=0;
            s = ures_getStringByKey(locRes.getAlias(), parentLocaleName, &resLen, &status);
            if (s != NULL) {
                errCode = U_ZERO_ERROR;
                break;
            }
            status = U_ZERO_ERROR;
        }
    }
    if (s==NULL) {
        return emptyStr;
    }

    char setKey[256];
    UChar result[256];
    u_UCharsToChars(s, setKey, resLen + 1);
    // printf("\n PluralRule: %s\n", setKey);


    LocalUResourceBundlePointer ruleRes(ures_getByKey(rb.getAlias(), "rules", NULL, &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    resLen=0;
    LocalUResourceBundlePointer setRes(ures_getByKey(ruleRes.getAlias(), setKey, NULL, &errCode));
    if (U_FAILURE(errCode)) {
        return emptyStr;
    }

    int32_t numberKeys = ures_getSize(setRes.getAlias());
    char *key=NULL;
    int32_t len=0;
    for(int32_t i=0; i<numberKeys; ++i) {
        int32_t keyLen;
        resLen=0;
        s=ures_getNextString(setRes.getAlias(), &resLen, (const char**)&key, &errCode);
        keyLen = (int32_t)uprv_strlen(key);
        u_charsToUChars(key, result+len, keyLen);
        len += keyLen;
        result[len++]=COLON;
        uprv_memcpy(result+len, s, resLen*sizeof(UChar));
        len += resLen;
        result[len++]=SEMI_COLON;
    }
    result[len++]=0;
    u_UCharsToChars(result, setKey, len);
    // printf(" Rule: %s\n", setKey);

    return UnicodeString(result);
}

AndConstraint::AndConstraint() {
    op = AndConstraint::NONE;
    opNum=-1;
    value = -1;
    rangeList = NULL;
    negated = FALSE;
    integerOnly = FALSE;
    digitsType = none;
    next=NULL;
}


AndConstraint::AndConstraint(const AndConstraint& other) {
    this->op = other.op;
    this->opNum=other.opNum;
    this->value=other.value;
    this->rangeList=NULL;
    if (other.rangeList != NULL) {
        UErrorCode status = U_ZERO_ERROR;
        this->rangeList = new UVector32(status);
        this->rangeList->assign(*other.rangeList, status);
    }
    this->integerOnly=other.integerOnly;
    this->negated=other.negated;
    this->digitsType = other.digitsType;
    if (other.next==NULL) {
        this->next=NULL;
    }
    else {
        this->next = new AndConstraint(*other.next);
    }
}

AndConstraint::~AndConstraint() {
    delete rangeList;
    if (next!=NULL) {
        delete next;
    }
}


UBool
AndConstraint::isFulfilled(const NumberInfo &number) {
    UBool result = TRUE;
    double n = number.get(digitsType);  // pulls n | i | v | f value for the number.
                                        // Will always be positive.
                                        // May be non-integer (n option only)
    do {
        if ((integerOnly && n != uprv_floor(n)) ||
                (digitsType == tVariableJ && number.getVisibleFractionDigitCount()) != 0) {
            result = FALSE;
            break;
        }

        if (op == MOD) {
            n = fmod(n, opNum);
        }
        if (rangeList == NULL) {
            result = value == -1 ||    // empty rule
                     n == value;       //  'is' rule
            break;
        }
        result = FALSE;                // 'in' or 'within' rule
        for (int32_t r=0; r<rangeList->size(); r+=2) {
            if (rangeList->elementAti(r) <= n && n <= rangeList->elementAti(r+1)) {
                result = TRUE;
                break;
            }
        }
    } while (FALSE);

    if (negated) {
        result = !result;
    }
    return result;
}

UBool 
AndConstraint::isLimited() {
    return (rangeList == NULL || integerOnly) && !negated && op != MOD;
}

AndConstraint*
AndConstraint::add()
{
    this->next = new AndConstraint();
    return this->next;
}

OrConstraint::OrConstraint() {
    childNode=NULL;
    next=NULL;
}

OrConstraint::OrConstraint(const OrConstraint& other) {
    if ( other.childNode == NULL ) {
        this->childNode = NULL;
    }
    else {
        this->childNode = new AndConstraint(*(other.childNode));
    }
    if (other.next == NULL ) {
        this->next = NULL;
    }
    else {
        this->next = new OrConstraint(*(other.next));
    }
}

OrConstraint::~OrConstraint() {
    if (childNode!=NULL) {
        delete childNode;
    }
    if (next!=NULL) {
        delete next;
    }
}

AndConstraint*
OrConstraint::add()
{
    OrConstraint *curOrConstraint=this;
    {
        while (curOrConstraint->next!=NULL) {
            curOrConstraint = curOrConstraint->next;
        }
        U_ASSERT(curOrConstraint->childNode == NULL);
        curOrConstraint->childNode = new AndConstraint();
    }
    return curOrConstraint->childNode;
}

UBool
OrConstraint::isFulfilled(const NumberInfo &number) {
    OrConstraint* orRule=this;
    UBool result=FALSE;

    while (orRule!=NULL && !result) {
        result=TRUE;
        AndConstraint* andRule = orRule->childNode;
        while (andRule!=NULL && result) {
            result = andRule->isFulfilled(number);
            andRule=andRule->next;
        }
        orRule = orRule->next;
    }

    return result;
}

UBool
OrConstraint::isLimited() {
    for (OrConstraint *orc = this; orc != NULL; orc = orc->next) {
        UBool result = FALSE;
        for (AndConstraint *andc = orc->childNode; andc != NULL; andc = andc->next) {
            if (andc->isLimited()) {
                result = TRUE;
                break;
            }
        }
        if (result == FALSE) {
            return FALSE;
        }
    }
    return TRUE;
}

RuleChain::RuleChain() {
    ruleHeader=NULL;
    next = NULL;
}

RuleChain::RuleChain(const RuleChain& other) {
    this->keyword=other.keyword;
    if (other.ruleHeader != NULL) {
        this->ruleHeader = new OrConstraint(*(other.ruleHeader));
    }
    else {
        this->ruleHeader = NULL;
    }
    if (other.next != NULL ) {
        this->next = new RuleChain(*other.next);
    }
    else
    {
        this->next = NULL;
    }
}

RuleChain::~RuleChain() {
    if (next != NULL) {
        delete next;
    }
    if ( ruleHeader != NULL ) {
        delete ruleHeader;
    }
}


UnicodeString
RuleChain::select(const NumberInfo &number) const {
    for (const RuleChain *rules = this; rules != NULL; rules = rules->next) {
       if (rules->ruleHeader->isFulfilled(number)) {
           return rules->keyword;
       }
    }
    return UnicodeString(TRUE, PLURAL_KEYWORD_OTHER, 5);
}

void
RuleChain::dumpRules(UnicodeString& result) {
    UChar digitString[16];

    if ( ruleHeader != NULL ) {
        result +=  keyword;
        OrConstraint* orRule=ruleHeader;
        while ( orRule != NULL ) {
            AndConstraint* andRule=orRule->childNode;
            while ( andRule != NULL ) {
                if ( (andRule->op==AndConstraint::NONE) && (andRule->rangeList==NULL) ) {
                    result += UNICODE_STRING_SIMPLE(" n is ");
                    if (andRule->negated) {
                        result += UNICODE_STRING_SIMPLE("not ");
                    }
                    uprv_itou(digitString,16, andRule->value,10,0);
                    result += UnicodeString(digitString);
                }
                else {
                    if (andRule->op==AndConstraint::MOD) {
                        result += UNICODE_STRING_SIMPLE("  n mod ");
                        uprv_itou(digitString,16, andRule->opNum,10,0);
                        result += UnicodeString(digitString);
                    }
                    else {
                        result += UNICODE_STRING_SIMPLE("  n ");
                    }
                    if (andRule->rangeList==NULL) {
                        if (andRule->negated) {
                            result += UNICODE_STRING_SIMPLE(" is not ");
                            uprv_itou(digitString,16, andRule->value,10,0);
                            result += UnicodeString(digitString);
                        }
                        else {
                            result += UNICODE_STRING_SIMPLE(" is ");
                            uprv_itou(digitString,16, andRule->value,10,0);
                            result += UnicodeString(digitString);
                        }
                    }
                    else {
                        if (andRule->negated) {
                            if ( andRule->integerOnly ) {
                                result += UNICODE_STRING_SIMPLE("  not in ");
                            }
                            else {
                                result += UNICODE_STRING_SIMPLE("  not within ");
                            }
                        }
                        else {
                            if ( andRule->integerOnly ) {
                                result += UNICODE_STRING_SIMPLE(" in ");
                            }
                            else {
                                result += UNICODE_STRING_SIMPLE(" within ");
                            }
                        }
                        for (int32_t r=0; r<andRule->rangeList->size(); r+=2) {
                            int32_t rangeLo = andRule->rangeList->elementAti(r);
                            int32_t rangeHi = andRule->rangeList->elementAti(r+1);
                            uprv_itou(digitString,16, rangeLo, 10, 0);
                            result += UnicodeString(digitString);
                            if (rangeLo != rangeHi) {
                                result += UNICODE_STRING_SIMPLE(" .. ");
                                uprv_itou(digitString,16, rangeHi, 10,0);
                            }
                            if (r+2 <= andRule->rangeList->size()) {
                                result += UNICODE_STRING_SIMPLE(", ");
                            }
                        }
                    }
                }
                if ( (andRule=andRule->next) != NULL) {
                    result.append(PK_AND, 3);
                }
            }
            if ( (orRule = orRule->next) != NULL ) {
                result.append(PK_OR, 2);
            }
        }
    }
    if ( next != NULL ) {
        next->dumpRules(result);
    }
}


UErrorCode
RuleChain::getKeywords(int32_t capacityOfKeywords, UnicodeString* keywords, int32_t& arraySize) const {
    if ( arraySize < capacityOfKeywords-1 ) {
        keywords[arraySize++]=keyword;
    }
    else {
        return U_BUFFER_OVERFLOW_ERROR;
    }

    if ( next != NULL ) {
        return next->getKeywords(capacityOfKeywords, keywords, arraySize);
    }
    else {
        return U_ZERO_ERROR;
    }
}

UBool
RuleChain::isKeyword(const UnicodeString& keywordParam) const {
    if ( keyword == keywordParam ) {
        return TRUE;
    }

    if ( next != NULL ) {
        return next->isKeyword(keywordParam);
    }
    else {
        return FALSE;
    }
}


RuleParser::RuleParser() {
}

RuleParser::~RuleParser() {
}

void
RuleParser::checkSyntax(tokenType prevType, tokenType curType, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }
    switch(prevType) {
    case none:
    case tSemiColon:
        if (curType!=tKeyword && curType != tEOF) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tVariableN:
    case tVariableI:
    case tVariableF:
    case tVariableT:
    case tVariableV:
    case tVariableJ:
        if (curType != tIs && curType != tMod && curType != tIn &&
            curType != tNot && curType != tWithin) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tKeyword:
        if (curType != tColon) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tColon:
        if (!(curType == tVariableN ||
              curType == tVariableI ||
              curType == tVariableF ||
              curType == tVariableT ||
              curType == tVariableV ||
              curType == tVariableJ)) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tIs:
        if ( curType != tNumber && curType != tNot) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tNot:
        if (curType != tNumber && curType != tIn && curType != tWithin) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tMod:
    case tDot:
    case tIn:
    case tWithin:
        if (curType != tNumber) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tAnd:
    case tOr:
        if ( curType != tVariableN &&
             curType != tVariableI &&
             curType != tVariableF &&
             curType != tVariableT &&
             curType != tVariableV &&
             curType != tVariableJ) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tComma:
        if (curType != tNumber) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tNumber:
        if (curType != tDot && curType != tSemiColon && curType != tIs && curType != tNot &&
            curType != tIn && curType != tWithin && curType != tAnd && curType != tOr && 
            curType != tComma && curType != tEOF)
        {
            status = U_UNEXPECTED_TOKEN;
        }
        // TODO: a comma following a number that is not part of a range will be allowed.
        //       It's not the only case of this sort of thing. Parser needs a re-write.
        break;
    default:
        status = U_UNEXPECTED_TOKEN;
        break;
    }
}

void
RuleParser::getNextToken(const UnicodeString& ruleData,
                         int32_t *ruleIndex,
                         UnicodeString& token,
                         tokenType& type,
                         UErrorCode &status)
{
    int32_t curIndex= *ruleIndex;
    UChar ch;
    tokenType prevType=none;

    if (U_FAILURE(status)) {
        return;
    }
    while (curIndex<ruleData.length()) {
        ch = ruleData.charAt(curIndex);
        if ( !inRange(ch, type) ) {
            status = U_ILLEGAL_CHARACTER;
            return;
        }
        switch (type) {
        case tSpace:
            if ( *ruleIndex != curIndex ) { // letter
                token=UnicodeString(ruleData, *ruleIndex, curIndex-*ruleIndex);
                *ruleIndex=curIndex;
                type=prevType;
                getKeyType(token, type, status);
                return;
            }
            else {
                *ruleIndex=*ruleIndex+1;
                if (*ruleIndex >= ruleData.length()) {
                    type = tEOF;
                }
            }
            break; // consective space
        case tColon:
        case tSemiColon:
        case tComma:
        case tIn:   // scanned '='
        case tNot:  // scanned '!'
        case tMod:  // scanned '%'
            if ( *ruleIndex != curIndex ) {
                token=UnicodeString(ruleData, *ruleIndex, curIndex-*ruleIndex);
                *ruleIndex=curIndex;
                type=prevType;
                getKeyType(token, type, status);
                return;
            }
            else {
                *ruleIndex=curIndex+1;
                return;
            }
        case tLetter:
             if ((type==prevType)||(prevType==none)) {
                prevType=type;
                break;
             }
             break;
        case tNumber:
             if ((type==prevType)||(prevType==none)) {
                prevType=type;
                break;
             }
             else {
                *ruleIndex=curIndex+1;
                return;
             }
         case tDot:
             if (prevType==none) {         // first dot
                prevType=type;
                break;
             }
             else if (prevType == tDot) {  // two consecutive dots. Return them
                *ruleIndex=curIndex+1;     //   without looking to see what follows.
                return;
             } else {
                // Encountered '.' while parsing something else
                // Return the something else.
                U_ASSERT( *ruleIndex != curIndex );
                token=UnicodeString(ruleData, *ruleIndex, curIndex-*ruleIndex);
                *ruleIndex=curIndex;
                type=prevType;
                getKeyType(token, type, status);
                return;
             }
         default:
             status = U_UNEXPECTED_TOKEN;
             return;
        }
        curIndex++;
    }
    if ( curIndex>=ruleData.length() ) {
        if ( (type == tLetter)||(type == tNumber) ) {
            token=UnicodeString(ruleData, *ruleIndex, curIndex-*ruleIndex);
            getKeyType(token, type, status);
            if (U_FAILURE(status)) {
                return;
            }
        }
        *ruleIndex = ruleData.length();
    }
}

UBool
RuleParser::inRange(UChar ch, tokenType& type) {
    if ((ch>=CAP_A) && (ch<=CAP_Z)) {
        // we assume all characters are in lower case already.
        return FALSE;
    }
    if ((ch>=LOW_A) && (ch<=LOW_Z)) {
        type = tLetter;
        return TRUE;
    }
    if ((ch>=U_ZERO) && (ch<=U_NINE)) {
        type = tNumber;
        return TRUE;
    }
    switch (ch) {
    case COLON:
        type = tColon;
        return TRUE;
    case SPACE:
        type = tSpace;
        return TRUE;
    case SEMI_COLON:
        type = tSemiColon;
        return TRUE;
    case DOT:
        type = tDot;
        return TRUE;
    case COMMA:
        type = tComma;
        return TRUE;
    case EXCLAMATION:
        type = tNot;
        return TRUE;
    case EQUALS:
        type = tIn;
        return TRUE;
    case PERCENT_SIGN:
        type = tMod;
        return TRUE;
    default :
        type = none;
        return FALSE;
    }
}


void
RuleParser::getKeyType(const UnicodeString& token, tokenType& keyType, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }
    if ( keyType==tNumber) {
    }
    else if (0 == token.compare(PK_VAR_N, 1)) {
        keyType = tVariableN;
    }
    else if (0 == token.compare(PK_VAR_I, 1)) {
        keyType = tVariableI;
    }
    else if (0 == token.compare(PK_VAR_F, 1)) {
        keyType = tVariableF;
    }
    else if (0 == token.compare(PK_VAR_T, 1)) {
        keyType = tVariableT;
    }
    else if (0 == token.compare(PK_VAR_V, 1)) {
        keyType = tVariableV;
    }
    else if (0 == token.compare(PK_VAR_J, 1)) {
        keyType = tVariableJ;
    }
    else if (0 == token.compare(PK_IS, 2)) {
        keyType = tIs;
    }
    else if (0 == token.compare(PK_AND, 3)) {
        keyType = tAnd;
    }
    else if (0 == token.compare(PK_IN, 2)) {
        keyType = tIn;
    }
    else if (0 == token.compare(PK_WITHIN, 6)) {
        keyType = tWithin;
    }
    else if (0 == token.compare(PK_NOT, 3)) {
        keyType = tNot;
    }
    else if (0 == token.compare(PK_MOD, 3)) {
        keyType = tMod;
    }
    else if (0 == token.compare(PK_OR, 2)) {
        keyType = tOr;
    }
    else if ( isValidKeyword(token) ) {
        keyType = tKeyword;
    }
    else {
        status = U_UNEXPECTED_TOKEN;
    }
}

UBool
RuleParser::isValidKeyword(const UnicodeString& token) {
    return PatternProps::isIdentifier(token.getBuffer(), token.length());
}

PluralKeywordEnumeration::PluralKeywordEnumeration(RuleChain *header, UErrorCode& status)
        : pos(0), fKeywordNames(status) {
    if (U_FAILURE(status)) {
        return;
    }
    fKeywordNames.setDeleter(uprv_deleteUObject);
    UBool  addKeywordOther=TRUE;
    RuleChain *node=header;
    while(node!=NULL) {
        fKeywordNames.addElement(new UnicodeString(node->keyword), status);
        if (U_FAILURE(status)) {
            return;
        }
        if (0 == node->keyword.compare(PLURAL_KEYWORD_OTHER, 5)) {
            addKeywordOther= FALSE;
        }
        node=node->next;
    }

    if (addKeywordOther) {
        fKeywordNames.addElement(new UnicodeString(PLURAL_KEYWORD_OTHER), status);
    }
}

const UnicodeString*
PluralKeywordEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && pos < fKeywordNames.size()) {
        return (const UnicodeString*)fKeywordNames.elementAt(pos++);
    }
    return NULL;
}

void
PluralKeywordEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
PluralKeywordEnumeration::count(UErrorCode& /*status*/) const {
       return fKeywordNames.size();
}

PluralKeywordEnumeration::~PluralKeywordEnumeration() {
}



NumberInfo::NumberInfo(double n, int32_t v, int64_t f) {
    init(n, v, f);
    // check values. TODO make into unit test.
    //            
    //            long visiblePower = (int) Math.pow(10, v);
    //            if (fractionalDigits > visiblePower) {
    //                throw new IllegalArgumentException();
    //            }
    //            double fraction = intValue + (fractionalDigits / (double) visiblePower);
    //            if (fraction != source) {
    //                double diff = Math.abs(fraction - source)/(Math.abs(fraction) + Math.abs(source));
    //                if (diff > 0.00000001d) {
    //                    throw new IllegalArgumentException();
    //                }
    //            }
}

NumberInfo::NumberInfo(double n, int32_t v) {
    // Ugly, but for samples we don't care.
    init(n, v, getFractionalDigits(n, v));
}

NumberInfo::NumberInfo(double n) {
    int64_t numFractionDigits = decimals(n);
    init(n, numFractionDigits, getFractionalDigits(n, numFractionDigits));
}

void NumberInfo::init(double n, int32_t v, int64_t f) {
    isNegative = n < 0;
    source = fabs(n);
    visibleFractionDigitCount = v;
    fractionalDigits = f;
    intValue = (int64_t)source;
    hasIntegerValue = (source == intValue);
    if (f == 0) {
         fractionalDigitsWithoutTrailingZeros = 0;
    } else {
        int64_t fdwtz = f;
        while ((fdwtz%10) == 0) {
            fdwtz /= 10;
        }
        fractionalDigitsWithoutTrailingZeros = fdwtz;
    }
}

int32_t NumberInfo::decimals(double n) {
    // Count the number of decimal digits in the fraction part of the number, excluding trailing zeros.
    n = fabs(n);
    double scaledN = n;
    for (int ndigits=0; ndigits<=3; ndigits++) {
        // fastpath the common cases, integers or fractions with 3 or fewer digits
        if (scaledN == floor(scaledN)) {
            return ndigits;
        }
        scaledN *= 10;
    }
    char  buf[30] = {0};
    sprintf(buf, "%1.15e", n);
    // formatted number looks like this: 1.234567890123457e-01
    int exponent = atoi(buf+18);
    int numFractionDigits = 15;
    for (int i=16; ; --i) {
        if (buf[i] != '0') {
            break;
        }
        --numFractionDigits; 
    }
    numFractionDigits -= exponent;   // Fraction part of fixed point representation.
    return numFractionDigits;
}


// Get the fraction digits of a double, represented as an integer.
//    v is the number of visible fraction digits in the displayed form of the number.
//       Example: n = 1001.234, v = 6, result = 234000
//    TODO: need to think through how this is used in the plural rule context.
//          This function can easily encounter integer overflow, 
//          and can easily return noise digits when the precision of a double is exceeded.

int64_t NumberInfo::getFractionalDigits(double n, int32_t v) {
    if (v == 0 || n == floor(n)) {
        return 0;
    }
    n = fabs(n);
    double fract = n - floor(n);
    switch (v) {
      case 1: return (int64_t)(fract*10.0 + 0.5);
      case 2: return (int64_t)(fract*100.0 + 0.5);
      case 3: return (int64_t)(fract*1000.0 + 0.5);
      default:
          double scaled = floor(fract * pow(10, v) + 0.5);
          if (scaled > INT64_MAX) {
              return INT64_MAX;
          } else {
              return (int64_t)scaled;
          }
      }
}


double NumberInfo::get(tokenType operand) const {
    switch(operand) {
        default:         return source;
        case tVariableI: return (double)intValue;
        case tVariableF: return (double)fractionalDigits;
        case tVariableT: return (double)fractionalDigitsWithoutTrailingZeros; 
        case tVariableV: return visibleFractionDigitCount;
    }
}

int32_t NumberInfo::getVisibleFractionDigitCount() const {
    return visibleFractionDigitCount;
}



PluralAvailableLocalesEnumeration::PluralAvailableLocalesEnumeration(UErrorCode &status) {
    fLocales = NULL;
    fRes = NULL;
    fOpenStatus = status;
    if (U_FAILURE(status)) {
        return;
    }
    fOpenStatus = U_ZERO_ERROR;
    LocalUResourceBundlePointer rb(ures_openDirect(NULL, "plurals", &fOpenStatus));
    fLocales = ures_getByKey(rb.getAlias(), "locales", NULL, &fOpenStatus);
}

PluralAvailableLocalesEnumeration::~PluralAvailableLocalesEnumeration() {
    ures_close(fLocales);
    ures_close(fRes);
    fLocales = NULL;
    fRes = NULL;
}

const char *PluralAvailableLocalesEnumeration::next(int32_t *resultLength, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return NULL;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return NULL;
    }
    fRes = ures_getNextResource(fLocales, fRes, &status);
    if (fRes == NULL || U_FAILURE(status)) {
        if (status == U_INDEX_OUTOFBOUNDS_ERROR) {
            status = U_ZERO_ERROR;
        }
        return NULL;
    }
    const char *result = ures_getKey(fRes);
    if (resultLength != NULL) {
        *resultLength = uprv_strlen(result);
    }
    return result;
}


void PluralAvailableLocalesEnumeration::reset(UErrorCode &status) {
    if (U_FAILURE(status)) {
       return;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return;
    }
    ures_resetIterator(fLocales);
}

int32_t PluralAvailableLocalesEnumeration::count(UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return 0;
    }
    return ures_getSize(fLocales);
}

U_NAMESPACE_END


#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
