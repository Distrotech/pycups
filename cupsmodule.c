/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006, 2007, 2008, 2009, 2010, 2011  Red Hat, Inc
 * Author: Tim Waugh <twaugh@redhat.com>
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
#include <pthread.h>
#include <wchar.h>
#include <wctype.h>

#include "cupsconnection.h"
#include "cupsppd.h"
#include "cupsipp.h"

static pthread_key_t tls_key = -1;
static pthread_once_t tls_key_once = PTHREAD_ONCE_INIT;

//////////////////////
// Worker functions //
//////////////////////

static void
destroy_TLS (void *value)
{
  struct TLS *tls = (struct TLS *) value;
  Py_XDECREF (tls->cups_password_callback);

#if HAVE_CUPS_1_4
  Py_XDECREF (tls->cups_password_callback_context);
#endif /* HAVE_CUPS_1_4 */

  free (value);
}

static void
init_TLS (void)
{
  pthread_key_create (&tls_key, destroy_TLS);
}

struct TLS *
get_TLS (void)
{
  struct TLS *tls;
  pthread_once (&tls_key_once, init_TLS);
  tls = (struct TLS *) pthread_getspecific (tls_key);
  if (tls == NULL)
    {
      tls = calloc (1, sizeof (struct TLS));
      pthread_setspecific (tls_key, tls);
    }

  return tls;
}

