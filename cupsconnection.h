/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006, 2008, 2009  Tim Waugh <twaugh@redhat.com>
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

#ifndef HAVE_CUPSCONNECTION_H
#define HAVE_CUPSCONNECTION_H

#include <Python.h>
#include <cups/http.h>

extern PyMethodDef Connection_methods[];
extern PyTypeObject cups_ConnectionType;
extern PyTypeObject cups_DestType;

extern PyObject *HTTPError;
extern PyObject *IPPError;

void Connection_begin_allow_threads (void *connection);
void Connection_end_allow_threads (void *connection);

const char *password_callback_newstyle (const char *prompt,
					http_t *http,
					const char *method,
					const char *resource,
					void *user_data);
const char *password_callback_oldstyle (const char *prompt,
					http_t *http,
					const char *method,
					const char *resource,
					void *user_data);
#endif /* HAVE_CUPSCONNECTION_H */
