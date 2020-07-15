# Exhale

Originally a fork of the [vita port][3] of [Moonlight Embedded][1], Exhale is a streaming frontend/manager for Vita.
It supports Steam and various PC emulators natively

## Documentation

More information can find [moonlight-docs][1], [moonlight-embedded][2], and [vita-moonlight][3].
If you need more help, join the #vita-help channel in [discord][4].

[1]: https://github.com/moonlight-stream/moonlight-docs/wiki
[2]: https://github.com/irtimmer/moonlight-embedded/wiki
[3]: https://github.com/xyzz/vita-moonlight/wiki
[4]: https://discord.gg/atkmxxT

# Build deps

You can install build dependencies with [ddpm](https://github.com/dolcesdk/ddpm).

# Build Moonlight

```
# if you do git pull, make sure submodules are updated first
git submodule update --init
mkdir build && cd build
cmake ..
make
```

# Assets

- Icon - [moonlight-stream][moonlight] project logo
- Livearea background - [Moonlight Reflection][reflection] Public domain

[moonlight]: https://github.com/moonlight-stream
[reflection]: http://www.publicdomainpictures.net/view-image.php?image=130014&picture=moonlight-reflection
