# Credits

CHORDMAN by swtdr34mz. All of its own code (the four engines, the effects, the player, the grooves, the sequencer, the
screens) is written for this project. All sounds are synthesized in the firmware: no samples.

The chord sets are the voicings of the Chord Set List in the Roland J-6 owner's manual (version 1.02), read out of its
table by the scripts in `tools/`. The grooves follow the manual's Phrase List: its nine styles and their variations,
its rhythm patterns as written there; the chord phrases and strums are CHORDMAN's own. Roland, J-6, Juno-106,
Prophet-6, Oberheim and Moog One are trademarks of their owners, named only to say what the engines lean on; CHORDMAN is
not affiliated with any of them.

## Built on

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | Charles Vestal | MPL-2.0 | the platform: screen, panel, audio, MIDI, USB host, storage, simulator, build tools, the Barlow fonts it carries (SIL OFL 1.1) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) | Leo Kuroshita (Hügelton Instruments) and contributors | MPL-2.0 | the hardware layer, update loader, packaging |
| JieLi's AC79 SDK | JieLi Technology | Apache-2.0 | three files in every `.fwsc` package (fetched at build time, never copied here) |
