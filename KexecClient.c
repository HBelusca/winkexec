/* WinKexec: kexec for Windows
 * Copyright (C) 2008 John Stumpo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>
#include <stdio.h>
#include "kexec.h"
#include "Revision.h"

/* Convenient wrapper around FormatMessage() and GetLastError() */
LPSTR KexecTranslateError(void)
{
  static LPSTR msgbuf = NULL;

  if (msgbuf) {
    LocalFree(msgbuf);
    msgbuf = NULL;
  }

  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
    NULL, GetLastError(), LANG_USER_DEFAULT, (LPSTR)&msgbuf, 0, NULL);

  return msgbuf;
}

/* Even more convenient wrapper around the above function.
   Use just like perror().
   XXX: Does the Windows API have something like this already? */
void KexecPerror(char * errmsg)
{
  fprintf(stderr, "%s: %s", errmsg, KexecTranslateError());
}

/* Is the kexec driver loaded? */
BOOL KexecDriverIsLoaded(void)
{
  SC_HANDLE Scm;
  SC_HANDLE KexecService;
  SERVICE_STATUS_PROCESS ServiceStatus;
  BOOL retval;
  DWORD ExtraBytes;

  Scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!Scm) {
    KexecPerror("Could not open SCM");
    exit(EXIT_FAILURE);
  }

  KexecService = OpenService(Scm, "kexec", SERVICE_ALL_ACCESS);
  if (!KexecService) {
    KexecPerror("Could not open the kexec service");
    fprintf(stderr, "(Is the kexec driver installed, and are you an admin?)\n");
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  if (!QueryServiceStatusEx(KexecService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ServiceStatus,
    sizeof(ServiceStatus), &ExtraBytes))
  {
    KexecPerror("Could not query the kexec service");
    fprintf(stderr, "(Are you an admin?)\n");
    CloseServiceHandle(KexecService);
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  retval = (ServiceStatus.dwCurrentState == SERVICE_RUNNING);
  CloseServiceHandle(KexecService);
  CloseServiceHandle(Scm);
  return retval;
}

/* Load kexec.sys into the kernel, if it isn't already. */
void LoadKexecDriver(void)
{
  SC_HANDLE Scm;
  SC_HANDLE KexecService;

  if (KexecDriverIsLoaded())
    return;

  printf("Loading the kexec driver... ");

  Scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!Scm) {
    KexecPerror("Could not open SCM");
    exit(EXIT_FAILURE);
  }

  KexecService = OpenService(Scm, "kexec", SERVICE_ALL_ACCESS);
  if (!KexecService) {
    KexecPerror("Could not open the kexec service");
    fprintf(stderr, "(Is the kexec driver installed, and are you an admin?)\n");
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  /* This does not return until DriverEntry() has completed in kexec.sys. */
  if (!StartService(KexecService, 0, NULL)) {
    KexecPerror("Could not start the kexec service");
    fprintf(stderr, "(Are you an admin?)\n");
    CloseServiceHandle(KexecService);
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  CloseServiceHandle(KexecService);
  CloseServiceHandle(Scm);
  printf("ok\n");
  return;
}

/* If kexec.sys is loaded into the kernel, unload it. */
void UnloadKexecDriver(void)
{
  SC_HANDLE Scm;
  SC_HANDLE KexecService;
  SERVICE_STATUS ServiceStatus;

  if (!KexecDriverIsLoaded())
    return;

  printf("Unloading the kexec driver... ");

  Scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!Scm) {
    KexecPerror("Could not open SCM");
    exit(EXIT_FAILURE);
  }

  KexecService = OpenService(Scm, "kexec", SERVICE_ALL_ACCESS);
  if (!KexecService) {
    KexecPerror("Could not open the kexec service");
    fprintf(stderr, "(Is the kexec driver installed, and are you an admin?)\n");
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  /* This does not return until DriverUnload() has completed in kexec.sys. */
  if (!ControlService(KexecService, SERVICE_CONTROL_STOP, &ServiceStatus)) {
    KexecPerror("Could not stop the kexec service");
    fprintf(stderr, "(Are you an admin?)\n");
    CloseServiceHandle(KexecService);
    CloseServiceHandle(Scm);
    exit(EXIT_FAILURE);
  }

  CloseServiceHandle(KexecService);
  CloseServiceHandle(Scm);
  printf("ok\n");
  return;
}

/* Handle kexec /l */
int DoLoad(int argc, char** argv)
{
  DWORD klen, ilen, read_len;
  unsigned char* kbuf;
  unsigned char* ibuf = NULL;
  HANDLE kernel, initrd, device;
  int i;

  /* No args: just load the driver and do nothing else. */
  if (argc < 1) {
    if (!KexecDriverIsLoaded())
      LoadKexecDriver();
    else
      printf("The kexec driver was already loaded; nothing to do.\n");
    exit(EXIT_SUCCESS);
  }

  printf("Using kernel: %s\n", argv[0]);

  /* Read the kernel into a buffer. */
  printf("Reading kernel... ");
  /* Open it... */
  if ((kernel = CreateFile(argv[0], GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
    KexecPerror("Failed to load kernel");
    exit(EXIT_FAILURE);
  }

  /* ...get the size... */
  if ((klen = GetFileSize(kernel, NULL)) == INVALID_FILE_SIZE) {
    KexecPerror("Failed to get kernel size");
    CloseHandle(kernel);
    exit(EXIT_FAILURE);
  }

  /* ...grab a buffer... */
  if ((kbuf = malloc(klen)) == NULL) {
    perror("Could not allocate buffer for kernel");
    CloseHandle(kernel);
    exit(EXIT_FAILURE);
  }
  /* ...read it in... */
  if (!ReadFile(kernel, kbuf, klen, &read_len, NULL)) {
    KexecPerror("Could not read kernel");
    CloseHandle(kernel);
    exit(EXIT_FAILURE);
  }
  /* ...and close it. */
  CloseHandle(kernel);
  printf("ok\n");

  /* Make sure we got all of it. */
  if (klen != read_len) {
    fprintf(stderr, "internal error: buffer length mismatch!\n");
    fprintf(stderr, "(" __FILE__ ":%d)\n", __LINE__);
    fprintf(stderr, "please report this to Stump!\n");
    exit(EXIT_FAILURE);
  }

  /* Magic numbers in a Linux kernel image */
  if (*(unsigned short*)(kbuf+510) != 0xaa55 ||
    strncmp(kbuf+514, "HdrS", 4) != 0)
  {
      fprintf(stderr, "warning: This does not look like a Linux kernel.\n");
      fprintf(stderr, "warning: Loading it anyway.\n");
  }

  /* Look for an initrd. */
  for (i = 1; i < argc; i++) {
    if (!strncasecmp(argv[i], "initrd=", 7)) {
      printf("Using initrd: %s\n", argv[i]+7);

      /* Read the initrd into a buffer. */
      printf("Reading initrd... ");
      /* Open it... */
      if ((initrd = CreateFile(argv[i]+7, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
        KexecPerror("Failed to load initrd");
        exit(EXIT_FAILURE);
      }

      /* ...get the size... */
      if ((ilen = GetFileSize(initrd, NULL)) == INVALID_FILE_SIZE) {
        KexecPerror("Failed to get initrd size");
        CloseHandle(initrd);
        exit(EXIT_FAILURE);
      }

      /* ...grab a buffer... */
      if ((ibuf = malloc(ilen)) == NULL) {
        perror("Could not allocate buffer for initrd");
        CloseHandle(initrd);
        exit(EXIT_FAILURE);
      }
      /* ...read it in... */
      if (!ReadFile(initrd, ibuf, ilen, &read_len, NULL)) {
        KexecPerror("Could not read initrd");
        CloseHandle(initrd);
        exit(EXIT_FAILURE);
      }
      /* ...and close it. */
      CloseHandle(initrd);
      printf("ok\n");

      /* Make sure we got all of it. */
      if (ilen != read_len) {
        fprintf(stderr, "internal error: buffer length mismatch!\n");
        fprintf(stderr, "(" __FILE__ ":%d in revision %d)\n", __LINE__, CLIENT_REVISION);
        fprintf(stderr, "please report this to Stump!\n");
        exit(EXIT_FAILURE);
      }
    }
  }

  /* Now let kexec.sys know about it. */
  LoadKexecDriver();
  /* \\.\kexec is the interface to kexec.sys. */
  if ((device = CreateFile("\\\\.\\kexec", 0, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
    KexecPerror("Failed to open \\\\.\\kexec");
    fprintf(stderr, "(Are you an admin?)\n");
    exit(EXIT_FAILURE);
  }

  /* Do the kernel... */
  printf("Loading kernel into kexec driver... ");
  if (!DeviceIoControl(device, KEXEC_SET | KEXEC_KERNEL, kbuf, klen, NULL, 0, &read_len, NULL)) {
    KexecPerror("Could not load kernel into driver");
    CloseHandle(device);
    exit(EXIT_FAILURE);
  }
  free(kbuf);
  printf("ok\n");

  /* ...and the initrd. */
  if (ibuf) {
    printf("Loading initrd into kexec driver... ");
    if (!DeviceIoControl(device, KEXEC_SET | KEXEC_INITRD, ibuf, ilen, NULL, 0, &read_len, NULL)) {
      KexecPerror("Could not load initrd into driver");
      CloseHandle(device);
      exit(EXIT_FAILURE);
    }
    free(ibuf);
    printf("ok\n");
  }

  /* And we're done! */
  CloseHandle(device);
  printf("ok\n");

  exit(EXIT_SUCCESS);
}

/* Handle kexec /u */
int DoUnload(int argc, char** argv)
{
  if (KexecDriverIsLoaded())
    UnloadKexecDriver();
  else
    printf("The kexec driver was not loaded; nothing to do.\n");
  exit(EXIT_SUCCESS);
}

/* Handle kexec /s */
int DoShow(int argc, char** argv)
{
  /* kexec.sys can't tell us anything if it's not loaded... */
  if (!KexecDriverIsLoaded()) {
    printf("The kexec driver is not loaded.  (Use `kexec /l' to load it.)\n");
    exit(EXIT_FAILURE);
  }

  /* Well, we know this much. */
  printf("The kexec driver is active.  (Use `kexec /u' to unload it.)\n");

  /* XXX: Actually show runtime state! */
  exit(EXIT_SUCCESS);
}

/* Show help on cmdline usage of kexec. */
void usage()
{
  fprintf(stderr, "%s",
"\n\
WinKexec: kexec for Windows (v1.0, svn revision " CLIENT_REVISION_STR ")\n\
Copyright (C) 2008 John Stumpo\n\
\n\
This program is free software; you may redistribute or modify it under the\n\
terms of the GNU General Public License, version 3 or later.  There is\n\
ABSOLUTELY NO WARRANTY, not even for MERCHANTABILITY or FITNESS FOR A\n\
PARTICULAR PURPOSE.  See the GPL version 3 for full details.\n\
\n\
Usage: kexec [action] [options]...\n\
Actions:\n\
  /l /load     Load a Linux kernel.\n\
    The next option is the kernel filename.  All subsequent options are\n\
    passed as the kernel command line.  If an initrd= option is given,\n\
    the named file will be loaded as an initrd.  The kexec driver will\n\
    be loaded automatically if it is not loaded.  With no options, just\n\
    load the kexec driver without loading a kernel.\n\
  /u /unload   Unload the kexec driver.  (Naturally, this causes it to\n\
    forget the currently loaded kernel, if any, as well.)\n\
  /c /clear    Clear the currently loaded Linux kernel, but leave the\n\
    kexec driver loaded.\n\
  /s /show     Show current state of kexec.\n\
  /h /? /help  Show this help.\n\
\n");
  exit(EXIT_FAILURE);
}

/* Entry point */
int main(int argc, char** argv)
{
  int (*action)(int, char**) = NULL;

  /* No args given, no sense in doing anything. */
  if (argc < 2)
    usage();

  /* Allow Unix-style cmdline options.
     XXX: Add GNU-style too! */
  if (argv[1][0] == '-')
    argv[1][0] = '/';

  /* Decide what to do... */
  if (!strcasecmp(argv[1], "/l") || !strcasecmp(argv[1], "/load"))
    action = DoLoad;

  if (!strcasecmp(argv[1], "/u") || !strcasecmp(argv[1], "/unload"))
    action = DoUnload;

  if (!strcasecmp(argv[1], "/s") || !strcasecmp(argv[1], "/show"))
    action = DoShow;

  if (!action)
    usage();

  /* ...and do it. */
  exit(action(argc - 2, argv + 2));
}
