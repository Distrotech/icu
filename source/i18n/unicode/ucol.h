/*
*******************************************************************************
* Copyright (c) {1996-2001}, International Business Machines Corporation and others.
* All Rights Reserved.
*******************************************************************************
*/

#ifndef UCOL_H
#define UCOL_H

#include "unicode/utypes.h"
#include "unicode/unorm.h"
#include "unicode/parseerr.h"

/**
 * \file
 * \brief C API: Collator 
 *
 * <h2> Collator C API </h2>
 *
 * The C API for Collator performs locale-sensitive
 * string comparison. You use this class to build
 * searching and sorting routines for natural language text.
 * <em>Important: </em>The ICU collation implementation is being reworked.
 * This means that collation results and especially sort keys will change
 * from ICU 1.6 to 1.7 and again to 1.8.
 * For details, see the <a href="http://oss.software.ibm.com/icu/develop/ICU_collation_design.htm">collation design document</a>.
 *  
 * <p>
 * Like other locale-sensitive classes, you can use the function
 * <code>ucol_open()</code>, to obtain the appropriate pointer to 
 * <code>UCollator</code> object for a given locale. If you need
 * to understand the details of a particular collation strategy or
 * if you need to modify that strategy.
 *
 * <p>
 * The following example shows how to compare two strings using
 * the <code>UCollator</code> for the default locale.
 * <blockquote>
 * <pre>
 * \code
 * // Compare two strings in the default locale
 * UErrorCode success = U_ZERO_ERROR;
 * UCollator* myCollator = ucol_open(NULL, &success);
 * UChar source[4], target[4];
 * u_uastrcpy(source, "abc");
 * u_uastrcpy(target, "ABC");
 * if( u_strcoll(myCollator, source, u_strlen(source), target, u_strlen(target)) == UCOL_LESS) {
 *     printf("abc is less than ABC\n");
 * }else{
 *     printf("abc is greater than or equal to ABC\n");
 * }
 * \endcode
 * </pre>
 * </blockquote>
 *
 * <p>
 * You can set a <code>Collator</code>'s <em>strength</em> property
 * to determine the level of difference considered significant in
 * comparisons. Four strengths are provided: <code>UCOL_PRIMARY</code>,
 * <code>UCOL_SECONDARY</code>, <code>UCOL_TERTIARY</code>, and 
 * <code>UCOL_IDENTICAL</code>. The exact assignment of strengths to 
 * language features is locale dependant.  For example, in Czech, 
 * "e" and "f" are considered primary differences, while "e" and "\u00EA"
 * are secondary differences, "e" and "E" are tertiary differences and 
 * "e" and "e" are identical.
 * The following shows how both case and accents could be ignored for
 * US English.
 * <blockquote>
 * <pre>
 * \code
 * //Get the Collator for US English and set its strength to UCOL_PRIMARY
 * UErrorCode success = U_ZERO_ERROR;
 * UCollator* usCollator = ucol_open("en_US", &success);
 * ucol_setStrength(usCollator, UCOL_PRIMARY);
 * UChar source[4], target[4];
 * u_uastrcpy(source, "abc");
 * u_uastrcpy(target, "ABC");
 * if( u_strcoll(myCollator, source, u_strlen(source), target, u_strlen(target)) == UCOL_EQUAL) {
 *     printf("'abc' and 'ABC' strings are equivalent with strength UCOL_PRIMARY\n");
 * }
 * \endcode
 * </pre>
 * </blockquote>
 * <p>
 * For comparing strings exactly once, the <code>u_strcoll</code>
 * method provides the best performance. When sorting a list of
 * strings however, it is generally necessary to compare each
 * string multiple times. In this case, sort keys
 * provide better performance. The <code>ucol_getSortKey</code> method converts
 * a string to a series of bytes that can be compared bitwise
 * against other sort keys using <code>strcmp()</code>.
 * Sort keys are written as zero-terminated byte strings.
 * They consist of several substrings, one for each collation strength level,
 * that are delimited by 0x01 bytes.
 * If the string code points are appended for UCOL_IDENTICAL, then they are processed
 * for correct code point order comparison and may contain 0x01 bytes
 * but not zero bytes.</p>
 * <p>
 * <strong>Note:</strong> <code>UCollator</code>s with different Locale,
 * Collation Strength and Decomposition Mode settings will return different
 * sort orders for the same set of strings. Locales have specific 
 * collation rules, and the way in which secondary and tertiary differences 
 * are taken into account, for example, will result in a different sorting order
 * for same strings.
 * <p>
 * @see         UCollationResult
 * @see         UNormalizationMode
 * @see            UCollationStrength
 * @see         UCollationElements
 */
