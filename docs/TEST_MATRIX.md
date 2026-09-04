# BetterGroup Test Matrix

## Player Panel
- [ ] Local staff shield uses the correct role color.
- [ ] Local staff nickname uses the correct role color.
- [ ] Moving the Player Panel anywhere keeps shield/nickname colored.
- [ ] Holding and dragging does not produce visible white/pink flashes.
- [ ] Scrolling the Player Panel does not corrupt unrelated UI.
- [ ] `/toggle-group` OFF removes local group styling.
- [ ] `/toggle-group` ON restores it.

## Multiple staff
- [ ] Community Moderator + Game Moderator keep separate colors.
- [ ] Report Moderator keeps its own color.
- [ ] Three or more visible staff do not steal colors.
- [ ] VTC tags / unusual usernames do not break mapping.

## Chat
- [ ] Local staff message appears with correct shield color.
- [ ] Second staff message keeps its own role color.
- [ ] Multiple staff messages visible together remain correct.
- [ ] New message does not visibly flash magenta.
- [ ] Open/close chat does not visibly flash magenta.
- [ ] Scroll chat does not visibly flash magenta.

## Stability
- [ ] Join server.
- [ ] Reconnect.
- [ ] Change area with many players.
- [ ] 30+ minutes runtime without a crash.

## Installer / updater
- [ ] Fresh install.
- [ ] Reinstall over an existing BetterGroup install.
- [ ] Original TMP shield backup created.
- [ ] Uninstall restores original shield.
- [ ] Manual update works.
- [ ] Silent automatic check does not interrupt the user when already up to date.
- [ ] SHA-256 manifest verification rejects a modified update package.
