/*
*******************************************************************************
* Copyright (C) 2007, International Business Machines Corporation and         *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/rbtz.h"
#include "unicode/tzrule.h"
#include "unicode/tztrans.h"
#include "unicode/gregocal.h"
#include "uvector.h"
#include "gregoimp.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

/**
 * A struct representing a time zone transition
 */
struct Transition {
    UDate time;
    TimeZoneRule* from;
    TimeZoneRule* to;
};

static UBool compareRules(UVector* rules1, UVector* rules2) {
    if (rules1 == NULL && rules2 == NULL) {
        return TRUE;
    } else if (rules1 == NULL || rules2 == NULL) {
        return FALSE;
    }
    int32_t size = rules1->size();
    if (size != rules2->size()) {
        return FALSE;
    }
    for (int32_t i = 0; i < size; i++) {
        TimeZoneRule *r1 = (TimeZoneRule*)rules1->elementAt(i);
        TimeZoneRule *r2 = (TimeZoneRule*)rules2->elementAt(i);
        if (r1 != r2) {
            return FALSE;
        }
    }
    return TRUE;
}

static UDate getTransitionTime(Transition* transition, UBool local) {
    UDate time = transition->time;
    if (local) {
        time += transition->from->getRawOffset() + transition->from->getDSTSavings();
    }
    return time;
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(RuleBasedTimeZone)

RuleBasedTimeZone::RuleBasedTimeZone(const UnicodeString& id, InitialTimeZoneRule* initialRule)
: BasicTimeZone(id), fInitialRule(initialRule), fHistoricRules(NULL), fFinalRules(NULL),
  fHistoricTransitions(NULL), fUpToDate(FALSE) {
}

RuleBasedTimeZone::RuleBasedTimeZone(const RuleBasedTimeZone& source)
: BasicTimeZone(source), fInitialRule(source.fInitialRule->clone()),
  fHistoricRules(NULL), fFinalRules(NULL), fHistoricTransitions(NULL), fUpToDate(FALSE) {
}

RuleBasedTimeZone::~RuleBasedTimeZone() {
    deleteTransitions();
    deleteRules();
}

RuleBasedTimeZone&
RuleBasedTimeZone::operator=(const RuleBasedTimeZone& right) {
    if (*this != right) {
        BasicTimeZone::operator=(right);
        deleteRules();
        fInitialRule = right.fInitialRule->clone();
        fHistoricRules = copyRules(right.fHistoricRules);
        fFinalRules = copyRules(right.fFinalRules);
        deleteTransitions();
        fUpToDate = FALSE;
    }
    return *this;
}

UBool
RuleBasedTimeZone::operator==(const RuleBasedTimeZone& that) const {
    if (this == &that) {
        return TRUE;
    }
    if (getDynamicClassID() != that.getDynamicClassID()
        || BasicTimeZone::operator==(that) == FALSE) {
        return FALSE;
    }
    if (*fInitialRule != *(that.fInitialRule)) {
        return FALSE;
    }
    if (compareRules(fHistoricRules, that.fHistoricRules)
        && compareRules(fFinalRules, that.fFinalRules)) {
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::operator!=(const RuleBasedTimeZone& that) const {
    return !operator==(that);
}

void
RuleBasedTimeZone::addTransitionRule(TimeZoneRule* rule, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (rule->getDynamicClassID() == AnnualTimeZoneRule::getStaticClassID()
        && ((AnnualTimeZoneRule*)rule)->getEndYear() == AnnualTimeZoneRule::MAX_YEAR) {
        // A final rule
        if (fFinalRules == NULL) {
            fFinalRules = new UVector(status);
            if (U_FAILURE(status)) {
                return;
            }
        } else if (fFinalRules->size() >= 2) {
            // Cannot handle more than two final rules
            status = U_INVALID_STATE_ERROR;
            return;
        }
        fFinalRules->addElement((void*)rule, status);
    } else {
        // Non-final rule
        if (fHistoricRules == NULL) {
            fHistoricRules = new UVector(status);
            if (U_FAILURE(status)) {
                return;
            }
        }
        fHistoricRules->addElement((void*)rule, status);
    }
    // Mark dirty, so transitions are recalculated at next complete() call
    fUpToDate = FALSE;
}

void
RuleBasedTimeZone::complete(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (fUpToDate) {
        return;
    }
    // Make sure either no final rules or a pair of AnnualTimeZoneRules
    // are available.
    if (fFinalRules != NULL && fFinalRules->size() != 2) {
        status = U_INVALID_STATE_ERROR;
        return;
    }

    UBool *done = NULL;
    // Create a TimezoneTransition and add to the list
    if (fHistoricRules != NULL || fFinalRules != NULL) {
        TimeZoneRule *curRule = fInitialRule;
        UDate lastTransitionTime = MIN_MILLIS;

        // Build the transition array which represents historical time zone
        // transitions.
        if (fHistoricRules != NULL && fHistoricRules->size() > 0) {
            int32_t i;
            int32_t historicCount = fHistoricRules->size();
            done = (UBool*)uprv_malloc(sizeof(UBool) * historicCount);
            if (done == NULL) {
                status = U_MEMORY_ALLOCATION_ERROR;
                goto cleanup;
            }
            for (i = 0; i < historicCount; i++) {
                done[i] = FALSE;
            }
            while (true) {
                int32_t curStdOffset = curRule->getRawOffset();
                int32_t curDstSavings = curRule->getDSTSavings();
                UDate nextTransitionTime = MAX_MILLIS;
                TimeZoneRule *nextRule = NULL;
                TimeZoneRule *r = NULL;
                UBool avail;
                UDate tt;
                UnicodeString curName, name;
                curRule->getName(curName);

                for (i = 0; i < historicCount; i++) {
                    if (done[i]) {
                        continue;
                    }
                    r = (TimeZoneRule*)fHistoricRules->elementAt(i);
                    avail = r->getNextStart(lastTransitionTime, curStdOffset, curDstSavings, false, tt);
                    if (!avail) {
                        // No more transitions from this rule - skip this rule next time
                        done[i] = TRUE;
                    } else {
                        r->getName(name);
                        if (*r == *curRule ||
                            (name == curName && r->getRawOffset() == curRule->getRawOffset()
                            && r->getDSTSavings() == curRule->getDSTSavings())) {
                            continue;
                        }
                        if (tt < nextTransitionTime) {
                            nextTransitionTime = tt;
                            nextRule = r;
                        }
                    }
                }

                if (nextRule ==  NULL) {
                    // Check if all historic rules are done
                    UBool bDoneAll = TRUE;
                    for (int32_t j = 0; j < historicCount; j++) {
                        if (!done[j]) {
                            bDoneAll = FALSE;
                            break;
                        }
                    }
                    if (bDoneAll) {
                        break;
                    }
                }

                if (fFinalRules != NULL) {
                    // Check if one of final rules has earlier transition date
                    for (i = 0; i < 2 /* fFinalRules->size() */; i++) {
                        TimeZoneRule *fr = (TimeZoneRule*)fFinalRules->elementAt(i);
                        if (*fr == *curRule) {
                            continue;
                        }
                        r = (TimeZoneRule*)fFinalRules->elementAt(i);
                        avail = r->getNextStart(lastTransitionTime, curStdOffset, curDstSavings, false, tt);
                        if (avail) {
                            if (tt < nextTransitionTime) {
                                nextTransitionTime = tt;
                                nextRule = r;
                            }
                        }
                    }
                }

                if (nextRule == NULL) {
                    // Nothing more
                    break;
                }

                if (fHistoricTransitions == NULL) {
                    fHistoricTransitions = new UVector(status);
                    if (U_FAILURE(status)) {
                        goto cleanup;
                    }
                }
                Transition *trst = (Transition*)uprv_malloc(sizeof(Transition));
                if (trst == NULL) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    goto cleanup;
                }
                trst->time = nextTransitionTime;
                trst->from = curRule;
                trst->to = nextRule;
                fHistoricTransitions->addElement(trst, status);
                if (U_FAILURE(status)) {
                    goto cleanup;
                }
                lastTransitionTime = nextTransitionTime;
                curRule = nextRule;
            }
        }
        if (fFinalRules != NULL) {
            if (fHistoricTransitions == NULL) {
                fHistoricTransitions = new UVector(status);
                if (U_FAILURE(status)) {
                    goto cleanup;
                }
            }
            // Append the first transition for each
            TimeZoneRule *rule0 = (TimeZoneRule*)fFinalRules->elementAt(0);
            TimeZoneRule *rule1 = (TimeZoneRule*)fFinalRules->elementAt(1);
            UDate tt0, tt1;
            UBool avail0 = rule0->getNextStart(lastTransitionTime, curRule->getRawOffset(), curRule->getDSTSavings(), false, tt0);
            UBool avail1 = rule1->getNextStart(lastTransitionTime, curRule->getRawOffset(), curRule->getDSTSavings(), false, tt1);
            if (!avail0 || !avail1) {
                // Should not happen, because both rules are permanent
                status = U_INVALID_STATE_ERROR;
                goto cleanup;
            }
            Transition *final0 = (Transition*)uprv_malloc(sizeof(Transition));
            if (final0 == NULL) {
                status = U_MEMORY_ALLOCATION_ERROR;
                goto cleanup;
            }
            Transition *final1 = (Transition*)uprv_malloc(sizeof(Transition));
            if (final1 == NULL) {
                status = U_MEMORY_ALLOCATION_ERROR;
                goto cleanup;
            }
            if (tt0 < tt1) {
                final0->time = tt0;
                final0->from = curRule;
                final0->to = rule0;
                rule1->getNextStart(tt0, rule0->getRawOffset(), rule0->getDSTSavings(), false, final1->time);
                final1->from = rule0;
                final1->to = rule1;
            } else {
                final0->time = tt1;
                final0->from = curRule;
                final0->to = rule1;
                rule0->getNextStart(tt1, rule1->getRawOffset(), rule1->getDSTSavings(), false, final1->time);
                final1->from = rule1;
                final1->to = rule0;
            }
            fHistoricTransitions->addElement(final0, status);
            if (U_FAILURE(status)) {
                goto cleanup;
            }
            fHistoricTransitions->addElement(final1, status);
            if (U_FAILURE(status)) {
                goto cleanup;
            }
        }
    }
    fUpToDate = TRUE;
    return;

cleanup:
    deleteTransitions();
    if (done != NULL) {
        uprv_free(done);
    }
    fUpToDate = FALSE;
}

TimeZone*
RuleBasedTimeZone::clone(void) const {
    return new RuleBasedTimeZone(*this);
}

int32_t
RuleBasedTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                             uint8_t dayOfWeek, int32_t millis, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (month < UCAL_JANUARY || month > UCAL_DECEMBER) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    } else {
        return getOffset(era, year, month, day, dayOfWeek, millis,
                         Grego::monthLength(year, month), status);
    }
}

