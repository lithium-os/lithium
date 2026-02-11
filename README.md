# Lithium

![GitHub License](https://img.shields.io/github/license/lithium-os/lithium)
![GitHub last commit](https://img.shields.io/github/last-commit/lithium-os/lithium)
![GitHub commits since latest release](https://img.shields.io/github/commits-since/lithium-os/lithium/latest)

![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/lithium-os/lithium)

Lithium is a general purpose x86_64 operating system. It's primarily developed in C with some features done in Assembly or even C++. Lithium is a continuation of and derivation of [Horizon](https://github.com/horizonos-project/horizon), a recently archived x86 OS project of similar structure and goal.

Lithium has one long-term goal as an operating system; to teach. To teach myself, other maintainers, and those interested in OS-level development. 

## Architecture Choices

This OS is x86_64, the freestanding 64-bit environment is provided by our bootloader of choice; *[Limine](https://codeberg.org/Limine/Limine)*. Limine is an excellent bootloader, but to use it here you'll need to clone the `v10.x-binary` branch. This repo provides a `v10.x` Limine header in its source code so you as the end user do not need to build Limine from scratch.

> `git clone https://codeberg.org/Limine/Limine.git --branch=v10.x-binary --depth=1 && cd limine && make`

This OS is also attempting to be mostly binary compatible with Linux. This is to avoid the impossible project of rebuilding pretty much the entire software ecosystem from scratch and makes the process of porting exponentially easier.

## Why an OS project in 202x?

For the most part, this is an educational side project. I don't have plans for this OS to really compete with already established systems like BSD, Linux, or Windows. Lithium is a tool to teach the fundamentals of operating systems and POSIX without having to build from ground-zero.

But also, I was inspired by other projects that are more progressed than this one currently is, the two biggest ones being:
 - [cavOS by MalwarePad](https://github.com/malwarepad/cavos)
 - [LemonOS by fido2020](https://github.com/LemonOSProject/LemonOS)

Both of these are *very* different, one is a traditional monolithic kernel almost fully in C, the other is a modular kernel written vastly in C++.

This project, Lithium, is following the more traditional path of a monolithic kernel mostly in C. However I do roughly know where C++ could be a net benefit, so some regions may be developed in C++. Other languages like Zig or Rust are being excluded from this project, not because of dislike for these languages, simply because... I don't know them well enough to make a project like this in them.

## How to build?

## How to Contribute?

## Using AI

## License
