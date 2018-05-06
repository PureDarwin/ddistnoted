# ddistnoted

The `distnoted` daemon on macOS is responsible for passing distributed notifications between tasks. `ddistnoted` is a reimplementation of `distnoted` for the [PureDarwin](http://www.puredarwin.org) project.

#### Prerequisits

The current implementation of `ddistnoted` -- along with the `waitdnot` and `postdnot` tools -- use `CFMachPort`. You will need a version of CoreFoundation which has fixed versions of these, such as [this one](https://github.com/sjc/CoreFoundation).

Distributed notifications -- like most Darwin IPC -- required access to the bootstrap name server, which is setup and managed by `launchd`.

#### Instalation

`ddistnoted` can be copied anywhere, but I'd suggest `/usr/sbin` to match Apple's placement, and to match the path in the provided launchd plist.

The launchd plist `org.puredarwin.ddistnoted.plist` should be copied to `/System/Library/LaunchDaemons/`. Be sure to check that its ownership is set to `root:wheel`.

The `waitdnot` and `postdnot` tools can be copied anywhere, if you need them. `/usr/local/bin/` would be a good choice.

#### Known Issues

* If started by launchd, `ddistnoted` will be terminated and relaunched every few seconds. This is probably due to being incorrectly implemented as a daemon. This will be fixed soon.
