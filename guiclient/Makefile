# WinKexec: kexec for Windows
# Copyright (C) 2008-2009 John Stumpo
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

include ../common.mk

CFLAGS += -I../include

KEXEC_GUI_OBJECTS = KexecGui.o resource.o
KEXEC_GUI_LIBS    = ../clientdll/KexecCommon.lib -lkernel32 -lmsvcrt -luser32 -lcomctl32 -lcomdlg32

__main_target : KexecGui.exe

KexecGui.exe : $(KEXEC_GUI_OBJECTS)
	$(CC) -mwindows $(CFLAGS) -o KexecGui.exe $(KEXEC_GUI_OBJECTS) $(KEXEC_GUI_LIBS)

clean :
	-rm -f KexecGui.exe *.o
.PHONY : clean
