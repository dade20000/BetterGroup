# BetterGroup 1.0.0 RC3

RC3 unisce il renderer stabile della 0.8.8, il low-latency della RC2 e due fix
specifici per gli ultimi micro-flash.

## Fix TAB — nickname bianco mentre trascini

Durante un drag confermato il nickname NON aspetta più il controllo dell'anchor
magenta. La posizione è già aggiornata ogni frame dal delta del mouse, quindi
usa direttamente quella posizione e mantiene soltanto la maschera text-like.

Inoltre i rettangoli shield hanno 4 px di tolleranza. Vengono comunque toccati
solo pixel magenta, quindi il padding non colora il resto della UI.

## Fix CHAT — shield rosa sul nuovo messaggio

Il vecchio fallback globale era volutamente vietato in chat perché in passato
poteva dare il colore di uno staff a un altro staff.

RC3 aggiunge un fallback CHAT separato e corto:

- gli shield già riconosciuti usano SEMPRE il loro colore esatto;
- un nuovo shield magenta non ancora raggiunto dal detector può usare per max
  ~180 ms il colore dell'ultimo evento staff appena letto;
- quando TU premi Invio, BetterGroup arma per ~180 ms il tuo colore locale,
  coprendo anche il frame in cui TMP mostra il messaggio prima di averlo scritto
  fisicamente nel chat log.

Quindi non è più il vecchio fallback globale che sporcava Game Moderator /
Community Moderator.

## Release completa

Lancia:

`BUILD_RELEASE.bat`

Output:

```text
RELEASE/
  BetterGroup_Setup.exe
  BetterGroup_Update.exe
  BetterGroup_Uninstall.exe
  BetterGroup_Update.zip
```

Agli utenti mandi:
`BetterGroup_Setup.exe`

Su ogni GitHub Release carichi:
- `BetterGroup_Setup.exe`
- `BetterGroup_Update.zip`

## Update sicuro

`BetterGroup_Update.zip` contiene anche:
- updater nuovo;
- uninstaller nuovo;
- `manifest.json`;
- SHA-256 di ogni file.

L'updater:
1. legge la release GitHub;
2. scarica lo ZIP;
3. verifica manifest/versione;
4. verifica SHA-256;
5. aggiorna DLL + ruoli + versione;
6. NON sovrascrive `BetterGroup.cfg`;
7. aggiorna anche updater/uninstaller dopo la chiusura dell'updater corrente.

Repository già configurato nel client:
`dade20000/BetterGroup`

Canale RC:
`prerelease`

## GitHub automatico

È incluso:
`.github/workflows/release.yml`

Quando il repository esiste e fai push di un tag, per esempio:

`v1.0.0-rc3`

GitHub Actions compila su Windows e crea/carica automaticamente:
- `BetterGroup_Setup.exe`
- `BetterGroup_Update.zip`

Nota: gli EXE non sono firmati Authenticode, quindi SmartScreen può mostrare
"Unknown publisher".
