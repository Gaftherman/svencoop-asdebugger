# Third-party notices

Project code is MIT-licensed. See the individual debugger source notices for
upstream authorship and attribution.
Dependencies retain their original source, copyright notices and licenses.

- [Metamod](https://github.com/metamod-fallguys/metamod): see
  `metamod/LICENSE` and individual source/header notices. Legacy Metamod files
  credit Will Day and contain GPL version 2-or-later notices with a Half-Life
  engine linking exception. HLSDK files contain Valve/Id notices.
- [ASExt](https://github.com/metamod-fallguys/asext): see `asext/LICENSE`
  (MIT, copyright hzqst) and individual legacy file notices.
- [cppdap](https://github.com/google/cppdap): see `cppdap` source (Apache License 2.0,
  copyright Google LLC). Provides the C++ Debug Adapter Protocol implementation.
- [nlohmann/json](https://github.com/nlohmann/json): see `nlohmann/json` source (MIT License,
  copyright Niels Lohmann). Transitive JSON serializer used by cppdap.
- [AngelScript 2.39.0](https://github.com/anjo76/angelscript/tree/v2.39.0):
  the standalone host supplies the engine and any standard add-ons. See the
  AngelScript source license and the notices in the add-on directories
  (zlib license, copyright Andreas Jönsson).
- Sven plugin dependencies include the customized AngelScript SDK, Capstone,
  procmap, RapidJSON, Chocobo1Hash and FormatValidation. Their notices remain
  in the dependency source trees fetched by Git or CMake.

Binary packages include dependency license/notice files under `build/licenses/`
and record the exact source commits in `build/dependencies.json`. These commits
identify the corresponding complete source, including per-file notices.