struct collIterate;
typedef struct collIterate collIterate;

struct incrementalContext;
typedef struct incrementalContext incrementalContext;

/** A collator.
*  For usage in C programs.
*/
struct UCollator;
typedef struct UCollator UCollator;


    /**
     * UCOL_LESS is returned if source string is compared to be less than target
     * string in the u_strcoll() method.
     * UCOL_EQUAL is returned if source string is compared to be equal to target
     * string in the u_strcoll() method.
     * UCOL_GREATER is returned if source string is compared to be greater than
     * target string in the u_strcoll() method.
     * @see u_strcoll()
     **/
/** Possible values for a comparison result */
typedef enum {
  /** string a == string b */
  UCOL_EQUAL    = 0,
  /** string a > string b */
  UCOL_GREATER    = 1,
  /** string a < string b */
  UCOL_LESS    = -1
} UCollationResult ;


typedef enum {
  /* accepted by most attributes */
  UCOL_DEFAULT = -1,

  /* for UCOL_STRENGTH */
  /** Primary collation strength */
  UCOL_PRIMARY = 0,
  /** Secondary collation strength */
  UCOL_SECONDARY = 1,
  /** Tertiary collation strength */
  UCOL_TERTIARY = 2,
  /** Default collation strength */
  UCOL_DEFAULT_STRENGTH = UCOL_TERTIARY,
  UCOL_CE_STRENGTH_LIMIT,
  /** Quaternary collation strength */
  UCOL_QUATERNARY=3,
  /** Identical collation strength */
  UCOL_IDENTICAL=15,
  UCOL_STRENGTH_LIMIT,

  /* for UCOL_FRENCH_COLLATION, UCOL_CASE_LEVEL & UCOL_DECOMPOSITION_MODE*/
  UCOL_OFF = 16,
  UCOL_ON = 17,
  
  /* for UCOL_ALTERNATE_HANDLING */
  UCOL_SHIFTED = 20,
  UCOL_NON_IGNORABLE = 21,

  /* for UCOL_CASE_FIRST */
  UCOL_LOWER_FIRST = 24,
  UCOL_UPPER_FIRST = 25,

  /* for UCOL_NORMALIZATION_MODE */
  UCOL_ON_WITHOUT_HANGUL = 28,

  /** No more attribute values after this*/
  UCOL_ATTRIBUTE_VALUE_COUNT

} UColAttributeValue;

    /**
     * Base letter represents a primary difference.  Set comparison
     * level to UCOL_PRIMARY to ignore secondary and tertiary differences.
     * Use this to set the strength of a Collator object.
     * Example of primary difference, "abc" &lt; "abd"
     * 
     * Diacritical differences on the same base letter represent a secondary
     * difference.  Set comparison level to UCOL_SECONDARY to ignore tertiary
     * differences. Use this to set the strength of a Collator object.
     * Example of secondary difference, "�" >> "a".
     *
     * Uppercase and lowercase versions of the same character represents a
     * tertiary difference.  Set comparison level to UCOL_TERTIARY to include
     * all comparison differences. Use this to set the strength of a Collator
     * object.
     * Example of tertiary difference, "abc" &lt;&lt;&lt; "ABC".
     *
     * Two characters are considered "identical" when they have the same
     * unicode spellings.  UCOL_IDENTICAL.
     * For example, "�" == "�".
     *
     * UCollationStrength is also used to determine the strength of sort keys 
     * generated from UCollator objects
     **/
