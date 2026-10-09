# Wayland protocol definitions

Vendored from the wlroots **wlr-protocols** project on 2026-09-23:
https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/tree/master/unstable

The XML files include their upstream copyright and permissive license notices.
Keep those notices in source and installed copies. CMake generates client/server
bindings with wayland-scanner; generated code is not checked into Git.

The compositor advertises layer-shell version 4 and foreign-toplevel-management
version 3. Clients must bind no higher than the advertised version.

Source SHA-256 checksums:

```text
87e0b9c837aecd6977f76f3c47d73088b7159871f5d979dc1840f6cadb5e2ed8  wlr-layer-shell-unstable-v1.xml
4ecc4588858e29fe680a33521e1f22bcf22071d66d1553fe96b4ddec03d591d2  wlr-foreign-toplevel-management-unstable-v1.xml
65b0f82a6cf129bf1a1c31a2428795abd33886c15ddd5f3ad97e5922d7bdc3a7  wlr-output-management-unstable-v1.xml
3ff6d540be0bc5228195bf072bde42117ea17945a5c2061add5d3cf97d6bb524  wlr-virtual-pointer-unstable-v1.xml
7ad7870003ecd592cae47dc19d277a609b7f18fd7b7be012623cf3225a7294f5  virtual-keyboard-unstable-v1.xml
7ebd98f3449d246a57829e4b4dd9fbc3ef98e3dd42fa94ea102f14f490eb20de  wlr-output-power-management-unstable-v1.xml
```

`wlr-output-power-management-unstable-v1.xml`, which wlopm and idle daemons use to turn monitors
off and on, was vendored from the same upstream on 2026-10-08.

The virtual pointer and virtual keyboard protocols are used only by the test client
`tests/pointer_probe.c`. `virtual-keyboard-unstable-v1.xml` is the copy in the
wayland-protocols-misc crate (from the same upstream), vendored on 2026-09-28.

`input-method-unstable-v2.xml`, which input methods such as fcitx5 and ibus speak, is the copy in
wlroots 0.20.2 (`protocol/`), which implements it, vendored on 2026-10-08 (SHA-256
`aaf68b2aec481f507a4b915ab7c624bf676495ebb2030eaf207a9c8bcac7630d`). It is used only by the test
client `tests/input_method_probe.c`; text-input-unstable-v3, the applications' side, comes from
wayland-protocols.

`paw-window-control-v1.xml` is paw's own: the shell's window menu moves a window it
knows by its foreign-toplevel handle to another workspace or output, makes it sticky or floats
it, and hears where it is (see [docs/architecture.md](../docs/architecture.md#naming-a-window-from-the-shell)).
