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

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 1)
#define HAVE_CUPS_1_2 1
#endif

#ifndef HAVE_CUPS_1_2
#warning Compiling against CUPS 1.1.x
#define CUPS_ADD_MODIFY_PRINTER CUPS_ADD_PRINTER
#define CUPS_ADD_MODIFY_CLASS CUPS_ADD_CLASS
#endif

#endif /* HAVE_CUPSMODULE_H */