/** Possible collation strengths  - all under UColAttributeValue*/
typedef UColAttributeValue UCollationStrength;

typedef enum {
     UCOL_FRENCH_COLLATION, /* attribute for direction of secondary weights*/
     UCOL_ALTERNATE_HANDLING, /* attribute for handling variable elements*/
     UCOL_CASE_FIRST, /* who goes first, lower case or uppercase */
     UCOL_CASE_LEVEL, /* do we have an extra case level */
     UCOL_NORMALIZATION_MODE, /* attribute for normalization */
     UCOL_DECOMPOSITION_MODE = UCOL_NORMALIZATION_MODE,
     UCOL_STRENGTH,         /* attribute for strength */
     UCOL_ATTRIBUTE_COUNT
} UColAttribute;

typedef enum {
	UCOL_TAILORING_ONLY,
	UCOL_FULL_RULES
}  UColRuleOption ;

/**
 * Open a UCollator for comparing strings.
 * The UCollator may be used in calls to \Ref{ucol_strcoll}.
 * @param loc The locale containing the comparison conventions.
 * @param status A pointer to an UErrorCode to receive any errors
 * @return A pointer to a UCollator, or 0 if an error occurred.
 * @see ucol_openRules
 * @stable
 */

U_CAPI UCollator*
ucol_open(    const    char         *loc,
        UErrorCode      *status);

/**
 * Open a UCollator with a specific version.
 * This is the same as ucol_open() except that ucol_getVersion() of
 * the returned object is guaranteed to be the same as the version
 * parameter.
 * This is designed to be used to open the same collator for a given
 * locale even when ICU is updated.
 * The same locale and version guarantees the same sort keys and
 * comparison results.
 *
 * @param loc The locale ID for which to open a collator.
 * @param version The requested collator version.
 * @param status A pointer to a UErrorCode,
 *               must not indicate a failure before calling this function.
 * @return A pointer to a UCollator, or NULL if an error occurred
 *         or a collator with the requested version is not available.
 *
 * @see ucol_open
 * @see ucol_getVersion
 * @draft ICU 1.8
 */
U_CAPI UCollator * U_EXPORT2
ucol_openVersion(const char *loc,
                 UVersionInfo version,
                 UErrorCode *status);

/**
 * Open a UCollator for comparing strings.
 * The UCollator may be used in calls to \Ref{ucol_strcoll}.
 * @param rules A string describing the collation rules.
 * @param rulesLength The length of rules, or -1 if null-terminated.
 * @param mode The normalization mode; one of UCOL_NO_NORMALIZATION,
 * UCOL_CAN_DECOMP, UCOL_COMPAT_DECOMP, UCOL_CAN_DECOMP_COMPAT_COMP,
 * UCOL_COMPAT_DECOMP_CAN_COMP, UCOL_DEFAULT_NORMALIZATION
 * @param strength The collation strength; one of UCOL_PRIMARY, UCOL_SECONDARY,
 * UCOL_TERTIARY, UCOL_IDENTICAL,UCOL_DEFAULT_STRENGTH
 * @param status A pointer to an UErrorCode to receive any errors
 * @param parseError  A pointer to UParseError to recieve information about errors
 *                    occurred during parsing.
 * @return A pointer to a UCollator, or 0 if an error occurred.
 * @see ucol_open
 * @stable
 */
U_CAPI UCollator*
ucol_openRules( const UChar        *rules,
                int32_t            rulesLength,
                UNormalizationMode mode,
                UCollationStrength strength,
                UParseError        *parseError,
                UErrorCode         *status);

