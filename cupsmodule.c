/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006, 2007  Tim Waugh <twaugh@redhat.com>
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

#include <stdarg.h>
#include <Python.h>
#include <cups/cups.h>
#include <cups/language.h>

#include "cupsmodule.h"

#include <locale.h>

#include "cupsconnection.h"
#include "cupsppd.h"

static PyObject *cups_password_callback = NULL;

//////////////////////
// Worker functions //
//////////////////////

static int
do_model_compare (const char *a, const char *b)
{
  const char *digits = "0123456789";
  char quick_a, quick_b;
  while ((quick_a = *a) != '\0' && (quick_b = *b) != '\0') {
    int end_a = strspn (a, digits);
    int end_b = strspn (b, digits);
    int min;
    int a_is_digit = 1;
    int cmp;

    if (quick_a != quick_b && !isdigit (quick_a) && !isdigit (quick_b)) {
      if (quick_a < quick_b) return -1;
      else return 1;
    }

    if (!end_a) {
      end_a = strcspn (a, digits);
      a_is_digit = 0;
    }

    if (!end_b) {
      if (a_is_digit)
	return -1;
      end_b = strcspn (b, digits);
    } else if (!a_is_digit)
      return 1;

    if (a_is_digit) {
      int n_a = atoi (a), n_b = atoi (b);
      if (n_a < n_b) cmp = -1;
      else if (n_a == n_b) cmp = 0;
      else cmp = 1;
    } else {
      min = end_a < end_b ? end_a : end_b;
      cmp = strncmp (a, b, min);
    }

    if (!cmp) {
      if (end_a != end_b)
	return end_a < end_b ? -1 : 1;

      a += end_a;
      b += end_b;
      continue;
    }

    return cmp;
  }

  if (quick_a == '\0') {
    if (*b == '\0')
      return 0;

    return -1;
  }

  return 1;
}

static const char *
do_password_callback (const char *prompt)
{
  static char *password;

  PyObject *args;
  PyObject *result;
  const char *pwval;

  args = Py_BuildValue ("(s)", prompt);
  result = PyEval_CallObject (cups_password_callback, args);
  Py_DECREF (args);
  if (result == NULL)
    return "";

  if (password) {
    free (password);
    password = NULL;
  }

  pwval = PyString_AsString (result);
  password = strdup (pwval);
  Py_DECREF (result);
  if (!password)
    return "";
  
  return password;
}

//////////////////////////
// Module-level methods //
//////////////////////////

static PyObject *
cups_modelSort (PyObject *self, PyObject *args)
{
  char *a, *b;

  if (!PyArg_ParseTuple (args, "ss", &a, &b))
    return NULL;

  return Py_BuildValue ("i", do_model_compare (a, b));
}

