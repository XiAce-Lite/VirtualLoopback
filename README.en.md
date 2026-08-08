# VirtualLoopback

A VST3 plugin that routes **PC playback audio** (browser, media players, etc.) into a DAW track **without virtual cables**.

It uses Windows **WASAPI loopback**. VB-Cable / VoiceMeeter are not required.

> **Supported OS:** Windows only (not available on Mac)

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

No build step is required. Use the files in the repository **`Release`** folder.

1. Get [`Release/VirtualLoopback.vst3`](Release/VirtualLoopback.vst3)  
   (If you download a zip from GitHub Releases, extract it and use the single **`VirtualLoopback.vst3` file** — not a folder-style VST3 bundle.)
2. Copy that `.vst3` file to either:
   - `C:\Program Files\Common Files\VST3\`
   - or your DAW’s VST3 folder
3. Rescan plugins in your DAW
4. Confirm **XiAceLite** / **VirtualLoopback** appears

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

1. **Add an Instrument track** (Project → Add Track → Instrument)
2. Select **VirtualLoopback**  
   (around the Instrument / Synth category)
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

---

## Build (for developers)

- JUCE: `F:/JUCE`
- To regenerate with Projucer:

```powershell
& F:\JUCE\Projucer.exe --resave D:\Documents\GitHub\VirtualLoopback\VirtualLoopback.jucer
```

- Build `Builds\VisualStudio2022\VirtualLoopback.sln` as x64 (VST3 target) in Visual Studio

If using MSBuild, prefer the **amd64** toolchain.

---

## Notes

- **Windows only** — does not work on Mac
- Playing prepared static files (WAV, etc.) is out of scope for this plugin
- On first capture start, there may be a very short silence while the internal buffer fills