/** 
 * Close a UCollator.
 * Once closed, a UCollator should not be used.
 * @param coll The UCollator to close.
 * @stable
 */
U_CAPI void
ucol_close(UCollator *coll);

/**
 * Compare two strings.
 * The strings will be compared using the normalization mode and options
 * specified in \Ref{ucol_open} or \Ref{ucol_openRules}
 * @param coll The UCollator containing the comparison rules.
 * @param source The source string.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param target The target string.
 * @param targetLength The length of target, or -1 if null-terminated.
 * @return The result of comparing the strings; one of UCOL_EQUAL,
 * UCOL_GREATER, UCOL_LESS
 * @see ucol_greater
 * @see ucol_greaterOrEqual
 * @see ucol_equal
 * @stable
 */

U_CAPI UCollationResult
ucol_strcoll(    const    UCollator    *coll,
        const    UChar        *source,
        int32_t            sourceLength,
        const    UChar        *target,
        int32_t            targetLength);

/**
 * Determine if one string is greater than another.
 * This function is equivalent to \Ref{ucol_strcoll} == UCOL_GREATER
 * @param coll The UCollator containing the comparison rules.
 * @param source The source string.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param target The target string.
 * @param targetLength The length of target, or -1 if null-terminated.
 * @return TRUE if source is greater than target, FALSE otherwise.
 * @see ucol_strcoll
 * @see ucol_greaterOrEqual
 * @see ucol_equal
 * @stable
 */
U_CAPI UBool
ucol_greater(    const    UCollator    *coll,
        const    UChar        *source,
        int32_t            sourceLength,
        const    UChar        *target,
        int32_t            targetLength);

/**
 * Determine if one string is greater than or equal to another.
 * This function is equivalent to \Ref{ucol_strcoll} != UCOL_LESS
 * @param coll The UCollator containing the comparison rules.
 * @param source The source string.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param target The target string.
 * @param targetLength The length of target, or -1 if null-terminated.
 * @return TRUE if source is greater than or equal to target, FALSE otherwise.
 * @see ucol_strcoll
 * @see ucol_greater
 * @see ucol_equal
 * @stable
 */
U_CAPI UBool
ucol_greaterOrEqual(    const    UCollator    *coll,
            const    UChar        *source,
            int32_t            sourceLength,
            const    UChar        *target,
            int32_t            targetLength);

/**
 * Compare two strings for equality.
 * This function is equivalent to \Ref{ucol_strcoll} == UCOL_EQUAL
 * @param coll The UCollator containing the comparison rules.
 * @param source The source string.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param target The target string.
 * @param targetLength The length of target, or -1 if null-terminated.
 * @return TRUE if source is equal to target, FALSE otherwise
 * @see ucol_strcoll
 * @see ucol_greater
 * @see ucol_greaterOrEqual
 * @stable
 */
U_CAPI UBool
ucol_equal(    const    UCollator    *coll,
        const    UChar        *source,
        int32_t            sourceLength,
        const    UChar        *target,
        int32_t            targetLength);

/**
 * Get the collation strength used in a UCollator.
 * The strength influences how strings are compared.
 * @param coll The UCollator to query.
 * @return The collation strength; one of UCOL_PRIMARY, UCOL_SECONDARY,
 * UCOL_TERTIARY, UCOL_IDENTICAL, UCOL_DEFAULT_STRENGTH
 * @see ucol_setStrength
 * @stable
 */
U_CAPI UCollationStrength
ucol_getStrength(const UCollator *coll);