int32_t
RuleBasedTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                             uint8_t dayOfWeek, int32_t millis,
                             int32_t monthLength, UErrorCode& status) const {
    // dayOfWeek and monthLength are unused
    if (U_FAILURE(status)) {
        return 0;
    }
    if (era == GregorianCalendar::BC) {
        // Convert to extended year
        year = 1 - year;
    }
    int32_t rawOffset, dstOffset;
    UDate time = (UDate)Grego::fieldsToDay(year, month, day) * U_MILLIS_PER_DAY + millis;
    getOffset(time, true, rawOffset, dstOffset, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    return (rawOffset + dstOffset);
}

void
RuleBasedTimeZone::getOffset(UDate date, UBool local, int32_t& rawOffset,
                             int32_t& dstOffset, UErrorCode& status) const {
    rawOffset = 0;
    dstOffset = 0;

    if (U_FAILURE(status)) {
        return;
    }
    if (!fUpToDate) {
        // Transitions are not yet resolved.  We cannot do it here
        // because this method is const.  Thus, do nothing and return
        // error status.
        status = U_INVALID_STATE_ERROR;
        return;
    }
    const TimeZoneRule *rule = NULL;
    if (fHistoricTransitions == NULL) {
        rule = fInitialRule;
    } else {
        UDate tstart = getTransitionTime((Transition*)fHistoricTransitions->elementAt(0), local);
        if (date < tstart) {
            rule = fInitialRule;
        } else {
            int32_t idx = fHistoricTransitions->size() - 1;
            UDate tend = getTransitionTime((Transition*)fHistoricTransitions->elementAt(idx), local);
            if (date > tend) {
                if (fFinalRules != NULL) {
                    rule = findRuleInFinal(date, local);
                } else {
                    // no final rule, use the last rule
                    rule = ((Transition*)fHistoricTransitions->elementAt(idx))->to;
                }
            } else {
                // Find a historical transition
                while (idx >= 0) {
                    if (date >= getTransitionTime((Transition*)fHistoricTransitions->elementAt(idx), local)) {
                        break;
                    }
                    idx--;
                }
                rule = ((Transition*)fHistoricTransitions->elementAt(idx))->to;
            }
        }
    }
    if (rule != NULL) {
        rawOffset = rule->getRawOffset();
        dstOffset = rule->getDSTSavings();
    }
}

void
RuleBasedTimeZone::setRawOffset(int32_t offsetMillis) {
    // We don't support this operation at this moment.
    // Nothing to do!
}

int32_t
RuleBasedTimeZone::getRawOffset(void) const {
    // Note: This implementation returns standard GMT offset
    // as of current time.
    UErrorCode status = U_ZERO_ERROR;
    int32_t raw, dst;
    getOffset(uprv_getUTCtime() * U_MILLIS_PER_SECOND,
        FALSE, raw, dst, status);
    return raw;
}

UBool
RuleBasedTimeZone::useDaylightTime(void) const {
    // Note: This implementation returns true when
    // daylight saving time is used as of now or
    // after the next transition.
    UErrorCode status = U_ZERO_ERROR;
    UDate now = uprv_getUTCtime() * U_MILLIS_PER_SECOND;
    int32_t raw, dst;
    getOffset(now, FALSE, raw, dst, status);
    if (dst != 0) {
        return TRUE;
    }
    // If DST is not used now, check if DST is used after the next transition
    UDate time;
    TimeZoneRule *from, *to;
    UBool avail = findNext(now, FALSE, time, from, to);
    if (avail && to->getDSTSavings() != 0) {
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::inDaylightTime(UDate date, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return FALSE;
    }
    int32_t raw, dst;
    getOffset(date, FALSE, raw, dst, status);
    if (dst != 0) {
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::hasSameRules(const TimeZone& other) const {
    if (this == &other) {
        return TRUE;
    }
    if (getDynamicClassID() != other.getDynamicClassID()) {
        return FALSE;
    }
    const RuleBasedTimeZone& that = (const RuleBasedTimeZone&)other;
    if (*fInitialRule != *(that.fInitialRule)) {
        return FALSE;
    }
    if (compareRules(fHistoricRules, that.fHistoricRules)
        && compareRules(fFinalRules, that.fFinalRules)) {
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::getNextTransition(UDate base, UBool inclusive, TimeZoneTransition& result) /*const*/ {
    UErrorCode status = U_ZERO_ERROR;
    complete(status);
    if (U_FAILURE(status)) {
        return FALSE;
    }
    UDate transitionTime;
    TimeZoneRule *fromRule, *toRule;
    UBool found = findNext(base, inclusive, transitionTime, fromRule, toRule);
    if (found) {
        result.setTime(transitionTime);
        result.setFrom((const TimeZoneRule&)*fromRule);
        result.setTo((const TimeZoneRule&)*toRule);
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::getPreviousTransition(UDate base, UBool inclusive, TimeZoneTransition& result) /*const*/ {
    UErrorCode status = U_ZERO_ERROR;
    complete(status);
    if (U_FAILURE(status)) {
        return FALSE;
    }
    UDate transitionTime;
    TimeZoneRule *fromRule, *toRule;
    UBool found = findPrev(base, inclusive, transitionTime, fromRule, toRule);
    if (found) {
        result.setTime(transitionTime);
        result.setFrom((const TimeZoneRule&)*fromRule);
        result.setTo((const TimeZoneRule&)*toRule);
        return TRUE;
    }
    return FALSE;
}

InitialTimeZoneRule*
RuleBasedTimeZone::getInitialRule(UErrorCode& status) /*const*/ {
    return fInitialRule->clone();
}

int16_t
RuleBasedTimeZone::countTransitionRules(UErrorCode& status) /*const*/ {
    int16_t count = 0;
    if (fHistoricRules != NULL) {
        count += fHistoricRules->size();
    }
    if (fFinalRules != NULL) {
        count += fFinalRules->size();
    }
    return count;
}

TimeZoneRule*
RuleBasedTimeZone::getTransitionRule(int16_t index, UErrorCode& status) /*const*/ {
    if (U_FAILURE(status)) {
        return NULL;
    }
    int16_t historicCount = fHistoricRules == NULL ? 0 : fHistoricRules->size();
    int16_t finalCount = fFinalRules == NULL ? 0 : fFinalRules->size();
    if (index >= 0 && index < historicCount + finalCount) {
        if (index < historicCount) {
            return ((TimeZoneRule*)fHistoricRules->elementAt(index))->clone();
        } else {
            return ((TimeZoneRule*)fFinalRules->elementAt(index - historicCount))->clone();
        }
    } else {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return NULL;
    }
}

void
RuleBasedTimeZone::deleteRules(void) {
    delete fInitialRule;
    fInitialRule = NULL;
    if (fHistoricRules != NULL) {
        while (!fHistoricRules->isEmpty()) {
            delete (TimeZoneRule*)(fHistoricRules->orphanElementAt(0));
        }
        delete fHistoricRules;
        fHistoricRules = NULL;
    }
    if (fFinalRules != NULL) {
        while (!fFinalRules->isEmpty()) {
            delete (AnnualTimeZoneRule*)(fFinalRules->orphanElementAt(0));
        }
        delete fFinalRules;
        fFinalRules = NULL;
    }
}

void
RuleBasedTimeZone::deleteTransitions(void) {
    if (fHistoricTransitions != NULL) {
        while (!fHistoricTransitions->isEmpty()) {
            Transition *trs = (Transition*)fHistoricTransitions->orphanElementAt(0);
            uprv_free(trs);
        }
        delete fHistoricTransitions;
    }
    fHistoricTransitions = NULL;
}

UVector*
RuleBasedTimeZone::copyRules(UVector* source) {
    if (source == NULL) {
        return NULL;
    }
    UErrorCode ec = U_ZERO_ERROR;
    int32_t size = source->size();
    UVector *rules = new UVector(size, ec);
    if (U_FAILURE(ec)) {
        return NULL;
    }
    int32_t i;
    for (i = 0; i < size; i++) {
        rules->addElement(((TimeZoneRule*)source->elementAt(i))->clone(), ec);
        if (U_FAILURE(ec)) {
            break;
        }
    }
    if (U_FAILURE(ec)) {
        // In case of error, clean up
        for (i = 0; i < rules->size(); i++) {
            TimeZoneRule *rule = (TimeZoneRule*)rules->orphanElementAt(i);
            delete rule;
        }
        delete rules;
        return NULL;
    }
    return rules;
}

TimeZoneRule*
RuleBasedTimeZone::findRuleInFinal(UDate date, UBool local) const {
    if (fFinalRules == NULL) {
        return NULL;
    }

    AnnualTimeZoneRule* fr0 = (AnnualTimeZoneRule*)fFinalRules->elementAt(0);
    AnnualTimeZoneRule* fr1 = (AnnualTimeZoneRule*)fFinalRules->elementAt(1);
    if (fr0 == NULL || fr1 == NULL) {
        return NULL;
    }

    UDate start0, start1;
    UDate base;

    base = local ? date - fr1->getRawOffset() - fr1->getDSTSavings() : date;
    UBool avail0 = fr0->getPreviousStart(base, fr1->getRawOffset(), fr1->getDSTSavings(), true, start0);

    base = local ? date - fr0->getRawOffset() - fr0->getDSTSavings() : date;
    UBool avail1 = fr1->getPreviousStart(base, fr0->getRawOffset(), fr0->getDSTSavings(), true, start1);

    if (avail0 && (!avail1 || start0 > start1)) {
        return fr0;
    } else if (avail1) {
        return fr1;
    }
    return NULL;
}

UBool
RuleBasedTimeZone::findNext(UDate base, UBool inclusive, UDate& transitionTime,
                            TimeZoneRule*& fromRule, TimeZoneRule*& toRule) const {
    if (fHistoricTransitions == NULL) {
        return FALSE;
    }
    UBool isFinal = FALSE;
    UBool found = FALSE;
    Transition result;
    Transition *tzt = (Transition*)fHistoricTransitions->elementAt(0);
    UDate tt = getTransitionTime(tzt, FALSE);
    if (tt > base || (inclusive && tt == base)) {
        result = *tzt;
        found = TRUE;
    } else {
        int32_t idx = fHistoricTransitions->size() - 1;        
        tzt = (Transition*)fHistoricTransitions->elementAt(idx);
        tt = getTransitionTime(tzt, FALSE);
        if (inclusive && tt == base) {
            result = *tzt;
            found = TRUE;
        } else if (tt <= base) {
            if (fFinalRules != NULL) {
                // Find a transion time with finalRules
                TimeZoneRule *r0 = (TimeZoneRule*)fFinalRules->elementAt(0);
                TimeZoneRule *r1 = (TimeZoneRule*)fFinalRules->elementAt(1);
                UDate start0, start1;
                UBool avail0 = r0->getNextStart(base, r1->getRawOffset(), r1->getDSTSavings(), inclusive, start0);
                UBool avail1 = r1->getNextStart(base, r0->getRawOffset(), r0->getDSTSavings(), inclusive, start1);
                //  avail0/avail1 should be always TRUE
                if (start0 < start1) {
                    result.time = start0;
                    result.from = r1;
                    result.to = r0;
                } else {
                    result.time = start1;
                    result.from = r0;
                    result.to = r1;
                }
                isFinal = TRUE;
                found = TRUE;
            }
        } else {
            // Find a transition within the historic transitions
            idx--;
            Transition *prev = tzt;
            while (idx > 0) {
                tzt = (Transition*)fHistoricTransitions->elementAt(idx);
                tt = getTransitionTime(tzt, FALSE);
                if (tt < base || (!inclusive && tt == base)) {
                    break;
                }
                idx--;
                prev = tzt;
            }
            result.time = prev->time;
            result.from = prev->from;
            result.to = prev->to;
            found = TRUE;
        }
    }
    if (found) {
        // For now, this implementation ignore transitions with only zone name changes.
        if (result.from->getRawOffset() == result.to->getRawOffset()
            && result.from->getDSTSavings() == result.to->getDSTSavings()) {
            // No offset changes.  Try next one if not final
            return findNext(result.time, FALSE /* always exclusive */,
                transitionTime, fromRule, toRule);
        }
        transitionTime = result.time;
        fromRule = result.from;
        toRule = result.to;
        return TRUE;
    }
    return FALSE;
}

UBool
RuleBasedTimeZone::findPrev(UDate base, UBool inclusive, UDate& transitionTime,
                            TimeZoneRule*& fromRule, TimeZoneRule*& toRule) const {
    if (fHistoricTransitions == NULL) {
        return FALSE;
    }
    UBool found = FALSE;
    Transition result;
    Transition *tzt = (Transition*)fHistoricTransitions->elementAt(0);
    UDate tt = getTransitionTime(tzt, FALSE);
    if (inclusive && tt == base) {
        result = *tzt;
        found = TRUE;
    } else if (tt < base) {
        int32_t idx = fHistoricTransitions->size() - 1;        
        tzt = (Transition*)fHistoricTransitions->elementAt(idx);
        tt = getTransitionTime(tzt, FALSE);
        if (inclusive && tt == base) {
            result = *tzt;
            found = TRUE;
        } else if (tt < base) {
            if (fFinalRules != NULL) {
                // Find a transion time with finalRules
                TimeZoneRule *r0 = (TimeZoneRule*)fFinalRules->elementAt(0);
                TimeZoneRule *r1 = (TimeZoneRule*)fFinalRules->elementAt(1);
                UDate start0, start1;
                UBool avail0 = r0->getPreviousStart(base, r1->getRawOffset(), r1->getDSTSavings(), inclusive, start0);
                UBool avail1 = r1->getPreviousStart(base, r0->getRawOffset(), r0->getDSTSavings(), inclusive, start1);
                //  avail0/avail1 should be always TRUE
                if (start0 > start1) {
                    result.time = start0;
                    result.from = r1;
                    result.to = r0;
                } else {
                    result.time = start1;
                    result.from = r0;
                    result.to = r1;
                }
            } else {
                result = *tzt;
            }
            found = TRUE;
        } else {
            // Find a transition within the historic transitions
            idx--;
            while (idx >= 0) {
                tzt = (Transition*)fHistoricTransitions->elementAt(idx);
                tt = getTransitionTime(tzt, FALSE);
                if (tt < base || (inclusive && tt == base)) {
                    break;
                }
                idx--;
            }
            result = *tzt;
            found = TRUE;
        }
    }
    if (found) {
        // For now, this implementation ignore transitions with only zone name changes.
        if (result.from->getRawOffset() == result.to->getRawOffset()
            && result.from->getDSTSavings() == result.to->getDSTSavings()) {
            // No offset changes.  Try next one if not final
            return findPrev(result.time, FALSE /* always exclusive */,
                transitionTime, fromRule, toRule);
        }
        transitionTime = result.time;
        fromRule = result.from;
        toRule = result.to;
        return TRUE;
    }
    return FALSE;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof

