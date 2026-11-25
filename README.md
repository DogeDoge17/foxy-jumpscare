# foxy-jumpscare

has a 1/1000 chance every second for withered foxy to jumpscare you. this is
literally just that terraria mod but for your whole system. i optimized it for
background idling so it wont actually wake up every second because the time
until the next scare is predetermined.

## compatibility

it only works on linux and wayland compositors that implement layer-shell. so
basically anything but gnome (use case for foxy jumpscare??). i only tested it
on niri but i trust that it works elsewhere. also it cant be on windows or mac
because it uses a ton of low-level wayland code. it would basically have to be a
separate app to be multi-platform

## building

uhhh install nix and run this while in the projects working directory

```bash
nix run
```
