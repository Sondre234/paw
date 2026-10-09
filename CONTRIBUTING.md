# Contributing

[docs/architecture.md](docs/architecture.md) explains how the code is laid out and which
files a new action, setting, query, protocol or test touches.

## Branches

paw keeps one long-lived branch, `main`. Each change is made on a short-lived branch
off it, named for the kind of change: `feat/…`, `fix/…`, `chore/…` or `docs/…`.

```sh
git switch -c feat/<name> main
```

## Building and testing

```sh
cmake -S . -B build -G Ninja -DPAW_BUILD_COMPOSITOR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`tools/check-all.sh` does that and then repeats the suite in a second build with
AddressSanitizer, UndefinedBehaviorSanitizer and leak detection (`build-asan`); run it before
merging larger changes. `--quick` skips the sanitizer pass, `--sanitize` runs only it, and
`--repeat 20` runs every test up to 20 times, which is how flaky tests are found. A test
that only fails on a loaded machine is a bug in the test: wait for the condition through
`tests/harness.py` instead of sleeping.

`cmake --build build --target all_qmllint` lints the shell's QML (`shell/.qmllint.ini` says which
checks the context-property design makes meaningless); it should print nothing.
`tools/shell_gallery.py build OUT_DIR` saves a picture of every popup of the taskbar, in a light
and a dark theme, drawn in software and through the GPU, to look at a change to the shell with
(see [Seeing a change](docs/architecture.md#seeing-a-change)).

## Committing

Commit small and often, one self-contained change per commit, so any step can be reverted
on its own. Commit as soon as a change builds and its tests pass, and leave the tree in a
working state. Stage only the files that belong to the change (`git add <paths>`, not
`git add -A`), so unrelated work in progress stays out of it. Write a short imperative
subject line ("Group an application's windows into one taskbar button"), and mention the
GitHub issue when there is one.

Add a line to [CHANGELOG.md](CHANGELOG.md) for changes users will notice, and keep
[README.md](README.md) and [docs/features.md](docs/features.md) in step with behavior
changes (settings, bindings, messages). The `docs_consistency` and `example_snippets` tests
check that every binding action is mentioned there and that every Lua example is accepted.

## Merging

When the change is done and working, merge it into `main` with a merge commit:

```sh
git switch main
git merge --no-ff feat/<name>
```

Keep the default message ("Merge branch 'feat/<name>'"). Never amend, rebase or reset
commits that have already been pushed, and never force-push.

## Testing on hardware

The automated tests run headless. Anything that needs a real display, GPU, or input
device (`--session` on a TTY, multiple monitors, real applications) has to be checked
by hand; record what was and was not checked in [docs/verification.md](docs/verification.md).

## Keeping private details out

`origin` (github.com/Sondre234/paw) is public. Do not commit LAN addresses, host
keys, passwords, home-directory paths, or machine-specific access details. Personal
key bindings and themes belong in `~/.config/paw`, not in the repository.

## Lua settings

The settings the configuration accepts are listed in `src/config_schema.cpp`; the parser
takes its allowed names from that list, so a new setting or table key needs an entry there
(type, default, an example value, its range) as well as code in `src/config.cpp`. Then
regenerate the reference with `PAW_UPDATE_DOCS=1 ctest --test-dir build -R config` and
commit `docs/config-reference.md`; the `config_diagnostics` test fails while they differ. A
new action name goes in the action table in `src/config.cpp`, which the reference reads.
