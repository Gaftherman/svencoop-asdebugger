# Contributing to asdebugger

**[English](CONTRIBUTING.md)** | **[Español](docs/CONTRIBUTING.es.md)**

Thank you for helping. Bug reports, fixes, documentation and examples are all welcome.

## Report a problem

Open an [issue](https://github.com/Gaftherman/asdebugger/issues) with the version you use, your operating system, the steps to reproduce it and the output you saw. A script of a few lines that reproduces the problem helps most.

## Propose a change

1. Fork the repository and create a branch from `main`.
2. Keep each change small and focused, with tests when behaviour changes.
3. Write commit messages as `type(scope): short imperative description` without a trailing period. Types: `feat`, `fix`, `test`, `perf`, `refactor`, `style`, `docs`, `chore`.
4. Open a pull request that says what changed and how you checked it.

## Build and test

Clone with submodules and build the 32-bit plugin:

~~~sh
git clone --recursive https://github.com/Gaftherman/asdebugger.git
cd asdebugger
cmake -S . -B build/x86/Release -A Win32
cmake --build build/x86/Release --config Release --parallel 4
ctest --test-dir build/x86/Release -C Release --output-on-failure
~~~

On Linux use `cmake -S . -B build/x86/Release -DCMAKE_BUILD_TYPE=Release` with GCC multilib.

## Style

Follow `.editorconfig` and the existing C++ style. Format Python with `ruff format`.

## License

By contributing you agree that your work is released under the [MIT License](LICENSE).

## Related projects

| Project | Use |
| --- | --- |
| [angelscript-dap](https://github.com/Gaftherman/angelscript-dap) | The debugger core |
| [angelscript-debugger-example](https://github.com/Gaftherman/angelscript-debugger-example) | A host that shows how to use the debugger |
| [angelscript-cli](https://github.com/Gaftherman/angelscript-cli) | Run, check and debug scripts from the terminal |
| [angelscript-lsp](https://github.com/Gaftherman/angelscript-lsp) | Editor support for VS Code |
