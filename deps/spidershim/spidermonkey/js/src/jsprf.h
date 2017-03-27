/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsprf_h
#define jsprf_h

#include "mozilla/Printf.h"

#include <stdarg.h>

#include "jstypes.h"

/* Wrappers for mozilla::Smprintf and friends that are used throughout
   JS.  */

extern JS_PUBLIC_API(char*) JS_smprintf(const char* fmt, ...)
    MOZ_FORMAT_PRINTF(1, 2);

extern JS_PUBLIC_API(void) JS_smprintf_free(char* mem);

extern JS_PUBLIC_API(char*) JS_sprintf_append(char* last, const char* fmt, ...)
     MOZ_FORMAT_PRINTF(2, 3);

extern JS_PUBLIC_API(char*) JS_vsmprintf(const char* fmt, va_list ap);
extern JS_PUBLIC_API(char*) JS_vsprintf_append(char* last, const char* fmt, va_list ap);

#endif /* jsprf_h */
