# vim:set noet ts=4:
# -*- coding: utf-8 -*-
#
# ibus-anthy - The Anthy engine for IBus
#
# Copyright (c) 2007-2008 Huang Peng <shawn.p.huang@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

import gobject
import pango
import dbus
import ibus
import m17n
from ibus import keysyms
from ibus import interface

_ = lambda a: a

class Engine (interface.IEngine):
	def __init__ (self, ic, dbusconn, object_path):
		interface.IEngine.__init__ (self, dbusconn, object_path)
		self._dbusconn = dbusconn
		self._ic = ic

		self._ic.set_callback (m17n.Minput_preedit_start.name (), self._input_preedit_start_cb)
		self._ic.set_callback (m17n.Minput_preedit_done.name (), self._input_preedit_done_cb)
		self._ic.set_callback (m17n.Minput_preedit_draw.name (), self._input_preedit_draw_cb)
		
		self._ic.set_callback (m17n.Minput_status_start.name (), self._input_states_start_cb)
		self._ic.set_callback (m17n.Minput_status_done.name (), self._input_states_done_cb)
		self._ic.set_callback (m17n.Minput_status_draw.name (), self._input_states_draw_cb)
		
		self._ic.set_callback (m17n.Minput_candidates_start.name (), self._input_candidates_start_cb)
		self._ic.set_callback (m17n.Minput_candidates_done.name (), self._input_candidates_done_cb)
		self._ic.set_callback (m17n.Minput_candidates_draw.name (), self._input_candidates_draw_cb)
		
		self._ic.set_callback (m17n.Minput_set_spot.name (), self._input_set_spot_cb)
		self._ic.set_callback (m17n.Minput_toggle.name (), self._input_toggle_cb)
		self._ic.set_callback (m17n.Minput_reset.name (), self._input_reset_cb)

		self._ic.set_callback (m17n.Minput_get_surrounding_text.name (), self._input_get_surrounding_text_cb)
		self._ic.set_callback (m17n.Minput_delete_surrounding_text.name (), self._input_delete_surrounding_text_cb)

		self._lookup_table = ibus.LookupTable ()

	def _input_preedit_start_cb (self, command): pass
	def _input_preedit_done_cb (self, command): pass
	def _input_preedit_draw_cb (self, command): pass
	
	def _input_states_start_cb (self, command): pass
	def _input_states_done_cb (self, command): pass
	def _input_states_draw_cb (self, command): pass
	
	def _input_candidates_start_cb (self, command): pass
	def _input_candidates_done_cb (self, command): pass
	def _input_candidates_draw_cb (self, command): pass

	def _input_set_spot_cb (self, command): pass
	def _input_toggle_cb (self, command): pass
	def _input_reset_cb (self, command): pass

	def _input_get_surrounding_text_cb (self, command): pass
	def _input_delete_surrounding_text_cb (self, command): pass

	def _page_up (self):
		pass

	def _page_down (self):
		pass

	def _cursor_up (self):
		pass

	def _cursor_down (self):
		pass

	def _process_key_event (self, keyval, is_press, state):
		if not is_press:
			return False

		key = chr (keyval)
		ret = self._ic.filter (key)
		
		if ret:
			return True

		lookup = self._ic.lookup (key)
		if lookup:
			self.CommitString (lookup)

		return True

	def _property_activate (self, prop_name, state):
		pass

	def _update_property (self, prop):
		self.UpdateProperty (prop.to_dbus_value ())

	# methods for dbus rpc
	def ProcessKeyEvent (self, keyval, is_press, state):
		try:
			return self._process_key_event (keyval, is_press, state)
		except Exception, e:
			print e
		return False

	def FocusIn (self):
		print "FocusIn"

	def FocusOut (self):
		print "FocusOut"

	def SetCursorLocation (self, x, y, w, h):
		pass

	def Reset (self):
		print "Reset"

	def PageUp (self):
		self._page_up ()

	def PageDown (self):
		self._page_down ()

	def CursorUp (self):
		self._cursor_up ()

	def CursorDown (self):
		self._cursor_down ()

	def SetEnable (self, enable):
		self._enable = enable
		pass

	def PropertyActivate (self, prop_name, prop_state):
		self._property_activate (prop_name, prop_state)

	def Destroy (self):
		print "Destroy"

