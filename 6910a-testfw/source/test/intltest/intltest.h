/********************************************************************
 * COPYRIGHT: 
 * Copyright (c) 1997-2012, International Business Machines Corporation and
 * others. All Rights Reserved.
 ********************************************************************/


/**
 * IntlTest is a base class for tests.  */

#ifndef _INTLTEST
#define _INTLTEST

// The following includes utypes.h, uobject.h and unistr.h
#include "unicode/fmtable.h"
#include "unicode/testlog.h"

/**
 * \def ICU_USE_THREADS
 *
 * Enables multi-threaded testing. Moved here from uconfig.h.
 * Default: enabled
 *
 * This switched used to allow thread support (use of mutexes) to be compiled out of ICU.
 */
#ifdef ICU_USE_THREADS
    /* Use the predefined value. */
#elif defined(APP_NO_THREADS)
    /* APP_NO_THREADS is an old symbol. We'll honour it if present. */
#   define ICU_USE_THREADS 0
#else
#   define ICU_USE_THREADS 1
#endif

U_NAMESPACE_USE

#if U_PLATFORM == U_PF_OS390
// avoid collision with math.h/log()
// this must be after including utypes.h so that U_PLATFORM is actually defined
#pragma map(IntlTest::log( const UnicodeString &message ),"logos390")
#endif

//-----------------------------------------------------------------------------
//convenience classes to ease porting code that uses the Java
//string-concatenation operator (moved from findword test by rtg)
UnicodeString UCharToUnicodeString(UChar c);
UnicodeString Int64ToUnicodeString(int64_t num);
//UnicodeString operator+(const UnicodeString& left, int64_t num); // Some compilers don't allow this because of the long type.
UnicodeString operator+(const UnicodeString& left, long num);
UnicodeString operator+(const UnicodeString& left, unsigned long num);
UnicodeString operator+(const UnicodeString& left, double num);
UnicodeString operator+(const UnicodeString& left, char num); 
UnicodeString operator+(const UnicodeString& left, short num);  
UnicodeString operator+(const UnicodeString& left, int num);      
UnicodeString operator+(const UnicodeString& left, unsigned char num);  
UnicodeString operator+(const UnicodeString& left, unsigned short num);  
UnicodeString operator+(const UnicodeString& left, unsigned int num);      
UnicodeString operator+(const UnicodeString& left, float num);
#if !UCONFIG_NO_FORMATTING
UnicodeString toString(const Formattable& f); // liu
UnicodeString toString(int32_t n);
#endif

// CString - A convenience class for converting UnicodeStrings to (char *) strings
//           intended for use in composing error messages.
//           Depending on the default charset, the conversion may be lossy.
//    Typical Usage:
//       errln("the string was: %s", CString(some_unicode_string).data());

class CString {
  public:
    CString(const UnicodeString &us);
    ~CString();
    const char *c_str() {return data_;};
  private:
    char *data_;
};

//-----------------------------------------------------------------------------