static PyObject *
cups_setUser (PyObject *self, PyObject *args)
{
  const char *user;

  if (!PyArg_ParseTuple (args, "s", &user))
    return NULL;

  cupsSetUser (user);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cups_setServer (PyObject *self, PyObject *args)
{
  const char *server;

  if (!PyArg_ParseTuple (args, "s", &server))
    return NULL;

  cupsSetServer (server);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cups_setPort (PyObject *self, PyObject *args)
{
  int port;

  if (!PyArg_ParseTuple (args, "i", &port))
    return NULL;

  ippSetPort (port);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cups_setEncryption (PyObject *self, PyObject *args)
{
  int e;
  if (!PyArg_ParseTuple (args, "i", &e))
    return NULL;

  cupsSetEncryption (e);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cups_getUser (PyObject *self)
{
  return PyString_FromString (cupsUser ());
}

static PyObject *
cups_getServer (PyObject *self)
{
  return PyString_FromString (cupsServer ());
}

static PyObject *
cups_getPort (PyObject *self)
{
  return Py_BuildValue ("i", ippPort ());
}

static PyObject *
cups_getEncryption (PyObject *self)
{
  return Py_BuildValue ("i", cupsEncryption ());
}

static PyObject *
cups_setPasswordCB (PyObject *self, PyObject *args)
{
  PyObject *cb;

  if (!PyArg_ParseTuple (args, "O:cups_setPasswordCB", &cb))
    return NULL;

  if (!PyCallable_Check (cb)) {
    PyErr_SetString (PyExc_TypeError, "Parameter must be callable");
    return NULL;
  }

  Py_XINCREF (cb);
  Py_XDECREF (cups_password_callback);
  cups_password_callback = cb;
  cupsSetPasswordCB (do_password_callback);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cups_require (PyObject *self, PyObject *args)
{
  const char *version = VERSION;
  const char *required;
  const char *pver, *preq;
  char *end;
  unsigned long nreq, nver;

  if (!PyArg_ParseTuple (args, "s", &required))
    return NULL;

  pver = version;
  preq = required;
  nreq = strtoul (preq, &end, 0);
  while (preq != end)
  {
    preq = end + 1;

    nver = strtoul (pver, &end, 0);
    if (pver == end)
      goto fail;
    else
      pver = end + 1;

    if (nver < nreq)
      goto fail;

    nreq = strtoul (preq, &end, 0);
  }

  return Py_None;
fail:
  PyErr_SetString (PyExc_RuntimeError, "I am version " VERSION);
  return NULL;
}

static PyMethodDef CupsMethods[] = {
  { "modelSort", cups_modelSort, METH_VARARGS,
    "Sort two model strings." },

  { "setUser", cups_setUser, METH_VARARGS,
    "Set user to connect as." },

  { "setServer", cups_setServer, METH_VARARGS,
    "Set server to connect to." },

  { "setPort", cups_setPort, METH_VARARGS,
    "Set IPP port to connect to." },

  { "setEncryption", cups_setEncryption, METH_VARARGS,
    "Set encryption policy." },

  { "getUser", (PyCFunction) cups_getUser, METH_NOARGS,
    "Get user to connect as." },

  { "getServer", (PyCFunction) cups_getServer, METH_NOARGS,
    "Get server to connect to." },

  { "getPort", (PyCFunction) cups_getPort, METH_NOARGS,
    "Get IPP port to connect to." },

  { "getEncryption", (PyCFunction) cups_getEncryption, METH_NOARGS,
    "Get encryption policy." },

  { "setPasswordCB", cups_setPasswordCB, METH_VARARGS,
    "Set user to connect as." },

  { "require", cups_require, METH_VARARGS,
    "Require pycups version." },  

  { NULL, NULL, 0, NULL }
};

void
initcups (void)
{
  PyObject *m = Py_InitModule ("cups", CupsMethods);
  PyObject *d = PyModule_GetDict (m);
  PyObject *obj;

  // Connection type
  cups_ConnectionType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_ConnectionType) < 0)
    return;

  PyModule_AddObject (m, "Connection",
		      (PyObject *)&cups_ConnectionType);

  // PPD type
  cups_PPDType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_PPDType) < 0)
    return;

  PyModule_AddObject (m, "PPD",
		      (PyObject *)&cups_PPDType);

  // Option type
  cups_OptionType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_OptionType) < 0)
    return;

  PyModule_AddObject (m, "Option",
		      (PyObject *)&cups_OptionType);

  // Group type
  cups_GroupType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_GroupType) < 0)
    return;

  PyModule_AddObject (m, "Group",
		      (PyObject *)&cups_GroupType);

  // Constraint type
  cups_ConstraintType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_ConstraintType) < 0)
    return;

  PyModule_AddObject (m, "Constraint",
		      (PyObject *)&cups_ConstraintType);

  // Attribute type
  cups_AttributeType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_AttributeType) < 0)
    return;

  PyModule_AddObject (m, "Attribute",
		      (PyObject *)&cups_AttributeType);

  // Dest type
  cups_DestType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_DestType) < 0)
    return;

  PyModule_AddObject (m, "Dest",
		      (PyObject *)&cups_DestType);

  // Constants

