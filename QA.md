# Testing Brain Dance on an OP-XY

Checks to run on a device with a firmware that includes Brain Dance, from top to bottom, in one project:
each section leaves the project ready for the next. To run only some sections, set up what their first
line says. Host tests (`python ../open-xy/sdk.py test --machine .`) don't replace these: they can't hear
the sound or see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps.

Note the SDK commit and firmware each full pass used.

## Setup

- Build a firmware with the machine ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Brain Dance: give track 1 a stock sound of its own and save it.
  Later steps load it as a project that never had the machine.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: Brain Dance (Shift + M1 on track 1, then choose it), with one note on step 1 lasting a whole
  bar, so it sounds all the time while the sequencer plays.
- Press play, and leave it playing unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Brain Dance is in the engine list of tracks 1-8 (Shift + M1 there), under its name, and in no other
      track's list

## Its page and controls

- [ ] Each control through its whole range, both directions: the sound follows, with no clicks, jumps or
      silence at the ends; the value shown reads in its unit (Hz, ms, %, a name)
- [ ] Choice controls move one choice per detent, both directions, and wrap or stop as the page says
- [ ] Fast turns: the value keeps up, no flicker or slowdown
- [ ] Controls with an alternate (an encoder press, a second page): pressing switches, the page shows
      which is active, and each keeps its own value
- [ ] The page while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] Every key across the keyboard's octaves starts a note at its pitch, and releasing it ends the note;
      no note gets stuck, also after long holds
- [ ] Chords and fast repeated notes: every note sounds and ends
- [ ] M2 and M3 (envelope and filter) change the sound as on a stock engine

## The screen

- [ ] At rest (no keys) the brain is still: a cloud of clearly visible points, dim red and blue clusters, nothing else below the header
- [ ] Hold a key: the brain turns smoothly; a cluster flashes and a wave of white runs out through the points from it
- [ ] dense from 0 to 100, then more keys: more and more thin links join neighbouring points, under the points and dimmer;
      at dense 0 with no keys there are none; it stays tidy, the points still the picture
- [ ] The turn is smooth in every algorithm (break and data included) and whatever any control is set to:
      glitch, crush and chaos never make it step, jump or lag
- [ ] Release: the turn eases to a stop within about a second, then nothing moves
- [ ] Each control shows on the brain: ring sends the wave further, tone grows the dots, chaos turns it faster,
      glitch makes points and links flicker, crush above 60 strips the far side away, color tints the idle clusters
- [ ] Links meet their points; no stray lines, streaks, or points over the header, at every setting

## The sixty steps

- [ ] Encoder 1 steps one choice a detent through sixty: lanes, lane1 .. lane9, break, brk1 .. brk9, data .. data9,
      eucl .. eucl9, pulse, puls1 .. puls9, shred, shrd1 .. shrd9; no caption runs into the next column
- [ ] Within an algorithm each step plays differently (other cycle lengths, another break, other lanes leading)
- [ ] A preset on a variant (Threes: shrd3; Two Step: brk1) loads on its step

## Shred and the space

- [ ] The default preset (Shred) at 180 BPM, one key: the bass sweeps down on nearly every 16th, clipped; now and then
      it rests under a burst of noise that is cut dead, or lands and holds a low note
- [ ] Add keys: each adds its own cycle (six, eight, five, seven steps) grinding against the others; the top end stays
      clicks and bursts, not a constant hiss
- [ ] ring from 0 up: the gated reverb comes in on the snaps and is cut by the bass; above half a metallic comb rings at the key
- [ ] glitch up: the reverb is chopped into slices, clicks stutter in triplets, and a voice drops out for a millisecond or two
- [ ] crush from 0 to 60%: louder and squarer, with no loss of top end; above 60% it turns to bits
- [ ] color moves the bass from a sine to a buzz; tone lengthens each sweep until the steps join
- [ ] Hold eight keys on Gate Storm for a minute: no dropouts in the audio other than glitch's own, no slowdown

## Samples

- [ ] With no sample: "load a sample  shift + [key]" shows in the bottom left corner, and nothing of the stock
      sampler's page shows behind the brain