/**
 * Set the collation strength used in a UCollator.
 * The strength influences how strings are compared.
 * <p>Example of use:
 * <pre>
 * .       UCollationResult result;
 * .       UChar *source, *target;
 * .       UErrorCode status = U_ZERO_ERROR;
 * .       UCollator *myCollation = ucol_open("en_US", status);
 * .       if (U_FAILURE(&status)) return;
 * .       ucol_setStrength(myCollation, UCOL_PRIMARY);
 * .       u_uastrcpy(source, "abc");
 * .       u_uastrcpy(target, "ABC");
 * .       // result will be "abc" == "ABC"
 * .       // tertiary differences will be ignored
 * .       result = ucol_strcoll(myCollation, source, u_strlen(source), target, u_strlen(target));
 * </pre>
 * @param coll The UCollator to set.
 * @param strength The desired collation strength; one of UCOL_PRIMARY, 
 * UCOL_SECONDARY, UCOL_TERTIARY, UCOL_IDENTICAL, UCOL_DEFAULT_STRENGTH
 * @see ucol_getStrength
 * @stable
 */
U_CAPI void
ucol_setStrength(    UCollator            *coll,
            UCollationStrength        strength);

/**
 * Get the normalization mode used in a UCollator.
 * The normalization mode influences how strings are compared.
 * @param coll The UCollator to query.
 * @return The normalization mode; one of UCOL_NO_NORMALIZATION, 
 * UCOL_CAN_DECOMP, UCOL_COMPAT_DECOMP, UCOL_CAN_DECOMP_COMPAT_COMP,
 * UCOL_COMPAT_DECOMP_CAN_COMP, UCOL_DEFAULT_NORMALIZATION
 * @see ucol_setNormalization
 * @stable
 */
U_CAPI UNormalizationMode
ucol_getNormalization(const UCollator* coll);

/**
 * Set the normalization mode used in a UCollator.
 * The normalization mode influences how strings are compared.
 * @param coll The UCollator to set.
 * @param mode The desired normalization mode; one of UCOL_NO_NORMALIZATION,
 * UCOL_CAN_DECOMP, UCOL_COMPAT_DECOMP, UCOL_CAN_DECOMP_COMPAT_COMP, 
 * UCOL_COMPAT_DECOMP_CAN_COMP, UCOL_DEFAULT_NORMALIZATION
 * @see ucol_getNormalization
 * @stable
 */
U_CAPI void
ucol_setNormalization(  UCollator        *coll,
            UNormalizationMode    mode);

/**
 * Get the display name for a UCollator.
 * The display name is suitable for presentation to a user.
 * @param objLoc The locale of the collator in question.
 * @param dispLoc The locale for display.
 * @param result A pointer to a buffer to receive the attribute.
 * @param resultLength The maximum size of result.
 * @param status A pointer to an UErrorCode to receive any errors
 * @return The total buffer size needed; if greater than resultLength,
 * the output was truncated.
 * @stable
 */
U_CAPI int32_t
ucol_getDisplayName(    const    char        *objLoc,
            const    char        *dispLoc,
            UChar             *result,
            int32_t         resultLength,
            UErrorCode        *status);

/**
 * Get a locale for which collation rules are available.
 * A UCollator in a locale returned by this function will perform the correct
 * collation for the locale.
 * @param index The index of the desired locale.
 * @return A locale for which collation rules are available, or 0 if none.
 * @see ucol_countAvailable
 * @stable
 */
U_CAPI const char*
ucol_getAvailable(int32_t index);

/**
 * Determine how many locales have collation rules available.
 * This function is most useful as determining the loop ending condition for
 * calls to \Ref{ucol_getAvailable}.
 * @return The number of locales for which collation rules are available.
 * @see ucol_getAvailable
 * @stable
 */
U_CAPI int32_t
ucol_countAvailable(void);

/**
 * Get the collation rules from a UCollator.
 * The rules will follow the rule syntax.
 * @param coll The UCollator to query.
 * @param length 
 * @return The collation rules.
 * @stable
 */
U_CAPI const UChar*
ucol_getRules(    const    UCollator    *coll, 
        int32_t            *length);