#define INT_CONSTANT(name)					\
  PyDict_SetItemString (d, #name, PyInt_FromLong (name))
#define STR_CONSTANT(name)					\
  PyDict_SetItemString (d, #name, PyString_FromString (name))

  // CUPS printer types
  INT_CONSTANT (CUPS_PRINTER_LOCAL);
  INT_CONSTANT (CUPS_PRINTER_CLASS);
  INT_CONSTANT (CUPS_PRINTER_REMOTE);
  INT_CONSTANT (CUPS_PRINTER_BW);
  INT_CONSTANT (CUPS_PRINTER_COLOR);
  INT_CONSTANT (CUPS_PRINTER_DUPLEX);
  INT_CONSTANT (CUPS_PRINTER_STAPLE);
  INT_CONSTANT (CUPS_PRINTER_COPIES);
  INT_CONSTANT (CUPS_PRINTER_COLLATE);
  INT_CONSTANT (CUPS_PRINTER_PUNCH);
  INT_CONSTANT (CUPS_PRINTER_COVER);
  INT_CONSTANT (CUPS_PRINTER_BIND);
  INT_CONSTANT (CUPS_PRINTER_SORT);
  INT_CONSTANT (CUPS_PRINTER_SMALL);
  INT_CONSTANT (CUPS_PRINTER_MEDIUM);
  INT_CONSTANT (CUPS_PRINTER_LARGE);
  INT_CONSTANT (CUPS_PRINTER_VARIABLE);
  INT_CONSTANT (CUPS_PRINTER_IMPLICIT);
  INT_CONSTANT (CUPS_PRINTER_DEFAULT);
  INT_CONSTANT (CUPS_PRINTER_FAX);
  INT_CONSTANT (CUPS_PRINTER_REJECTING);
  INT_CONSTANT (CUPS_PRINTER_DELETE);
  INT_CONSTANT (CUPS_PRINTER_NOT_SHARED);
  INT_CONSTANT (CUPS_PRINTER_AUTHENTICATED);
  INT_CONSTANT (CUPS_PRINTER_COMMANDS);
  INT_CONSTANT (CUPS_PRINTER_OPTIONS);

  // HTTP encryption
  INT_CONSTANT (HTTP_ENCRYPT_IF_REQUESTED);
  INT_CONSTANT (HTTP_ENCRYPT_NEVER);
  INT_CONSTANT (HTTP_ENCRYPT_REQUIRED);
  INT_CONSTANT (HTTP_ENCRYPT_ALWAYS);

  // Selected HTTP status codes
  INT_CONSTANT (HTTP_ERROR);
  INT_CONSTANT (HTTP_OK);
  INT_CONSTANT (HTTP_BAD_REQUEST);
  INT_CONSTANT (HTTP_UNAUTHORIZED);
  INT_CONSTANT (HTTP_FORBIDDEN);
  INT_CONSTANT (HTTP_NOT_FOUND);
  INT_CONSTANT (HTTP_REQUEST_TIMEOUT);
  INT_CONSTANT (HTTP_UPGRADE_REQUIRED);
  INT_CONSTANT (HTTP_SERVER_ERROR);

  // PPD UI enum
  INT_CONSTANT (PPD_UI_BOOLEAN);
  INT_CONSTANT (PPD_UI_PICKONE);
  INT_CONSTANT (PPD_UI_PICKMANY);

  // Job states
  INT_CONSTANT (IPP_JOB_PENDING);
  INT_CONSTANT (IPP_JOB_HELD);
  INT_CONSTANT (IPP_JOB_PROCESSING);
  INT_CONSTANT (IPP_JOB_STOPPED);
  INT_CONSTANT (IPP_JOB_CANCELED);
  INT_CONSTANT (IPP_JOB_ABORTED);
  INT_CONSTANT (IPP_JOB_COMPLETED);

  // Printer states
  INT_CONSTANT (IPP_PRINTER_IDLE);
  INT_CONSTANT (IPP_PRINTER_PROCESSING);
  INT_CONSTANT (IPP_PRINTER_STOPPED);

  // IPP finishings
  INT_CONSTANT (IPP_FINISHINGS_NONE);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE);
  INT_CONSTANT (IPP_FINISHINGS_PUNCH);
  INT_CONSTANT (IPP_FINISHINGS_COVER);
  INT_CONSTANT (IPP_FINISHINGS_BIND);
  INT_CONSTANT (IPP_FINISHINGS_SADDLE_STITCH);
  INT_CONSTANT (IPP_FINISHINGS_EDGE_STITCH);
  INT_CONSTANT (IPP_FINISHINGS_FOLD);
  INT_CONSTANT (IPP_FINISHINGS_TRIM);
  INT_CONSTANT (IPP_FINISHINGS_BALE);
  INT_CONSTANT (IPP_FINISHINGS_BOOKLET_MAKER);
  INT_CONSTANT (IPP_FINISHINGS_JOB_OFFSET);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_TOP_LEFT);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_BOTTOM_LEFT);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_TOP_RIGHT);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_BOTTOM_RIGHT);
  INT_CONSTANT (IPP_FINISHINGS_EDGE_STITCH_LEFT);
  INT_CONSTANT (IPP_FINISHINGS_EDGE_STITCH_TOP);
  INT_CONSTANT (IPP_FINISHINGS_EDGE_STITCH_RIGHT);
  INT_CONSTANT (IPP_FINISHINGS_EDGE_STITCH_BOTTOM);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_DUAL_LEFT);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_DUAL_TOP);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_DUAL_RIGHT);
  INT_CONSTANT (IPP_FINISHINGS_STAPLE_DUAL_BOTTOM);
  INT_CONSTANT (IPP_FINISHINGS_BIND_LEFT);
  INT_CONSTANT (IPP_FINISHINGS_BIND_TOP);
  INT_CONSTANT (IPP_FINISHINGS_BIND_RIGHT);
  INT_CONSTANT (IPP_FINISHINGS_BIND_BOTTOM);

  // IPP errors
  INT_CONSTANT (IPP_OK);
  INT_CONSTANT (IPP_OK_SUBST);
  INT_CONSTANT (IPP_OK_CONFLICT);
  INT_CONSTANT (IPP_OK_IGNORED_SUBSCRIPTIONS);
  INT_CONSTANT (IPP_OK_IGNORED_NOTIFICATIONS);
  INT_CONSTANT (IPP_OK_TOO_MANY_EVENTS);
  INT_CONSTANT (IPP_OK_BUT_CANCEL_SUBSCRIPTION);
  INT_CONSTANT (IPP_OK_EVENTS_COMPLETE);
  INT_CONSTANT (IPP_REDIRECTION_OTHER_SITE);
  INT_CONSTANT (IPP_BAD_REQUEST);
  INT_CONSTANT (IPP_FORBIDDEN);
  INT_CONSTANT (IPP_NOT_AUTHENTICATED);
  INT_CONSTANT (IPP_NOT_AUTHORIZED);
  INT_CONSTANT (IPP_NOT_POSSIBLE);
  INT_CONSTANT (IPP_TIMEOUT);
  INT_CONSTANT (IPP_NOT_FOUND);
  INT_CONSTANT (IPP_GONE);
  INT_CONSTANT (IPP_REQUEST_ENTITY);
  INT_CONSTANT (IPP_REQUEST_VALUE);
  INT_CONSTANT (IPP_DOCUMENT_FORMAT);
  INT_CONSTANT (IPP_ATTRIBUTES);
  INT_CONSTANT (IPP_URI_SCHEME);
  INT_CONSTANT (IPP_CHARSET);
  INT_CONSTANT (IPP_CONFLICT);
  INT_CONSTANT (IPP_COMPRESSION_NOT_SUPPORTED);
  INT_CONSTANT (IPP_COMPRESSION_ERROR);
  INT_CONSTANT (IPP_DOCUMENT_FORMAT_ERROR);
  INT_CONSTANT (IPP_DOCUMENT_ACCESS_ERROR);
  INT_CONSTANT (IPP_ATTRIBUTES_NOT_SETTABLE);
  INT_CONSTANT (IPP_IGNORED_ALL_SUBSCRIPTIONS);
  INT_CONSTANT (IPP_TOO_MANY_SUBSCRIPTIONS);
  INT_CONSTANT (IPP_IGNORED_ALL_NOTIFICATIONS);
  INT_CONSTANT (IPP_PRINT_SUPPORT_FILE_NOT_FOUND);
  INT_CONSTANT (IPP_INTERNAL_ERROR);
  INT_CONSTANT (IPP_OPERATION_NOT_SUPPORTED);
  INT_CONSTANT (IPP_SERVICE_UNAVAILABLE);
  INT_CONSTANT (IPP_VERSION_NOT_SUPPORTED);
  INT_CONSTANT (IPP_DEVICE_ERROR);
  INT_CONSTANT (IPP_TEMPORARY_ERROR);
  INT_CONSTANT (IPP_NOT_ACCEPTING);
  INT_CONSTANT (IPP_PRINTER_BUSY);
  INT_CONSTANT (IPP_ERROR_JOB_CANCELLED);
  INT_CONSTANT (IPP_MULTIPLE_JOBS_NOT_SUPPORTED);
  INT_CONSTANT (IPP_PRINTER_IS_DEACTIVATED);

  // Limits
  INT_CONSTANT (IPP_MAX_NAME);

  // Admin Util constants
  STR_CONSTANT (CUPS_SERVER_DEBUG_LOGGING);
  STR_CONSTANT (CUPS_SERVER_REMOTE_ADMIN);
  STR_CONSTANT (CUPS_SERVER_REMOTE_PRINTERS);
  STR_CONSTANT (CUPS_SERVER_SHARE_PRINTERS);
  STR_CONSTANT (CUPS_SERVER_USER_CANCEL_ANY);