static int
do_model_compare (const wchar_t *a, const wchar_t *b)
{
  const wchar_t *digits = L"0123456789";
  wchar_t quick_a, quick_b;
  while ((quick_a = *a) != L'\0' && (quick_b = *b) != L'\0') {
    int end_a = wcsspn (a, digits);
    int end_b = wcsspn (b, digits);
    int min;
    int a_is_digit = 1;
    int cmp;

    if (quick_a != quick_b && !iswdigit (quick_a) && !iswdigit (quick_b)) {
      if (quick_a < quick_b) return -1;
      else return 1;
    }

    if (!end_a) {
      end_a = wcscspn (a, digits);
      a_is_digit = 0;
    }

    if (!end_b) {
      if (a_is_digit)
	return -1;
      end_b = wcscspn (b, digits);
    } else if (!a_is_digit)
      return 1;

    if (a_is_digit) {
      unsigned long n_a, n_b;
      n_a = wcstoul (a, NULL, 10);
      n_b = wcstoul (b, NULL, 10);
      if (n_a < n_b) cmp = -1;
      else if (n_a == n_b) cmp = 0;
      else cmp = 1;
    } else {
      min = end_a < end_b ? end_a : end_b;
      cmp = wcsncmp (a, b, min);
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

  if (quick_a == L'\0') {
    if (*b == L'\0')
      return 0;

    return -1;
  }

  return 1;
}

#ifndef HAVE_CUPS_1_4
static const char *
do_password_callback (const char *prompt)
{
  struct TLS *tls = get_TLS ();
  static char *password;

  PyObject *args;
  PyObject *result;
  const char *pwval;

  debugprintf ("-> do_password_callback\n");
  Connection_end_allow_threads (tls->g_current_connection);
  args = Py_BuildValue ("(s)", prompt);
  result = PyEval_CallObject (tls->cups_password_callback, args);
  Py_DECREF (args);
  if (result == NULL)
  {
    debugprintf ("<- do_password_callback (exception)\n");
    Connection_begin_allow_threads (tls->g_current_connection);
    return NULL;
  }

  if (password) {
    free (password);
    password = NULL;
  }

  if (result == Py_None)
    password = NULL;
  else
  {
    pwval = PyString_AsString (result);
    password = strdup (pwval);
  }

  Py_DECREF (result);
  if (!password || !*password)
  {
    debugprintf ("<- do_password_callback (empty/null)\n");
    Connection_begin_allow_threads (tls->g_current_connection);
    return NULL;
  }

  Connection_begin_allow_threads (tls->g_current_connection);
  debugprintf ("<- do_password_callback\n");
  return password;
}
#endif /* !HAVE_CUPS_1_4 */

//////////////////////////
// Module-level methods //
//////////////////////////

static PyObject *
cups_modelSort (PyObject *self, PyObject *args)
{
  PyObject *Oa, *Ob, *a, *b;
  int len_a, len_b;
  size_t size_a, size_b;
  wchar_t *wca, *wcb;

  if (!PyArg_ParseTuple (args, "OO", &Oa, &Ob))
    return NULL;

  a = PyUnicode_FromObject (Oa);
  b = PyUnicode_FromObject (Ob);
  if (a == NULL || b == NULL || !PyUnicode_Check (a) || !PyUnicode_Check (b)) {
    if (a) {
      Py_DECREF (a);
    }
    if (b) {
      Py_DECREF (b);
    }

    PyErr_SetString (PyExc_TypeError, "Unable to convert to Unicode");
    return NULL;
  }

  len_a = 1 + PyUnicode_GetSize (a);
  size_a = len_a * sizeof (wchar_t);
  if ((size_a / sizeof (wchar_t)) != len_a) {
    Py_DECREF (a);
    Py_DECREF (b);
    PyErr_SetString (PyExc_RuntimeError, "String too long");
    return NULL;
  }

  len_b = 1 + PyUnicode_GetSize (b);
  size_b = len_b * sizeof (wchar_t);
  if ((size_b / sizeof (wchar_t)) != len_b) {
    Py_DECREF (a);
    Py_DECREF (b);
    PyErr_SetString (PyExc_RuntimeError, "String too long");
    return NULL;
  }

  wca = malloc (size_a);
  wcb = malloc (size_b);
  if (wca == NULL || wcb == NULL) {
    Py_DECREF (a);
    Py_DECREF (b);
    free (wca);
    free (wcb);
    PyErr_SetString (PyExc_RuntimeError, "Insufficient memory");
    return NULL;
  }

  PyUnicode_AsWideChar ((PyUnicodeObject *) a, wca, size_a);
  PyUnicode_AsWideChar ((PyUnicodeObject *) b, wcb, size_b);
  Py_DECREF (a);
  Py_DECREF (b);
  return Py_BuildValue ("i", do_model_compare (wca, wcb));
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
  struct TLS *tls = get_TLS ();
  PyObject *cb;

  if (!PyArg_ParseTuple (args, "O:cups_setPasswordCB", &cb))
    return NULL;

  if (!PyCallable_Check (cb)) {
    PyErr_SetString (PyExc_TypeError, "Parameter must be callable");
    return NULL;
  }

  debugprintf ("-> cups_setPasswordCB\n");
#ifdef HAVE_CUPS_1_4
  Py_XDECREF (tls->cups_password_callback_context);
  tls->cups_password_callback_context = NULL;
#endif /* HAVE_CUPS_1_4 */

  Py_XINCREF (cb);
  Py_XDECREF (tls->cups_password_callback);
  tls->cups_password_callback = cb;

#ifdef HAVE_CUPS_1_4
  cupsSetPasswordCB2 (password_callback_oldstyle, NULL);
#else
  cupsSetPasswordCB (do_password_callback);
#endif

  debugprintf ("<- cups_setPasswordCB\n");
  Py_INCREF (Py_None);
  return Py_None;
}

#ifdef HAVE_CUPS_1_4
static PyObject *
cups_setPasswordCB2 (PyObject *self, PyObject *args)
{
  struct TLS *tls = get_TLS ();
  PyObject *cb;
  PyObject *cb_context = NULL;

  if (!PyArg_ParseTuple (args, "O|O", &cb, &cb_context))
    return NULL;

  if (cb == Py_None && cb_context != NULL) {
    PyErr_SetString (PyExc_TypeError, "Default callback takes no context");
    return NULL;
  }
  else if (cb != Py_None && !PyCallable_Check (cb)) {
    PyErr_SetString (PyExc_TypeError, "Parameter must be callable");
    return NULL;
  }

  debugprintf ("-> cups_setPasswordCB2\n");

  Py_XINCREF (cb_context);
  Py_XDECREF (tls->cups_password_callback_context);
  tls->cups_password_callback_context = cb_context;

  if (cb == Py_None)
  {
    Py_XDECREF (tls->cups_password_callback);
    tls->cups_password_callback = NULL;
    cupsSetPasswordCB2 (NULL, NULL);
  }
  else
  {
    Py_XINCREF (cb);
    Py_XDECREF (tls->cups_password_callback);
    tls->cups_password_callback = cb;
    cupsSetPasswordCB2 (password_callback_newstyle, cb_context);
  }

  debugprintf ("<- cups_setPasswordCB2\n");
  Py_INCREF (Py_None);
  return Py_None;
}
#endif /* HAVE_CUPS_1_4 */

static PyObject *
cups_ppdSetConformance (PyObject *self, PyObject *args)
{
  int level;
  if (!PyArg_ParseTuple (args, "i", &level))
    return NULL;

  ppdSetConformance (level);
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
    preq = end;
    if (*preq == '.')
      preq++;

    nver = strtoul (pver, &end, 0);
    if (pver == end)
      goto fail;
    else {
      pver = end;
      if (*pver == '.')
	pver++;
    }

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
    "modelSort(s1,s2) -> integer\n\n"
    "Sort two model strings.\n\n"
    "@type s1: string\n"
    "@param s1: first string\n"
    "@type s2: string\n"
    "@param s2: second string\n"
    "@return: strcmp-style comparision result"},

  { "setUser", cups_setUser, METH_VARARGS,
    "setUser(user) -> None\n\n"
    "Set user to connect as.\n\n"
    "@type user: string\n"
    "@param user: username"},

  { "setServer", cups_setServer, METH_VARARGS,
    "setServer(server) -> None\n\n"
    "Set server to connect to.\n\n"
    "@type server: string\n"
    "@param server: server hostname" },

  { "setPort", cups_setPort, METH_VARARGS,
    "setPort(port) -> None\n\n"
    "Set IPP port to connect to.\n\n"
    "@type port: integer\n"
    "@param port: IPP port" },

  { "setEncryption", cups_setEncryption, METH_VARARGS,
    "setEncryption(policy) -> None\n\n"
    "Set encryption policy.\n\n"
    "@type policy: integer\n"
    "@param policy: L{HTTP_ENCRYPT_ALWAYS}, L{HTTP_ENCRYPT_IF_REQUESTED}, \n"
    "L{HTTP_ENCRYPT_NEVER}, or L{HTTP_ENCRYPT_REQUIRED}" },

  { "getUser", (PyCFunction) cups_getUser, METH_NOARGS,
    "getUser() -> string\n\n"
    "@return: user to connect as." },

  { "getServer", (PyCFunction) cups_getServer, METH_NOARGS,
    "getServer() -> string\n\n"
    "@return: server to connect to." },

  { "getPort", (PyCFunction) cups_getPort, METH_NOARGS,
    "getPort() -> integer\n\n"
    "@return: IPP port to connect to." },

  { "getEncryption", (PyCFunction) cups_getEncryption, METH_NOARGS,
    "getEncryption() -> integer\n\n"
    "Get encryption policy.\n"
    "@see: L{setEncryption}" },

  { "setPasswordCB", cups_setPasswordCB, METH_VARARGS,
    "setPasswordCB(fn) -> None\n\n"
    "Set password callback function.  This Python function will be called \n"
    "when a password is required.  It must take one string parameter \n"
    "(the password prompt) and it must return a string (the password), or \n"
    "None to abort the operation.\n\n"
    "@type fn: callable object\n"
    "@param fn: callback function" },

#ifdef HAVE_CUPS_1_4
  { "setPasswordCB2", cups_setPasswordCB2, METH_VARARGS,
    "setPasswordCB2(fn, context=None) -> None\n\n"
    "Set password callback function.  This Python function will be called \n"
    "when a password is required.  It must take parameters of type string \n"
    "(the password prompt), instance (the cups.Connection), string (the \n"
    "HTTP method), string (the HTTP resource) and, optionally, the user-\n"
    "supplied context.  It must return a string (the password), or None \n"
    "to abort the operation.\n\n"
    "@type fn: callable object, or None for default handler\n"
    "@param fn: callback function" },
#endif /* HAVE_CUPS_1_4 */

  { "ppdSetConformance", cups_ppdSetConformance, METH_VARARGS,
    "ppdSetConformance(level) -> None\n\n"
    "Set PPD conformance level.\n\n"
    "@type level: integer\n"
    "@param level: PPD_CONFORM_RELAXED or PPD_CONFORM_STRICT" },

  { "require", cups_require, METH_VARARGS,
    "require(version) -> None\n\n"
    "Require pycups version.\n\n"
    "@type version: string\n"
    "@param version: minimum pycups version required\n"
    "@raise RuntimeError: requirement not met" },  

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

  // IPPRequest type
  cups_IPPRequestType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_IPPRequestType) < 0)
      return;

  PyModule_AddObject (m, "IPPRequest",
		      (PyObject *)&cups_IPPRequestType);

  // IPPAttribute type
  cups_IPPAttributeType.tp_new = PyType_GenericNew;
  if (PyType_Ready (&cups_IPPAttributeType) < 0)
      return;

  PyModule_AddObject (m, "IPPAttribute",
		      (PyObject *)&cups_IPPAttributeType);

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
#if CUPS_VERSION_MAJOR > 1 || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 3)
  INT_CONSTANT (CUPS_PRINTER_DISCOVERED);