/**
 * Get a sort key for a string from a UCollator.
 * Sort keys may be compared using <TT>strcmp</TT>.
 * @param coll The UCollator containing the collation rules.
 * @param source The string to transform.
 * @param sourecLength The length of source, or -1 if null-terminated.
 * @param result A pointer to a buffer to receive the attribute.
 * @param resultLength The maximum size of result.
 * @return The size needed to fully store the sort key..
 * @see ucol_keyHashCode
 * @stable
 */
U_CAPI int32_t
ucol_getSortKey(const    UCollator    *coll,
        const    UChar        *source,
        int32_t        sourceLength,
        uint8_t        *result,
        int32_t        resultLength);



/**
 * Gets the version information for a Collator. 
 * @param info the version # information, the result will be filled in
 * @stable
 */
U_CAPI void U_EXPORT2
ucol_getVersion(const UCollator* coll, UVersionInfo info);


/* Following are the new APIs for 1.7. They are all draft and most are not even implemented */

/**
 * Universal attribute setter
 * @param coll collator which attributes are to be changed
 * @param attr attribute type 
 * @param value attribute value
 * @param status to indicate whether the operation went on smoothly or there were errors
 * @draft
 */
U_CAPI void ucol_setAttribute(UCollator *coll, UColAttribute attr, UColAttributeValue value, UErrorCode *status);

/**
 * Universal attribute getter
 * @param coll collator which attributes are to be changed
 * @param attr attribute type
 * @return attribute value
 * @param status to indicate whether the operation went on smoothly or there were errors
 * @draft
 */
U_CAPI UColAttributeValue ucol_getAttribute(const UCollator *coll, UColAttribute attr, UErrorCode *status);

/** 
 * Sets the variable top to a collation element value of a string supplied. 
 * @param coll collator which variable top needs to be changed
 * @param varTop one or more (if contraction) UChars to which the variable top should be set
 * @param len length of variable top string. If -1 it is considered to be zero terminated.
 * @param status error code. If error code is set, the return value is undefined. Errors set by this function are: <br>
 *    U_CE_NOT_FOUND_ERROR if more than one character was passed and there is no such a contraction<br>
 *    U_PRIMARY_TOO_LONG_ERROR if the primary for the variable top has more than two bytes
 * @return a 32 bit value containing the value of the variable top in upper 16 bits. Lower 16 bits are undefined
 * @draft
 */
U_CAPI uint32_t ucol_setVariableTop(UCollator *coll, const UChar *varTop, int32_t len, UErrorCode *status);

/** 
 * Gets the variable top value of a Collator. 
 * Lower 16 bits are undefined and should be ignored.
 * @param coll collator which variable top needs to be retrieved
 * @param status error code (not changed by function). If error code is set, the return value is undefined.
 * @draft
 */
U_CAPI uint32_t ucol_getVariableTop(const UCollator *coll, UErrorCode *status);

/** 
 * Sets the variable top to a collation element value supplied. Variable top is set to the upper 16 bits. 
 * Lower 16 bits are ignored.
 * @param coll collator which variable top needs to be changed
 * @param varTop CE value, as returned by ucol_setVariableTop or ucol)getVariableTop
 * @param status error code (not changed by function)
 * @draft
 */
U_CAPI void ucol_restoreVariableTop(UCollator *coll, const uint32_t varTop, UErrorCode *status);

/**
 * Thread safe cloning operation
 * @param coll collator to be cloned
 * @param stackBuffer user allocated space for the new clone. If NULL new memory will be allocated. 
	If buffer is not lareg enough, new memory will be allocated.
	Clients can use the U_COL_SAFECLONE_BUFFERSIZE. 
	This will probably be enough to avoid memory allocations.
 * @param pBufferSize pointer to size of allocated space. 
	If *pBufferSize == 0, a sufficient size for use in cloning will 
	be returned ('pre-flighting')
	If *pBufferSize is not enough for a stack-based safe clone, 
	new memory will be allocated.
 * @param status to indicate whether the operation went on smoothly or there were errors
    An informational status value, U_SAFECLONE_ALLOCATED_ERROR, is used if any allocations were necessary.
 * @return pointer to the new clone
 * @draft API 1.8 freeze
 */


