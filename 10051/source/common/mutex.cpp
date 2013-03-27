/*
*******************************************************************************
*
*   Copyright (C) 2008-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  mutex.cpp
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*/

#include "unicode/utypes.h"
#include "mutex.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

void *SimpleSingleton::getInstance(InstantiatorFn *instantiator, const void *context,
                                   UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) {
        return NULL;
    }
    if (std::atomic_load_explicit(&fInitOnce.fState, std::memory_order_acquire) == 2) {
        return fInstance;
    }
    if (u_initImplPreInit(&fInitOnce)) {
        fInstance = instantiator(context, errorCode);
        u_initImplPostInit(&fInitOnce, fInstance != NULL);
    }
    return fInstance;
}


/*
 * Three states:
 *
 * Initial state: Instance creation not attempted yet.
 * fInstance=NULL && U_SUCCESS(fErrorCode)
 *
 * Instance creation run & succeeded:
 * fInstance!=NULL && U_SUCCESS(fErrorCode)
 *
 * Instance creation & failed:
 * fInstance=NULL && U_FAILURE(fErrorCode)
 * We will not attempt again to create the instance.
 *
 * The instantiator function will be called only once, whether it succeeds or fails.
 * The controlling state is maintained by the UInitOnce object, not by
 *    fInstance and fErrorCode.
 * The values of fInstance and fErrorCode must only be set between pre and post init(),
 *    where they are in a controlled memory environment.
 */
void *TriStateSingleton::getInstance(InstantiatorFn *instantiator, const void *context,
                                     UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) {
        return NULL;
    }
    if (std::atomic_load_explicit(&fInitOnce.fState, std::memory_order_acquire) == 2) {
        errorCode = fErrorCode;
        return fInstance;
    }
    if (u_initImplPreInit(&fInitOnce)) {
        errorCode = U_ZERO_ERROR;
        fInstance = instantiator(context, errorCode);
        fErrorCode = errorCode;
        u_initImplPostInit(&fInitOnce, TRUE);
    }
    return fInstance;
}






#if 0
    // TODO: With atomicops.h: void *instance = (void*)Acquire_Load(&fInstance);
    //       and remove UMTX_ACQUIRE_BARRIER below.
    void *instance=ANNOTATE_UNPROTECTED_READ(fInstance);
    UMTX_ACQUIRE_BARRIER;
    ANNOTATE_HAPPENS_AFTER(&fInstance);
    if(instance!=NULL) {
        // instance was created
        return instance;
    }

    // The read access to fErrorCode is thread-unsafe, but harmless because
    // at worst multiple threads race to each create a new instance,
    // and all losing threads delete their duplicates.
    UErrorCode localErrorCode=ANNOTATE_UNPROTECTED_READ(fErrorCode);
    if(U_FAILURE(localErrorCode)) {
        // instance creation failed
        errorCode=localErrorCode;
        return NULL;
    }

    // First attempt to create the instance.
    // If a race occurs, then the losing thread will assign its new instance
    // to the "duplicate" parameter, and the caller deletes it.
    instance=instantiator(context, errorCode);
    UMTX_RELEASE_BARRIER;  // Release-barrier before fInstance=instance;
    Mutex mutex;
    if(fInstance==NULL && U_SUCCESS(errorCode)) {
        // instance creation newly succeeded
        U_ASSERT(instance!=NULL);
        ANNOTATE_HAPPENS_BEFORE(&fInstance);
        // TODO: With atomicops.h: Release_Store(&fInstance, (AtomicWord)instance);
        //       and remove UMTX_RELEASE_BARRIER above.
        fInstance=instance;
        // Set fErrorCode on the off-chance that a previous instance creation failed.
        fErrorCode=errorCode;
        // Completed state transition: initial->succeeded, or failed->succeeded.
    } else {
        // Record a duplicate if we lost the race, or
        // if we got an instance but its creation failed anyway.
        duplicate=instance;
        if(fInstance==NULL && U_SUCCESS(fErrorCode) && U_FAILURE(errorCode)) {
            // instance creation newly failed
            fErrorCode=errorCode;
            // Completed state transition: initial->failed.
        }
    }
    return fInstance;
}
#endif

void TriStateSingleton::reset() {
    fInstance=NULL;
    fErrorCode=U_ZERO_ERROR;
    fInitOnce.reset();
}

#if UCONFIG_NO_SERVICE

/* If UCONFIG_NO_SERVICE, then there is no invocation of Mutex elsewhere in
   common, so add one here to force an export */
static Mutex *aMutex = 0;

/* UCONFIG_NO_SERVICE */
#endif

U_NAMESPACE_END
