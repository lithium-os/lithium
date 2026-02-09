# Lithium

Lithium is a general purpose x86_64 operating system. It's primarily developed in C with some features done in Assembly or even C++. Lithium is a continuation of and derivation of [Horizon](https://github.com/horizonos-project/horizon), a recently archived x86 OS project of similar structure and goal.

Lithium has one long-term goal as an operating system; to teach. To teach myself, other maintainers, and those interested in OS-level development. 

## Architecture Choices

This OS is x86_64, the freestanding 64-bit environment is provided by the bootloader of choice; *[Limine]()https://codeberg.org/Limine/Limine*. Limine is an excellent bootloader, but to use it here you'll need to clone the `v10.x-binary` branch.

> `git clone https://codeberg.org/Limine/Limine.git --branch=v10.x-binary --depth=1`

This OS is also attempting to be mostly binary compatible with Linux.