#endif /* CUPS 1.3 */

  // HTTP encryption
  INT_CONSTANT (HTTP_ENCRYPT_IF_REQUESTED);
  INT_CONSTANT (HTTP_ENCRYPT_NEVER);
  INT_CONSTANT (HTTP_ENCRYPT_REQUIRED);
  INT_CONSTANT (HTTP_ENCRYPT_ALWAYS);

  // Selected HTTP status codes
  INT_CONSTANT (HTTP_ERROR);
  INT_CONSTANT (HTTP_OK);
  INT_CONSTANT (HTTP_NOT_MODIFIED);
  INT_CONSTANT (HTTP_BAD_REQUEST);
  INT_CONSTANT (HTTP_UNAUTHORIZED);
  INT_CONSTANT (HTTP_FORBIDDEN);
  INT_CONSTANT (HTTP_NOT_FOUND);
  INT_CONSTANT (HTTP_REQUEST_TIMEOUT);
  INT_CONSTANT (HTTP_UPGRADE_REQUIRED);
  INT_CONSTANT (HTTP_SERVER_ERROR);
  INT_CONSTANT (HTTP_NOT_IMPLEMENTED);
  INT_CONSTANT (HTTP_BAD_GATEWAY);
  INT_CONSTANT (HTTP_SERVICE_UNAVAILABLE);
  INT_CONSTANT (HTTP_GATEWAY_TIMEOUT);
  INT_CONSTANT (HTTP_NOT_SUPPORTED);
  INT_CONSTANT (HTTP_AUTHORIZATION_CANCELED);
