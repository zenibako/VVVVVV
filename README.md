# VVVVVV for Analogue Pocket

This fork adds an [openfpgaOS](https://github.com/zenibako/openfpgaCore) port
of VVVVVV. The openfpgaOS port code, builds, benchmarks and packaging live
in [`openfpga/`](openfpga/). The shared C++ engine and game resources remain in
[`desktop_version/`](desktop_version/) to follow upstream's layout.

- [Build, test and install the Pocket port](openfpga/README.md)
- [Benchmark profiles, collection and comparison](openfpga/BENCHMARKING.md)
- [Port plan and progress](https://github.com/zenibako/VVVVVV-pocket/issues/1)
- [Upstream VVVVVV](https://github.com/TerryCavanagh/VVVVVV)

You must supply your own `data.zip`; no commercial game data is included.
This is an altered version of VVVVVV, copyright Terry Cavanagh. See
[LICENSE.md](LICENSE.md) for the terms and required attribution.

---

## Upstream README

![logo](logo.gif "VVVVVV")

This is the source code to VVVVVV, the 2010 indie game by [Terry Cavanagh](http://distractionware.com/), with music by [Magnus Pålsson](http://souleye.madtracker.net/). You can read the [announcement](http://distractionware.com/blog/2020/01/vvvvvv-is-now-open-source/) of the source code release on Terry's blog!

The source code for the desktop version is in [this folder](desktop_version).

VVVVVV is still commercially available at [thelettervsixtim.es](https://thelettervsixtim.es/) if you'd like to support it, but you are completely free to compile the game for your own personal use. If you're interested in distributing a compiled version of the game, see [LICENSE.md](LICENSE.md) for more information.

Discussion about VVVVVV updates mainly happens on the "unofficial" [VVVVVV discord](https://discord.gg/Zf7Nzea), in the `vvvvvv-code` channel.

Credits
-------
- Created by [Terry Cavanagh](http://distractionware.com/)
- Room Names by [Bennett Foddy](http://www.foddy.net)
- Music by [Magnus Pålsson](https://magnuspalsson.com/)
- Metal Soundtrack by [FamilyJules](https://link.space/@familyjules)
- 2.0 Update (C++ Port) by [Simon Roth](http://www.machinestudios.co.uk)
- 2.2 Update (SDL2/PhysicsFS/Steamworks port) by [Ethan Lee](http://www.flibitijibibo.com/)
- Additional coding by [Misa Kai](https://infoteddy.info/)
- Beta Testing by Sam Kaplan and Pauli Kohberger
- Ending Picture by Pauli Kohberger
- Localisations by [our localisation teams](desktop_version/TRANSLATORS.txt)
- With additional contributions by [many others here on github](desktop_version/CONTRIBUTORS.txt) <3
