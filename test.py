#!/usr/bin/python
import cups

# Simple demonstration of cups module

def callback (prompt):
	print ("Password is required for this operation")
	password = raw_input (prompt)
	return password

def test_cups_module ():
	cups.setUser ("root")
	cups.setPasswordCB (callback)
	conn = cups.Connection ()
	printers = list(conn.getPrinters ().keys ())

	if 0:
		print ("Getting cupsd.conf")
		file ("cupsd.conf", "w")
		conn.getFile ("/admin/conf/cupsd.conf", "cupsd.conf")
		print ("Putting cupsd.conf")
		conn.putFile ("/admin/conf/cupsd.conf", "cupsd.conf")

	print ("Getting PPD for %s" % printers[len (printers) - 1])
	f = conn.getPPD (printers[len (printers) - 1])
	ppd = cups.PPD (f)
	ppd.markDefaults ()
	print (ppd.conflicts ())
	groups = ppd.optionGroups
	for group in groups:
		for opt in group.options:
			print (list (map (lambda x: x["text"], opt.choices)))

test_cups_module ()
