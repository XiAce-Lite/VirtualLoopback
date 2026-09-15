# VirtualLoopback

A plugin that routes **PC playback audio** (browser, media players, etc.) into a DAW track **without virtual cables**.

- **Windows:** **WASAPI loopback** (VB-Cable / VoiceMeeter not required)
- **Mac:** **Core Audio Process Tap** (BlackHole / Rogue Amoeba Loopback not required; macOS 14.2+)

> **Supported OS:** Windows, and macOS 14.2 or later  
> Mac sources live on the `feature/macos-coreaudio` branch. The master branch still ships the Windows VST3.

[日本語版 README](README.md)

---

## What it does

When playing in a SYNCROOM session, you may want others to hear not only your mic/instrument, but also whatever is playing on your PC (e.g. YouTube or karaoke audio).

Even without an audio interface that has a hardware loopback feature, you can send that playback into SYNCROOM by inserting this plugin in your DAW.

```text
[Mic / instrument] ──► Track A ──┐
                                  ├─► Bus / Master ─► syncroom_vst_bridge2 ─► SYNCROOM
[VirtualLoopback] ─► Track B ────┘   (PC playback such as Chrome)
```

For pre-made static files, use SYNCROOM’s built-in file playback. This plugin is for **live playback audio**.

---

## Install

### Windows

No build step is required. Use the files in the repository **`Release`** folder.

1. Get [`Release/VirtualLoopback.vst3`](Release/VirtualLoopback.vst3)  
   (If you download a zip from GitHub Releases, extract it and use the single **`VirtualLoopback.vst3` file** — not a folder-style VST3 bundle.)
