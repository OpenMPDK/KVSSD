/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef LIBCOUCHSTORE_VISIBILITY_H
#define LIBCOUCHSTORE_VISIBILITY_H

#if defined(LIBCOUCHSTORE_INTERNAL)

#if defined (__SUNPRO_C) && (__SUNPRO_C >= 0x550)
#define LIBCOUCHSTORE_API __global
#elif defined __GNUC__
#define LIBCOUCHSTORE_API __attribute__ ((visibility("default")))
#elif defined(_MSC_VER)
#define LIBCOUCHSTORE_API extern __declspec(dllexport)
#else
#define LIBCOUCHSTORE_API
#endif

#else

#if defined(_MSC_VER) && !defined(LIBCOUCHSTORE_NO_VISIBILITY)
#define LIBCOUCHSTORE_API extern __declspec(dllimport)
#else
#define LIBCOUCHSTORE_API
#endif

#endif


#if defined(TESTAPP)
#define STATIC
#else
#define STATIC static
#endif


#endif
