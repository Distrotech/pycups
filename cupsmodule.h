/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2006  Tim Waugh <twaugh@redhat.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef HAVE_CUPSMODULE_H
#define HAVE_CUPSMODULE_H

#include <cups/cups.h>
#include <cups/language.h>
#include <cups/adminutil.h>

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 1)
#define HAVE_CUPS_1_2 1
#endif

#ifndef HAVE_CUPS_1_2
#warning Compiling against CUPS 1.1.x
#define CUPS_ADD_MODIFY_PRINTER CUPS_ADD_PRINTER
#define CUPS_ADD_MODIFY_CLASS CUPS_ADD_CLASS

#define CUPS_PRINTER_DELETE 0x100000
#define CUPS_PRINTER_NOT_SHARED 0x200000
#define CUPS_PRINTER_AUTHENTICATED 0x400000
#define CUPS_PRINTER_COMMANDS 0x800000

#define IPP_OK_EVENTS_COMPLETE 7

static ipp_t *
ippNewRequest (ipp_op_t op)
{
  ipp_t *request = ippNew ();
  cups_lang_t *language = cupsLangDefault ();
  request->request.op.operation_id = op;
  request->request.op.request_id = 1;
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  return request;
}
#endif

#endif /* HAVE_CUPSMODULE_H */