#if CUPS_VERSION_MAJOR > 1 || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 5)
  INT_CONSTANT (HTTP_PKI_ERROR);
#endif /* CUPS 1.5 */

  // PPD UI enum
  INT_CONSTANT (PPD_UI_BOOLEAN);
  INT_CONSTANT (PPD_UI_PICKONE);
  INT_CONSTANT (PPD_UI_PICKMANY);

  // PPD Order dependency enum

  INT_CONSTANT (PPD_ORDER_ANY);
  INT_CONSTANT (PPD_ORDER_DOCUMENT);
  INT_CONSTANT (PPD_ORDER_EXIT);
  INT_CONSTANT (PPD_ORDER_JCL);
  INT_CONSTANT (PPD_ORDER_PAGE);
  INT_CONSTANT (PPD_ORDER_PROLOG);

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

  // IPP resolution units
  INT_CONSTANT (IPP_RES_PER_CM);
  INT_CONSTANT (IPP_RES_PER_INCH);

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

  // IPP orientations
  INT_CONSTANT (IPP_PORTRAIT);
  INT_CONSTANT (IPP_LANDSCAPE);
  INT_CONSTANT (IPP_REVERSE_PORTRAIT);
  INT_CONSTANT (IPP_REVERSE_LANDSCAPE);

  // IPP qualities
  INT_CONSTANT (IPP_QUALITY_DRAFT);
  INT_CONSTANT (IPP_QUALITY_NORMAL);
  INT_CONSTANT (IPP_QUALITY_HIGH);

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
#if CUPS_VERSION_MAJOR > 1 || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 5)
  INT_CONSTANT (IPP_AUTHENTICATION_CANCELED);
  INT_CONSTANT (IPP_PKI_ERROR);
#endif /* CUPS 1.5 */

  // IPP states
  INT_CONSTANT (IPP_ERROR);
  INT_CONSTANT (IPP_IDLE);
  INT_CONSTANT (IPP_HEADER);
  INT_CONSTANT (IPP_ATTRIBUTE);
  INT_CONSTANT (IPP_DATA);

  // IPP attribute tags
  INT_CONSTANT (IPP_TAG_ZERO);
  INT_CONSTANT (IPP_TAG_OPERATION);
  INT_CONSTANT (IPP_TAG_JOB);
  INT_CONSTANT (IPP_TAG_PRINTER);
  INT_CONSTANT (IPP_TAG_INTEGER);
  INT_CONSTANT (IPP_TAG_BOOLEAN);
  INT_CONSTANT (IPP_TAG_ENUM);
  INT_CONSTANT (IPP_TAG_STRING);
  INT_CONSTANT (IPP_TAG_RANGE);
  INT_CONSTANT (IPP_TAG_TEXT);
  INT_CONSTANT (IPP_TAG_NAME);
  INT_CONSTANT (IPP_TAG_KEYWORD);
  INT_CONSTANT (IPP_TAG_URI);
  INT_CONSTANT (IPP_TAG_CHARSET);
  INT_CONSTANT (IPP_TAG_LANGUAGE);
  INT_CONSTANT (IPP_TAG_MIMETYPE);

  // Limits
  INT_CONSTANT (IPP_MAX_NAME);

  // PPD conformance levels
  INT_CONSTANT (PPD_CONFORM_RELAXED);
  INT_CONSTANT (PPD_CONFORM_STRICT);

  // Admin Util constants
  STR_CONSTANT (CUPS_SERVER_DEBUG_LOGGING);
  STR_CONSTANT (CUPS_SERVER_REMOTE_ADMIN);
  STR_CONSTANT (CUPS_SERVER_REMOTE_PRINTERS);
  STR_CONSTANT (CUPS_SERVER_SHARE_PRINTERS);
  STR_CONSTANT (CUPS_SERVER_USER_CANCEL_ANY);
#if CUPS_VERSION_MAJOR > 1 || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 3)
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
