# Lithium

![GitHub License](https://img.shields.io/github/license/lithium-os/lithium)
![GitHub last commit](https://img.shields.io/github/last-commit/lithium-os/lithium)
![GitHub commits since latest release](https://img.shields.io/github/commits-since/lithium-os/lithium/latest)

Lithium is a general purpose x86_64 operating system. It's primarily developed in C with some features done in Assembly or even C++. Lithium is a continuation of and derivation of [Horizon](https://github.com/horizonos-project/horizon), a recently archived x86 OS project of similar structure and goal.

Lithium has one long-term goal as an operating system; to teach. To teach myself, other maintainers, and those interested in OS-level development. 

## Architecture Choices

This OS is x86_64, the freestanding 64-bit environment is provided by our bootloader of choice; *[Limine](https://codeberg.org/Limine/Limine)*. Limine is an excellent bootloader, but to use it here you'll need to clone the `v10.x-binary` branch. This repo provides a `v10.x` Limine header in its source code so you as the end user do not need to build Limine from scratch.

> `git clone https://codeberg.org/Limine/Limine.git --branch=v10.x-binary --depth=1`

This OS is also attempting to be mostly binary compatible with Linux. This is to avoid the impossible project of rebuilding pretty much the entire software ecosystem from scratch and makes the process of porting exponentially easier.
