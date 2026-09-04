# Release flow

## 1. Build locally

Run:

```bat
BUILD_PUBLIC_RELEASE.bat
```

## 2. Upload to a GitHub Release

Create a release/tag such as:

```text
v1.0.0-rc3.2
```

Upload only:

```text
PUBLIC/GITHUB_RELEASE/BetterGroup_Setup.exe
PUBLIC/GITHUB_RELEASE/BetterGroup_Update.zip
```

## 3. What users download

New users download:

```text
BetterGroup_Setup.exe
```

Existing users normally do not manually download the update ZIP. BetterGroup's updater downloads it automatically.

## 4. Final/stable channel

When 1.0.0 is considered stable:

- change `channel=prerelease` to `channel=stable` in `setup/BetterGroup_update.ini`;
- publish the final release without marking it as prerelease.
