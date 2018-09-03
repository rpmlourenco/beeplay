Remote Speakers Output Plug-In

Copyright (c) 2006-2015  Eric Milles

http://emilles.dyndns.org/software/out_apx.html



REQUIREMENTS
------------
1) Apple TV (http://www.apple.com/appletv)
     or
   AirPort Express (http://www.apple.com/airportexpress)
     or
   AirPlay-compatibile device or software (http://airplayspeakers.com, etc.)

2) Foobar2000 (http://www.foobar2000.org)
     or
   MediaMonkey (http://www.mediamonkey.com)
     or
   MusicBee (http://getmusicbee.com)
     or
   Winamp (http://www.winamp.com)
     or
   XMPlay (http://www.xmplay.com)



INSTALLATION
------------
1) Close player and uninstall any previous version(s) of the plug-in.

2) Run the Bonjour installer (bonjourXX.msi).  Bonjour is highly recommended but
   not required.  It is used for automatic discovery of remote speakers devices.
   Bonjour 3.0.0.10 is included; check with Apple for instructions and updates
   (http://www.apple.com/support/bonjour).

3) Run the Remote Speakers Outupt Plug-In installer (rsoutput.msi).  Make note
   of the directory chosen for installation.

4) For foobar2000: Copy foo_apx.dll from plug-in install directory to player's
   Components directory.  Start player and go to File > Preferences > Playback >
   DSP Manager.  Move 'Remote Speakers output' into Active DSPs column and
   click Apply button.  Go to Playback > Output, set Device to 'Null Output'
   and click Apply button to disable local PC speakers (if desired).

   For MediaMonkey, MusicBee, or Winamp: Copy out_apx.dll from plug-in install
   directory to player's Plugins directory.  For MediaMonkey, see note in the
   KNOWN ISSUES section about disabling splash screen.  For MusicBee, the IPC
   plug-in (http://musicbee.wikia.com/wiki/MusicBeeIPC) is highly recommended;
   it is necessary for track metadata display and remote control.

   Start player and go to:
     Tools > Options > Player > Output Plug-ins (in MediaMonkey), or
     Preferences (Ctrl+O) > Player > audio player > output (in MusicBee), or
     Preferences (Ctrl+P) > Plug-ins > Output (in Winamp).
   Select 'Remote Speakers output' as the output plug-in and apply change.

   For XMPlay: Copy xmp-apx.dll from from plug-in install directory to player's
   directory.  Start player and go to Options and stuff (wrench button) > Output.
   Select 'Remote Speakers output' for Output Device and click Apply button.

5) If you have a firewall activated, configure an exception that will allow the
   player exe to accept incoming connections on the local network.  For Windows
   Firewall, this can be accomplished by opening the Windows Firewall control
   panel, clicking on the Exceptions tab, clicking Add Program, selecting the
   player from the Programs and Services list or using Browse to locate it,
   clicking Change Scope, selecting 'My network (subnet) only' and clicking OK
   to confirm in each open dialog box.  (The same could also be accomplished by
   launching the player and choosing Unblock when the Windows Security Alert
   appears; although you may wish to change the scope to the local subnet after
   creating an exception this way.)

6) Log off of Windows or restart your PC entirely.  The plug-in's installer adds
   the install directory to Windows' search path for DLLs.  This change does not
   always take effect immedaitely on running programs.  And without it, the 
   player will not be able to properly load the plug-in.



CONFIGURATION
-------------
For foobar2000: Go to File > Preferences > Playback > DSP Manager, select
   'Remote Speakers output' in Active DSPs list and click Configure selected.

For MediaMonkey: Go to Tools > Options > Player > Output Plug-ins, select
   'Remote Speakers output' in the list and click Configure.  Or, right-click on
   play controls area and pick 'Configure current output plug-in' from the menu.

For MusicBee: Go to Preferences (Ctrl+O) > Player, select 'Remote Speakers output'
   in the 'output' drop-down menu and click Configure.

For Winamp: Go to Preferences (Ctrl+P) > Plug-ins > Output, select 'Remote
   Speakers output' in the list and click Configure.

For XMPlay: Go to Options and stuff > Output > Devices.

On the plug-in's options page, remote speakers devices can be setup and selected.
Additionally, there are three other settings that can be toggled, as follows:

1) Remote speakers - list of remote speakers on local subnet.  If using Bonjour,
   this list updates automatically as remote speakers come and go.  Right-click
   inside the listbox to add or edit static entries.

2) Allow player to control volume of remote speakers - if checked, player's
   volume bar (or knob depending on your choice of skin) will influence the
   playback volume of the remote speakers.  If unchecked, the remote speakers
   should playback audio at full volume.

3) Allow player to be controlled by remote speakers - if checked, several of the
   player's controls (play, pause, seek, skip, etc.) may be operated from remote
   speakers location.  If unchecked, this functionality is disabled and incoming
   port 3689 is not used.

   For the Apple TV, the device's remote or the Remote app can be used.  For the
   AirPort Express, the Express Remote by Keyspan
   (http://www.tripplite.com/en/products/Discontinued-Products.cfm?MDLID=3911)
   is required for remote control.  For other AirPlay devices, consult with the
   manufacturer to see if remote control is supported.

4) Clear playback buffer of remote speakers on pause - if checked, the plug-in
   will instruct the remote speakers to flush its playback buffer on pause.  This
   causes playback to stop more quickly but causes some clipping of the track
   when playback is resumed.  If unchecked, no flush command will be sent so
   playback will take a few seconds to halt but there will not be any clipping
   of the track if playback is resumed.