U_CAPI UCollator * ucol_safeClone(
          const UCollator     *coll,
          void                *stackBuffer,
          int32_t        *pBufferSize,
          UErrorCode          *status);

#define U_COL_SAFECLONE_BUFFERSIZE 384

/**
 * Returns current rules. Delta defines whether full rules are returned or just the tailoring. 
 * Returns number of UChars needed to store rules. If buffer is NULL or bufferLen is not enough 
 * to store rules, will store up to available space.
 * @param coll collator to get the rules from
 * @param delta one of 	UCOL_TAILORING_ONLY, UCOL_FULL_RULES. 
 * @param buffer buffer to store the result in. If NULL, you'll get no rules.
 * @param bufferLen lenght of buffer to store rules in. If less then needed you'll get only the part that fits in.
 */
U_CAPI int32_t ucol_getRulesEx(const UCollator *coll, UColRuleOption delta, UChar *buffer, int32_t bufferLen);

/* This is the C API wrapper for CollationIterator that got booted out from here, including just for */
/* include backward compatibility */
#include "unicode/ucoleitr.h"

typedef enum {
	UCOL_JUST_STATE = 0,
	UCOL_INCLUDE_TAILORING,
	UCOL_INCLUDE_TAILORING_AND_UCA
} UColStateEnum;

/**
 * This API would save (freeze) the current state of a collator so that a collator with same functionality 
 * can be later constructed according to this data. The state blob can optionally contain binary tailoring 
 * data as well as the binary version of the UCA, in order to facilitate independence from ICU data. 
 * @draft
 */
int32_t ucol_getState(const UCollator *coll, UColStateEnum contents, 
                      uint8_t *buffer, int32_t buflen, 
                      UErrorCode *status);

/**
 * This API would compare the frozen state with the current implementation of ICU and will set status to one of four values:
 * U_ZERO_ERROR - most desirable result, meaning that the frozen collator in entirety matches the current ICU implementation, and that the same results would be obtained with simple opening of a collator (with a caveat: opening from state resurrects the attributes, while simple opening doesn't). 
 * U_COLLATOR_OLD_WARN - this is a warning. This collator can be constructed - however, the current implementation would not give the same results, so compatibility layer is used. This fact can affect performance and memory usage of collation. Also, it means that eventually this collator will not be available. It is users responsibility to consider regenerating the data that depends on the collator while it is 'old'.
 * U_MISSING_RESOURCE_ERROR - this is an error that means that the data required to construct a collator is missing. If the user supplies the required data, it will be able to construct the collator, either in regular OR compatibility mode.
 * U_COLLATOR_VERSION_TOO_OLD_ERROR - This is an error and means that this collator cannot be instantiated because this version is no longer supported by ICU. The user needs either to regenerate the data OR install an older version of ICU.
 * @draft
 */
void ucol_checkState (const uint8_t *state, UErrorCode *status);

/**
 * This API tries to construct a collator based on a state blob passed. 
 * @draft
 */
UCollator *ucol_openState(const uint8_t *state, UErrorCode *status);


/********************************* Deprecated API ********************************/
/**
 *@deprecated Remove after Aug 2002
 */
#ifdef U_USE_DEPRECATED_FORMAT_API

#if ((U_ICU_VERSION_MAJOR_NUM != 1) || (U_ICU_VERSION_MINOR_NUM !=9))
#   error "ICU version has changed. Please redefine the macros under U_USE_DEPRECATED_FORMAT_API pre-processor definition"
#else 
#   define ucol_openRules_1_9(rules,rulesLength,modes,strength,status) ucol_openRules(rules,rulesLength,modes,strength,NULL,status)
#endif

#endif
/********************************* End *******************************************/

#endif