// Use the TESTCASE macro in subclasses of IntlTest.  Define the
// runIndexedTest method in this fashion:
//
//| void MyTest::runIndexedTest(int32_t index, UBool exec,
//|                             const char* &name, char* /*par*/) {
//|     switch (index) {
//|         TESTCASE(0,TestSomething);
//|         TESTCASE(1,TestSomethingElse);
//|         TESTCASE(2,TestAnotherThing);
//|         default: name = ""; break;
//|     }
//| }
#define TESTCASE(id,test)             \
    case id:                          \
        name = #test;                 \
        if (exec) {                   \
            logln(#test "---");       \
            logln();                  \
            test();                   \
        }                             \
        break

// More convenient macros. These allow easy reordering of the test cases.
//
//| void MyTest::runIndexedTest(int32_t index, UBool exec,
//|                             const char* &name, char* /*par*/) {
//|     TESTCASE_AUTO_BEGIN;
//|     TESTCASE_AUTO(TestSomething);
//|     TESTCASE_AUTO(TestSomethingElse);
//|     TESTCASE_AUTO(TestAnotherThing);
//|     TESTCASE_AUTO_END;
//| }
#define TESTCASE_AUTO_BEGIN \
    for(;;) { \
        int32_t testCaseAutoNumber = 0

#define TESTCASE_AUTO(test) \
        if (index == testCaseAutoNumber++) { \
            name = #test; \
            if (exec) { \
                logln(#test "---"); \
                logln(); \
                test(); \
            } \
            break; \
        }

#define TESTCASE_AUTO_END \
        name = ""; \
        break; \
    }

class IntlTest : public TestLog {
public:

    IntlTest();
    // TestLog has a virtual destructor.

    virtual UBool runTest( char* name = NULL, char* par = NULL, char *baseName = NULL); // not to be overidden

    virtual UBool setVerbose( UBool verbose = TRUE );
    virtual UBool setNoErrMsg( UBool no_err_msg = TRUE );
    virtual UBool setQuick( UBool quick = TRUE );
    virtual UBool setLeaks( UBool leaks = TRUE );
    virtual UBool setNotime( UBool no_time = TRUE );
    virtual UBool setWarnOnMissingData( UBool warn_on_missing_data = TRUE );
    virtual int32_t setThreadCount( int32_t count = 1);

    virtual int32_t getErrors( void );
    virtual int32_t getDataErrors (void );

    virtual void setCaller( IntlTest* callingTest ); // for internal use only
    virtual void setPath( char* path ); // for internal use only

    virtual void log( const UnicodeString &message );

    virtual void logln( const UnicodeString &message );

    virtual void logln( void );

    virtual void info( const UnicodeString &message );

    virtual void infoln( const UnicodeString &message );

    virtual void infoln( void );

    virtual void err(void);
    
    virtual void err( const UnicodeString &message );

    virtual void errln( const UnicodeString &message );

    virtual void dataerr( const UnicodeString &message );

    virtual void dataerrln( const UnicodeString &message );
    
    void errcheckln(UErrorCode status, const UnicodeString &message );

    // convenience functions: sprintf() + errln() etc.
    void log(const char *fmt, ...);
    void logln(const char *fmt, ...);
    void info(const char *fmt, ...);
    void infoln(const char *fmt, ...);
    void err(const char *fmt, ...);
    void errln(const char *fmt, ...);
    void dataerr(const char *fmt, ...);
    void dataerrln(const char *fmt, ...);
    void errcheckln(UErrorCode status, const char *fmt, ...);

  private:
    void dataerr(const char *message, UBool newLine);
  public:

    // Print ALL named errors encountered so far
    void printErrors(); 
        
    virtual void usage( void ) ;

    /**
     * Returns a uniform random value x, with 0.0 <= x < 1.0.  Use
     * with care: Does not return all possible values; returns one of
     * 714,025 values, uniformly spaced.  However, the period is
     * effectively infinite.  See: Numerical Recipes, section 7.1.
     *
     * @param seedp pointer to seed. Set *seedp to any negative value
     * to restart the sequence.
     */
    static float random(int32_t* seedp);

    /**
     * Convenience method using a global seed.
     */
    static float random();

    /**
     * Returns true if u_getVersion() < major.minor.
     */
    static UBool isICUVersionBefore(int major, int minor) {
        return isICUVersionBefore(major, minor, 0);
    }

    /**
     * Returns true if u_getVersion() < major.minor.milli.
     */
    static UBool isICUVersionBefore(int major, int minor, int milli);

    /**
     * Returns true if u_getVersion() >= major.minor.
     */
    static UBool isICUVersionAtLeast(int major, int minor) {
        return isICUVersionAtLeast(major, minor, 0);
    }

    /**
     * Returns true if u_getVersion() >= major.minor.milli.
     */
    static UBool isICUVersionAtLeast(int major, int minor, int milli) {
        return !isICUVersionBefore(major, minor, milli);
    }

    enum { kMaxProps = 16 };

    virtual void setProperty(const char* propline);
    virtual const char* getProperty(const char* prop);

protected:
    /* JUnit-like assertions. Each returns TRUE if it succeeds. */
    UBool assertTrue(const char* message, UBool condition, UBool quiet=FALSE, UBool possibleDataError=FALSE);
    UBool assertFalse(const char* message, UBool condition, UBool quiet=FALSE);
    UBool assertSuccess(const char* message, UErrorCode ec, UBool possibleDataError=FALSE);
    UBool assertEquals(const char* message, const UnicodeString& expected,
                       const UnicodeString& actual, UBool possibleDataError=FALSE);
    UBool assertEquals(const char* message, const char* expected,
                       const char* actual);
#if !UCONFIG_NO_FORMATTING
    UBool assertEquals(const char* message, const Formattable& expected,
                       const Formattable& actual);
    UBool assertEquals(const UnicodeString& message, const Formattable& expected,
                       const Formattable& actual);
#endif
    UBool assertTrue(const UnicodeString& message, UBool condition, UBool quiet=FALSE);
    UBool assertFalse(const UnicodeString& message, UBool condition, UBool quiet=FALSE);
    UBool assertSuccess(const UnicodeString& message, UErrorCode ec);
    UBool assertEquals(const UnicodeString& message, const UnicodeString& expected,
                       const UnicodeString& actual);
    UBool assertEquals(const UnicodeString& message, const char* expected,
                       const char* actual);

    virtual void runIndexedTest( int32_t index, UBool exec, const char* &name, char* par = NULL ); // overide !

    virtual UBool runTestLoop( char* testname, char* par, char *baseName );

    virtual int32_t IncErrorCount( void );

    virtual int32_t IncDataErrorCount( void );

    virtual UBool callTest( IntlTest& testToBeCalled, char* par );



    /*
     * Macro-based assertions
     *
     *  ASSERT_TRUE((UBool condition [, const char *message [, message parameters ..]]))
     *  ASSERT_FALSE((UBool condition [, const char *message [, message parameters ..]]))
     *  ASSERT_SUCCESS((UErrorCode ec [, const char *message [, message parameters ..]]))
     *  ASSERT_EQUALS((int expected, int actual [, const char *message [, message parameters ..]]))
     *  ASSERT_EQUALS((String expected, String actual [, const char *message [, message parameters ..]]))
     *
     * Notes:
     *  The macros may only be invoked from within a member function of a class derived from IntlTest.
     *  
     *  On failure they will display a default error message with the file and line number of the error,
     *  the condition being tested, and the expected and actual values.  Any additional caller-supplied
     *  message is optional, and is typically only useful for providing additional context for tests
     *  that are looping over data.
     *
     *  ASSERT_EQUALS(String, String) will work with UnicodeString, (const char *)strings or "quoted" strings,
     *  in any combination.  For portability, quoted and char * strings as expected results
     *  should be restricted to invariant characters.
     *
     */

    #define ASSERT_TRUE(args) assertImpl1(__FILE__, __LINE__, #args, assertTrueHelper args)
    #define ASSERT_FALSE(args) assertImpl1(__FILE__, __LINE__, #args, assertFalseHelper args)
    #define ASSERT_EQUALS(args) assertImpl2(__FILE__, __LINE__, #args, assertEqualsHelper args)
    #define ASSERT_SUCCESS(args) assertImpl1(__FILE__, __LINE__, #args, assertSuccessHelper args)

    // assert Helper functions
    //    Parameters are exactly as passed by the test program.
    //    Normal function overloading resolution selects the appropriate function
    //       at each ASSERT macro usage.
    //    Return is null if the test passes.
    //       Return is malloced char * string with the formatted message if the test fails.
    //
    const char *assertTrueHelper(UBool actual);
    const char *assertTrueHelper(UBool actual, const char *msg, ...);
    const char *assertFalseHelper(UBool actual);
    const char *assertFalseHelper(UBool actual, const char *msg, ...);

    const char *assertEqualsHelper(int64_t expected, int64_t actual);
    const char *assertEqualsHelper(int64_t expected, int64_t actual, 
                                   const char *msg, ...);
    const char *assertEqualsHelper(const char *expected, const char *actual);
    const char *assertEqualsHelper(const char *expected, const char *actual,
                                   const char *msg, ...);
    const char *assertEqualsHelper(const char *expected, const StringPiece &actual);
    const char *assertEqualsHelper(const char *expected, const StringPiece &actual,
                                   const char *msg, ...);
    const char *assertEqualsHelper(const char *expected, const UnicodeString &actual);
    const char *assertEqualsHelper(const char *expected, const UnicodeString &actual,
                                   const char *msg, ...);
    const char *assertEqualsHelper(const UnicodeString &expected, const UnicodeString &actual);
    const char *assertEqualsHelper(const UnicodeString &expected, const UnicodeString &actual,
                                   const char *msg, ...);

    const char *assertSuccessHelper(UErrorCode actual);
    const char *assertSuccessHelper(UErrorCode actual, const char *msg, ...);

                               
    // assertImpl2()
    //    If the msg string is NULL then the test passed, return quietly.
    //    otherwise print the message prefixed by the file & line info.
    //    (specialized for two argument asserts), prefix by the source statement)
    UBool assertImpl2(const char *fileName, int lineNum, const char *argString, const char *msg);
    UBool assertImpl1(const char *fileName, int lineNum, const char *argString, const char *msg);

#if 0
    UBool       assertFalseImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UBool condition, 
                               const char *message, ...);
    UBool       assertFalseImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UBool condition);

    UBool       assertSuccessImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UErrorCode ec, 
                               const char *message, ...);
    UBool       assertSuccessImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UErrorCode ec);
    
    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               int64_t expected, int64_t actual,
                               const char *message, ...);
    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               int64_t expected, int64_t actual);

    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UnicodeString expected, UnicodeString actual);
    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               UnicodeString expected, UnicodeString actual,
                               const char *message, ...);

    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               StringPiece expected, StringPiece actual);
    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               StringPiece expected, StringPiece actual,
                               const char *message, ...);
     
    template <typename T>
    UBool       assertEqualsImpl(const char *fileName, int32_t lineNumber, 
                               const char *macroArgs, 
                               const T& expected, const T& actual,
                               const char *message, ...);


