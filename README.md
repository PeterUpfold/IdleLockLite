Idle Lock Lite
==============

Lightweight Win32 C(++) application to lock the screen with a timed warning.

This offers a balanced solution for environments where workstations must be locked, but where the user may legitimately
be "idle" for some period of time in terms of mouse and keyboard activity, but still present.

The application notices periods of the computer being idle. If the configured idle time expires, a dialogue box appears
warning the user that the computer will lock. If this is not dismissed by moving the mouse or pressing a key, the system
will lock.

## Usage

Start the `IdleLockLite.exe` program with exactly two arguments:

  * the number of seconds after which the idle warning dialogue box should be displayed
  * the number of seconds grace period while the dialogue is open that the user can cancel the locking of the computer.

You would likely want to use some method, such as Group Policy or the registry `Run` key, to have this application launch when the user logs on.

The application is lightweight (~21 KB) and uses minimal resources.