EVALUATION
----------
You have 14 days from first use to evaluate the plug-in.  You are encouraged to
use this time wisely, to be sure the plug-in works with your player of choice
and properly streams to your remote speaker device(s) or software.  Once you
have been sent an Activation Code (see below), your payment can not be refunded.



REGISTRATION
------------
Visit the plug-in's main page (http://emilles.dyndns.org/software/out_apx.html)
to purchase a license, send in your Site Code, and receive an Activation Code.

If you have already purchased a license and are experiencing problems or need to
set up on a new workstation, send me an email (registration@emilles.dyndns.org).



KNOWN ISSUES
------------
1) Recent versions of MediaMonkey have an issue with plug-ins that display a
   modal dialog box during startup.  A modal dialog prevents any actions from
   occurring in any other windows of the application until it is dismissed.  But
   in this case, the dialog box is not visible (only the MM splash screen is).
   So no action can be taken to break the deadlock.

   To work around this problem, MediaMonkey can be started with no splash screen
   displayed.  For MediaMonkey 4 and up, this is done by unchecking 'Show splash
   screen at startup' in the General Options pane.  For MediaMonkey 3 and below,
   setting the NoSplash option does the trick.  To start MediaMonkey this way,
   edit the shorcut that is used to start it by adding "/NoSplash" to the end of
   the shortcut's Target property.  Right-click on the shortcut and select
   Properties from the context menu to edit the Target.  So for example, the
   shortcut target may be something like:
      "C:\Program Files\MediaMonkey\MediaMonkey.exe" /NoSplash
   You can try this command in the Start > Run... dialog to see if it corrects
   the problem before modifying your shortcut(s).

2) Portable versions of MediaMonkey do not register with Windows when installed.
   This prevents the plug-in from being able to determine track metadata during
   playback.  If your device supports it and you would like to have the current
   title, album, artist, etc. displayed, you can tell MM to register fully with
   the command-line options "/elevate" and "/regcomserver".  For example, run:
      "C:\Program Files\MediaMonkey\MediaMonkey.exe" /elevate /regcomserver

3) Older versions of MusicBee (before 2.4) fail to update track metadata during
   normal playback.  Pausing, seeking or skipping forces an update.  Also, the
   final few seconds of the last track played will be clipped.  These problems
   have been fixed in MusicBee 2.4.

4) It has been reported by more than one user that the plug-in may freeze the
   player when displaying the initial evalualte/activate dialog box.  The source
   of the problem seems to be the UltraMon multi-monitor utility software.  If
   you run UltraMon, I recommend you run the latest version available.

   If the player still freezes, you can uninstall UltraMon just long enough to
   evaluate or activate the plug-in.  Once activated, the plug-in will not show
   a dialog box during startup, unless your system changes quite significantly.

5) The plug-in may not function correctly with all AirPlay-compatible devices
   due to my limited ability to test with all existing devices.  If you run into
   any issues with a particular device or program, send me a support email
   (support@emilles.dyndns.org) with the details and I'll do my best to ensure
   compatibility with your device.



QUESTIONS AND COMMENTS
----------------------
Visit the plug-in's FAQ page (http://emilles.dyndns.org/software/out_apx/faq.html)
first to see if your question or comment has already been answered.

I encourage you to report any problems you find with the plug-in or suggest any
features you would like to see added in future versions.  Send me an email
(support@emilles.dyndns.org).  For problem reports, please include as much
detail as you can, inclusing the error message and error code you receive, the
type and version of the player and Windows you are running with and the type and
version of your device (if you know it) along with a complete description of
what you were doing when the error occurred.

Aditionally, trace output can be captured using the DebugView program provided
by Microsoft (http://technet.microsoft.com/en-us/sysinternals/bb896647).  Start
DebugView, then start your player and run through the recreation steps to gather
information that is invaluable in resolving your problem.  On Windows Vista and
up, you will likely need to run DebugView 'As Administrator' to capture trace
output from the plug-in.
