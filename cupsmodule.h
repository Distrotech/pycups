/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2006, 2007, 2008, 2009, 2010  Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef HAVE_CUPSMODULE_H
#define HAVE_CUPSMODULE_H

#include <cups/cups.h>
#include <cups/language.h>
#include <cups/adminutil.h>

/* GCC attributes */
#if !defined(__GNUC__) || __GNUC__ < 2 || \
    (__GNUC__ == 2 && __GNUC_MINOR__ < 5) || __STRICT_ANSI__
# define FORMAT(x)
#else /* GNU C: */
# define FORMAT(x) __attribute__ ((__format__ x))
#endif

extern void debugprintf (const char *fmt, ...) FORMAT ((__printf__, 1, 2));

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 1)
#define HAVE_CUPS_1_2 1
#endif

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 2)
#define HAVE_CUPS_1_3 1
#else
#define cupsAdminGetServerSettings _cupsAdminGetServerSettings
#define cupsAdminSetServerSettings _cupsAdminSetServerSettings
#endif

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 3)
#define HAVE_CUPS_1_4 1
#endif

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

#ifndef HAVE_CUPS_1_2
#error pycups requires CUPS 1.2.x
#endif

struct TLS
{
    PyObject *cups_password_callback;
#ifdef HAVE_CUPS_1_4
    PyObject *cups_password_callback_context;
#else /* !HAVE_CUPS_1_4 */
    void *g_current_connection;
#endif /* !HAVE_CUPS_1_4 */
};

extern struct TLS *get_TLS (void);

#ifndef HAVE_CUPS_1_6
int ippGetBoolean(ipp_attribute_t *attr, int element);
int ippGetCount(ipp_attribute_t *attr);
ipp_tag_t ippGetGroupTag(ipp_attribute_t *attr);
int ippGetInteger(ipp_attribute_t *attr, int element);
const char * ippGetName(ipp_attribute_t *attr);
ipp_op_t ippGetOperation(ipp_t *ipp);
int ippGetRange(ipp_attribute_t *attr, int element, int *uppervalue);
int ippGetResolution(ipp_attribute_t *attr, int element,
                     int *yres, ipp_res_t *units);
ipp_status_t ippGetStatusCode(ipp_t *ipp);
const char * ippGetString(ipp_attribute_t *attr, int element,
                          const char **language);
ipp_tag_t ippGetValueTag(ipp_attribute_t *attr);
ipp_attribute_t	* ippFirstAttribute(ipp_t *ipp);
ipp_attribute_t * ippNextAttribute(ipp_t *ipp);
int ippSetInteger(ipp_t *ipp, ipp_attribute_t **attr,
                  int element, int intvalue);
int ippSetOperation(ipp_t *ipp, ipp_op_t op);
int ippSetString(ipp_t *ipp, ipp_attribute_t **attr,
                 int element, const char *strvalue);
#endif


#endif /* HAVE_CUPSMODULE_H */