- [ ] With a sample loaded: the hint is gone, and still nothing of the stock page (no waveform) shows behind the brain
- [ ] Hold Shift with a sample: the brain dims, "swap sample with [key]" shows, and the header reads start, window, gain,
      end with times and the gain; the encoders move them; release and the brain is back

- [ ] Shift + the sample key on Brain Dance's track: the stock sample browser opens; load a sample with clear hits
      (a drum loop or a kit) and hold a key: within a second or two the lanes play its kick, snare, hats instead of their own synthesis
- [ ] Play C, then other keys: on C the hits sound at their own pitch; other keys tune them up to six semitones either way
- [ ] tone shortens the hits (low) and lets them ring (high); chaos now and then swaps in another hit from the sample
- [ ] Load a sample of a held note or a pad: the low and blip lanes play its tone, tuned to the key; load a held noise: snap and click play it
- [ ] Move the sample's start and end markers past the hits: lanes with nothing inside the trim go back to their synthesis
- [ ] The sample's gain (Shift + encoder 3) changes the snippets' level
- [ ] A 20-second sample: analysis finishes (the lanes change over) without a glitch in the audio or the screen
- [ ] Remove the sample (or load silence): every lane plays its own synthesis again
- [ ] Brain Dance on three tracks, each with its own sample: each plays its own

## Parameter locks

- [ ] Lock each control on a step (hold the step, turn the knob): it plays on that step, and the page
      shows the lock while the step is held
- [ ] Several locks on one step, and locks on neighbouring steps: each plays as written
- [ ] Clear the locks: the control plays its own value again

## LFOs

- [ ] The track's LFO (M4) lists the machine's controls by their names, and moving one of them is heard,
      stopped and playing
- [ ] The LFO on a choice control switches choices as it moves, and nothing freezes
- [ ] Rate, depth and shape behave as on a stock engine; turning the LFO off stops the movement

Then turn the LFO off.

## Patterns, scenes and songs

- [ ] Give the track a second pattern with different values: each pattern keeps its own
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Switch between the patterns while playing and while stopped: the sound and the page follow at once
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Presets

- [ ] Shift + track key lists presets: Brain Dance's are listed where they belong, and choosing one sets its
      values
- [ ] Save the track's sound as a preset, change it, load the preset: the saved values come back
- [ ] A stock preset loaded on a track holding Brain Dance (and the reverse) gives what it says, with no stray
      values

## Mute

- [ ] Mute the track (hold aux and press the track's key): it's silent, with no click or tail left behind;
      unmute: it plays as before
- [ ] The same from the mixer
- [ ] Muted in scene 2 only: switching between scenes 1 and 2 mutes and unmutes it

Unmute it everywhere.

## Saving and loading

- [ ] Save **qa**, power off and on, load it: the machine is there with every value as you left it, on
      each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound, nothing of **qa** shows. Load **qa** again: all back
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Brain Dance on a track; it starts at its defaults
- [ ] On a firmware without Brain Dance, open **qa**: it loads, and the rest of the project plays

## CPU

- [ ] Follow [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md):
      a profiling build from committed source, every required workload collected, and the readout
      kept with the commit it measured
- [ ] Run `sdk.py profile report`: inspect both average CPU (`block_percent`) and worst observed block
      CPU (`peak_percent`), and the workload that produced the spike; a heaviest-setting average is not a peak
- [ ] Before submitting, copy the complete generated `cpu` member into the catalog entry; resolve clipped
      or invalid peaks rather than omitting them. Hardware evidence still pending means submission is not ready

## In the builder

Once the machine's catalog entry is merged and released (the beta site from `staging`, open-xy.com from
`main`; see [submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Brain Dance as Community, with its name, maintainer and description
- [ ] Filtering by its kind (samplers) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it; its details panel lists its keywords, and
      clicking one lists it among that keyword's machines
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

Not failures, but worth confirming they haven't changed.

- While a sample is analysed, the OP-XY shows its own "Preparing sample" popup over the screen. It is
  the stock popup; Brain Dance can't move or restyle it.
- A sample engine has eight controls and no Shift row of its own: Shift edits the stock sampler's
  start, window, gain and end.
- Brain Dance and Timestretch can't be built into one firmware: a firmware's sample analyses share
  2 MiB, and Timestretch reserves all of it.
- A full mix gives fewer usable hits than a sample with separate hits and held notes.
