# foxy-jumpscare

has a 1/10000 chance every second for withered foxy to jumpscare you. this is
literally just that terraria mod but for your whole system. i optimized it for
background idling so it wont actually wake up every second because the time
until the next scare is predetermined.

## usage

run it and it will sleep in the background until scary time. if you want more of
less scary time you can adjust the chance.

```bash
foxy-jumpscare [chance]
```

if you enter 100, it will be a 1/100 chance. if you enter 1, it will execute
every time. if you enter anything <=0 or a non number, it will default to 10000.

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