2. Copy that `.vst3` file to **`C:\Program Files\Common Files\VST3\`**  
   (the standard VST3 install location; if your system drive is not C:, use `%CommonProgramFiles%\VST3`)
3. **If that folder does not exist**, create the `VST3` folder at the path above, then copy the file  
   (on PCs that have never had a third-party VST3 installed, this folder may be missing from the start. Administrator privileges may be required.)
4. Rescan plugins in your DAW (for Cubase, restarting the app also works)
5. Confirm **XiAceLite** / **VirtualLoopback** appears

### Mac (VST3 / AU)

The Mac build is a **bundle**. Use **`VirtualLoopback-macos-universal.zip`** from GitHub Releases.

1. Unzip, then clear quarantine: `xattr -cr VirtualLoopback.vst3` and `xattr -cr VirtualLoopback.component`
2. Copy the **`VirtualLoopback.vst3` package itself** to **`~/Library/Audio/Plug-Ins/VST3/`** (top level — not nested in another folder). On Mac, `.vst3` is a folder; final path must be `.../VST3/VirtualLoopback.vst3/Contents/MacOS/VirtualLoopback`  
   (Cubase, SYNCROOM VST link, and other VST3 hosts)
3. Copy `VirtualLoopback.component` to **`~/Library/Audio/Plug-Ins/Components/`**  
   (Logic / GarageBand and other AU hosts)
4. Create those folders if they do not exist
5. Rescan plugins in your DAW (Cubase: Studio → VST Plug-in Manager)
6. On first capture, grant **Screen & System Audio Recording** to **the DAW** (the host, not the plugin). If no prompt appears, add the DAW manually in System Settings

If AU appears but VST3 does not, the VST3 path is usually nested incorrectly or quarantine is still set.

BlackHole and Loopback are not required. This plugin does not install a virtual audio device.

---

## How to insert it in a DAW

VirtualLoopback is built as an **Instrument / synth**, not a regular FX insert.  
It may not appear in FX / Insert lists. Load it as a **synth / instrument** on a track.

### Cakewalk (Sonar)

1. Create an empty track (or add an instrument track)
2. Choose **VirtualLoopback** from the track’s **soft synth / Instrument** slot
3. Enable playback/monitor on that track if needed
4. In the plugin UI, select the capture device and confirm the level meter moves

※ Looking in the FX rack (insert effects) often won’t find it.

### Cubase

#### Install location (Cubase users)

In Cubase, **do not** use these locations for VirtualLoopback:

- `C:\Program Files\Steinberg\VSTPlugins\` — **for VST2** (the folder shown in the VST 2 Plug-in Path Settings in the Plug-in Manager)
- `C:\Program Files\Steinberg\Cubase 14\VST3\` — **for bundled Cubase plugins** (some users report third-party plugins working here, but it is not the official location for third-party VST3)

Put VirtualLoopback in **`C:\Program Files\Common Files\VST3\`**.  
If that folder is missing, **create it manually**. Cubase does not let you add custom VST3 scan paths, so this is effectively the only correct location.

If the plugin is not recognized, open **Studio → VST Plug-in Manager → Plug-in Report** and confirm VirtualLoopback’s **Path** points to `Common Files\VST3`.

#### Adding it to a track

1. **Add an Instrument track** (Project → Add Track → Instrument)
2. Select **VirtualLoopback**  
   (around the Instrument / Synth category; it will not appear in the FX list)
3. Enable monitor (or input listening) and confirm audio / meter activity
4. Optionally route to a group/master and mix with the SYNCROOM bridge VST downstream

### Common idea for other DAWs

- Treat it as a **sound source**, not an effect that processes an existing waveform
- Put it on a **separate track** from your mic, then mix on a bus/master

---

## Connecting to SYNCROOM (VST link)

1. Prepare a mic/instrument track and a VirtualLoopback track in the DAW
2. Send both to a bus or the master
3. Insert **syncroom_vst_bridge2** on the master (or send bus) and launch SYNCROOM in VST-link mode
4. SYNCROOM uses the DAW path instead of its normal ASIO device settings

Using the same ASIO device in both the DAW and SYNCROOM at once often conflicts, so **VST link is the recommended approach** for this workflow.

---

## Choosing the playback device in the plugin UI

This is the most confusing part.

### Windows

VirtualLoopback captures whatever is playing on the **Windows render (playback) device** you select.

### What to choose (typical)

When Chrome or an MP3 player normally comes out of your speakers on Windows, the device is often something like:

- **Speakers (Realtek(R) Audio)**
- **Headphones (Realtek(R) Audio)**
- Other built-in Realtek speaker/headphone devices on laptops

If the name includes **Realtek**, you are usually choosing the right one.  
(Use Headphones when earbuds are plugged in, Speakers for external speakers, etc.)

### What not to choose

- Devices that carry **SYNCROOM / DAW monitor return**
- Lines such as **Yamaha SYNCROOM Driver** (that path is the opposite direction: SYNCROOM → other apps)

Choosing those can loop remote/monitor audio back into the input and cause echo or feedback.

### How to verify

1. Select your usual Realtek (or similar) playback device
2. Play audio in Chrome or another player
3. If the plugin’s **output level** meter moves, you’re good

### Mac

Keep the default **システム再生音** item. It taps playback from apps other than the DAW (Chrome, Music, etc.). The host DAW and SYNCROOM are excluded. You can also pick a named output device after that.

If capture is running but the meter stays still, open **System Settings → Privacy & Security → Screen & System Audio Recording** and allow the DAW. Without that permission the APIs can still succeed and deliver only silence.

---

## Build (for developers)

- JUCE: `F:/JUCE`
- To regenerate with Projucer:

```powershell
& F:\JUCE\Projucer.exe --resave D:\Documents\GitHub\VirtualLoopback\VirtualLoopback.jucer
```

- Build `Builds\VisualStudio2022\VirtualLoopback.sln` as x64 (VST3 target) in Visual Studio

If using MSBuild, prefer the **amd64** toolchain.

### Mac

- macOS 14.2+, Xcode, and JUCE
- In Projucer, point the Xcode exporter’s JUCE module path at your Mac JUCE tree, then `--resave`
- Build `Builds/MacOSX/VirtualLoopback.xcodeproj` as Universal (arm64 + x86_64) for VST3 and AU
- Codesign and notarize before shipping (ad-hoc signing often makes the system-audio permission unreliable)

---

## Notes

- Mac requires **macOS 14.2 or later** (there is no public playback-mix API before that)
- Mac does **not** use BlackHole, Loopback, or a virtual HAL driver
- Playing prepared static files (WAV, etc.) is out of scope for this plugin
- On first capture start, there may be a very short silence while the internal buffer fills