#ifdef HAVE_CUPS_1_3
  STR_CONSTANT (CUPS_SERVER_REMOTE_ANY);
#endif /* CUPS 1.3 */

  // Exceptions
  obj = PyDict_New ();
  PyDict_SetItemString (obj, "__doc__", PyString_FromString(
    "This exception is raised when an HTTP problem has occurred.  It \n"
    "provides an integer HTTP status code.\n\n"
    "Use it like this::\n"
    "  try:\n"
    "    ...\n"
    "  except cups.HTTPError (status):\n"
    "    print 'HTTP status is %d' % status\n"));
  HTTPError = PyErr_NewException ("cups.HTTPError", NULL, obj);
  Py_DECREF (obj);
  if (HTTPError == NULL)
    return;
  Py_INCREF (HTTPError);
  PyModule_AddObject (m, "HTTPError", HTTPError);

  obj = PyDict_New ();
  PyDict_SetItemString (obj, "__doc__", PyString_FromString(
    "This exception is raised when an IPP error has occurred.  It \n"
    "provides an integer IPP status code, and a human-readable string \n"
    "describing the error.\n\n"
    "Use it like this::\n"
    "  try:\n"
    "    ...\n"
    "  except cups.IPPError (status, description):\n"
    "    print 'IPP status is %d' % status\n"
    "    print 'Meaning:', description\n"));
  IPPError = PyErr_NewException ("cups.IPPError", NULL, obj);
  Py_DECREF (obj);
  if (IPPError == NULL)
    return;
  Py_INCREF (IPPError);
  PyModule_AddObject (m, "IPPError", IPPError);
}

///////////////
// Debugging //
///////////////

#define ENVAR "PYCUPS_DEBUG"
static int debugging_enabled = -1;

void
debugprintf (const char *fmt, ...)
{
  if (!debugging_enabled)
    return;

  if (debugging_enabled == -1)
    {
      if (!getenv (ENVAR))
	{
	  debugging_enabled = 0;
	  return;
	}

      debugging_enabled = 1;
    }
  
  {
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
  }
}
