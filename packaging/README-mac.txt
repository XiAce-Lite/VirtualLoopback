VirtualLoopback for Mac (Core Audio Process Tap)
================================================

macOS 14.2+ / Universal (Apple Silicon + Intel)
BlackHole / Rogue Amoeba Loopback are NOT required.

This zip was built on GitHub Actions. It is ad-hoc signed, not Apple-notarized.

IMPORTANT — VST3 is a FOLDER
----------------------------
On Mac, VirtualLoopback.vst3 is a package (folder), not a single file like Windows.
Copy the whole .vst3 package. Do not open it and copy only Contents/MacOS.

Install
-------
1. Unzip. You should see these at the TOP LEVEL (not inside another folder):
     VirtualLoopback.vst3
     VirtualLoopback.component
     README-mac.txt

2. Clear quarantine (required — otherwise VST3 often fails to scan while AU still appears):

     cd /path/to/unzipped
     xattr -cr VirtualLoopback.vst3
     xattr -cr VirtualLoopback.component

3. VST3 (Cubase / SYNCROOM VST link / most DAWs):

     mkdir -p ~/Library/Audio/Plug-Ins/VST3
     ditto VirtualLoopback.vst3 ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback.vst3

   Correct final path:
     ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback.vst3/Contents/MacOS/VirtualLoopback

   WRONG (hosts often miss this):
     ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback-macos/VirtualLoopback.vst3
     ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback.vst3/VirtualLoopback.vst3

4. AU (Logic / GarageBand / AU hosts):

     mkdir -p ~/Library/Audio/Plug-Ins/Components
     ditto VirtualLoopback.component ~/Library/Audio/Plug-Ins/Components/VirtualLoopback.component

5. Rescan plugins. Load as Instrument / Synth (not FX insert).

6. First capture: System Settings → Privacy & Security → Screen & System Audio Recording
   Allow the DAW host (Cubase etc.), not the plugin itself.

Cubase notes
------------
- Studio → VST Plug-in Manager → Rescan / Restart Cubase
- Look under Instrument / Synth, manufacturer XiAceLite
- If blacklisted, remove from blacklist and rescan
- Confirm the path above with Finder (Go → Go to Folder… → ~/Library/Audio/Plug-Ins/VST3)

Verify install in Terminal
--------------------------
  ls ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback.vst3/Contents/MacOS
  ls ~/Library/Audio/Plug-Ins/Components/VirtualLoopback.component/Contents/MacOS
  xattr -p com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/VirtualLoopback.vst3 2>/dev/null || echo "VST3 quarantine: cleared OK"

Usage
-----
Keep the default “システム再生音”, play Chrome/Music, watch the output meter.

Troubleshooting
---------------
- AU found, VST3 missing: almost always wrong copy path or quarantine still set
- Capture running but silent: grant System Audio Recording to the DAW
- Feedback: do not tap DAW / SYNCROOM monitor return
