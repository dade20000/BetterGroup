# 🛡️ BetterGroup

BetterGroup is a community-made TruckersMP GameClientSDK plugin that improves the visual display of staff groups in the Player Panel and chat.

> Current public test line: **1.0.0 RC3.2**

## What it does

- recolors the native staff shield according to the detected staff role;
- supports multiple staff members with different role colors;
- recolors the staff nickname in the live Player Panel;
- follows the Player Panel when it is moved;
- respects the local group visibility toggle;
- includes low-latency handling for Player Panel and chat;
- installs and updates through a Windows setup/updater flow.

## Download

Normal users should **not** compile the source.

Open the latest GitHub Release and download:

```text
BetterGroup_Setup.exe
```

That is the only file a new user needs.

Existing users can update from:

```text
Start Menu -> BetterGroup -> Check for updates
```

## Installation

`BetterGroup_Setup.exe` automatically:

1. finds Euro Truck Simulator 2;
2. finds the local TruckersMP UI installation;
3. backs up the original staff shield;
4. creates the local marker needed by BetterGroup from the user's existing TruckersMP asset;
5. installs `BetterGroup.dll`, configuration and role colors;
6. installs the updater and uninstaller;
7. adds BetterGroup shortcuts to the Start Menu.

The repository does **not** redistribute TruckersMP's original staff icon.

## Building

Requirements:

- Windows x64;
- Visual Studio 2022/2026 with Desktop C++;
- CMake.

Developer build:

```bat
BUILD.bat
```

Public package:

```bat
BUILD_PUBLIC_RELEASE.bat
```

Expected output:

```text
PUBLIC/
  BetterGroup_Setup.exe
  LEGGIMI.txt
  GITHUB_RELEASE/
    BetterGroup_Setup.exe
    BetterGroup_Update.zip
```

## Releases and updates

The updater is configured for:

```text
github_owner=dade20000
github_repo=BetterGroup
```

During RC testing it uses the prerelease channel.

Each GitHub Release should contain:

```text
BetterGroup_Setup.exe
BetterGroup_Update.zip
```

`BetterGroup_Update.zip` is consumed by the updater and contains a manifest plus SHA-256 hashes.

## Project status

This is still a release candidate. Visual edge cases around fast UI movement/chat are being tested before calling the build final.

## Disclaimer

BetterGroup is community-made and is not affiliated with or endorsed by TruckersMP.

TruckersMP names, trademarks, UI artwork and other assets belong to their respective owners.
