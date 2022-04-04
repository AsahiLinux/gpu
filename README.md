# Asahi GPU 

Research for an open source graphics stack for Apple M1.

As development of a Mesa driver has begun, development work has moved in-tree in [Mesa](https://gitlab.freedesktop.org/mesa/mesa/). As such this repository is no longer in use. 

## wrap

Build with the included makefile `make wrap.dylib`, and insert in any Metal application by setting the environment variable `DYLD_INSERT_LIBRARIES=/Users/bloom/gpu/wrap.dylib`.

## Contributors

* Alyssa Rosenzweig (`bloom`) on IRC, working on the command stream and ISA
* marcan, working on kernel side

## Contributing

All contributors are expected to abide by our [Code of Conduct](https://asahilinux.org/code-of-conduct) and our [Copyright and Reverse Engineering Policy](https://asahilinux.org/copyright).

For more information, please see our [Contributing](https://asahilinux.org/contribute/) page.