#endif

    void        displayAssert(const char *formattedMessage, UBool passing, int32_t options);


    UBool       verbose;
    UBool       no_err_msg;
    UBool       quick;
    UBool       leaks;
    UBool       warn_on_missing_data;
    UBool       no_time;
    int32_t     threadCount;

private:
    UBool       LL_linestart;
    int32_t     LL_indentlevel;

    int32_t     errorCount;
    int32_t     dataErrorCount;
    IntlTest*   caller;
    char*       testPath;           // specifies subtests
    
    char basePath[1024];

    //FILE *testoutfp;
    void *testoutfp;

    const char* proplines[kMaxProps];
    int32_t     numProps;

protected:

    /**
      * Write out the message string, with all line(s) indented by the current indent amount,
      *  and an optional added newline at the end.
      */
    virtual void LL_message( UnicodeString message, UBool newline );
    virtual void LL_message(const char *message, UBool newline);

    // used for collation result reporting, defined here for convenience

    static UnicodeString &prettify(const UnicodeString &source, UnicodeString &target);
    static UnicodeString prettify(const UnicodeString &source, UBool parseBackslash=FALSE);
    static UnicodeString &appendHex(uint32_t number, int32_t digits, UnicodeString &target);

public:
    static void setICU_DATA();       // Set up ICU_DATA if necessary.

    static const char* pathToDataDirectory();

public:
    UBool run_phase2( char* name, char* par ); // internally, supports reporting memory leaks
    static const char* loadTestData(UErrorCode& err);
    virtual const char* getTestDataPath(UErrorCode& err);
    static const char* getSourceTestData(UErrorCode& err);

// static members
public:
    static IntlTest* gTest;
    static const char* fgDataDir;

};

void it_log( UnicodeString message );
void it_logln( UnicodeString message );
void it_logln( void );
void it_info( UnicodeString message );
void it_infoln( UnicodeString message );
void it_infoln( void );
void it_err(void);
void it_err( UnicodeString message );
void it_errln( UnicodeString message );
void it_dataerr( UnicodeString message );
void it_dataerrln( UnicodeString message );

/**
 * This is a variant of cintltst/ccolltst.c:CharsToUChars().
 * It converts a character string into a UnicodeString, with
 * unescaping \u sequences.
 */
extern UnicodeString CharsToUnicodeString(const char* chars);

/* alias for CharsToUnicodeString */
extern UnicodeString ctou(const char* chars);

#endif // _INTLTEST
