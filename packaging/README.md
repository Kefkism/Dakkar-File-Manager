# Dakkar — releaseverpakkingen

Deze map bevat packaging **recepten**, niet reeds gecompileerde distributiepakketten.

## Native pakketmodel

De eigen executable, bibliotheken, schema's, hulpbestanden, assets en Nemo-afkomstige interne componenten staan onder `/opt/dakkar`. Alleen deze eigen namen worden naar het systeem geëxporteerd:

- `/usr/bin/dakkar` (wrapper)
- `/usr/share/applications/org.dakkar.Files.desktop`
- `/usr/share/metainfo/org.dakkar.Files.metainfo.xml`
- `/usr/share/icons/hicolor/*/apps/org.dakkar.Files.png`

**Geen** `nemo.desktop`, `nemo` executable, `org.nemo.*` schema of Nemo D-Bus activatieservice wordt wereldwijd geïnstalleerd. Nemo blijft bestaan. `Name=Dakkar` toont het als een eigen app in desktopmenu's.

### 1. Debian / Ubuntu / Mint

Installeer de build-afhankelijkheden van de broncode (zie `README.md`) en `dpkg-dev`, dan:

```bash
./packaging/deb/build-deb.sh
sudo apt install ./_release/deb/dakkar_1.0-3_*.deb
```

**Bouw per distroversie**, omdat afhankelijkheden en ABI's verschillen. Het controlbestand bevat een basislijst van runtime-deps; controleer voor publicatie met `dpkg-shlibdeps`, `lintian`, een verse VM en daadwerkelijke start. Dit is nog geen universele `.deb`.

### 2. Arch/AUR

```bash
./packaging/aur/prepare-local.sh
cd packaging/aur
makepkg -si
```

Voor officiële AUR-publicatie moet de PKGBUILD de tag-URL van een *bestaande* GitHub-release gebruiken en de echte SHA256 daarvan. Genereer daarna `.SRCINFO` met `makepkg --printsrcinfo > .SRCINFO` en commit beide naar de AUR-repo.

### 3. Shell-installer (compileert lokaal, zonder root)

```bash
./packaging/sh/install.sh
~/.local/bin/dakkar
./packaging/sh/uninstall.sh  # optioneel
```

Dit is geen universeel uitvoerbare binary; lokaal zijn compiler en development dependencies nodig.

### 4. Flatpak (experimenteel: manifest NIET gereed voor publieke release)

```bash
flatpak install flathub org.gnome.Platform//50 org.gnome.Sdk//50
./packaging/flatpak/build-flatpak.sh
flatpak install --user ./_release/Dakkar-1.0.flatpak
```

**Bekende blocker:** de GNOME runtime levert de vereiste Cinnamon/XApp/GSF/Exempi stack niet zonder aanvullende dependency-modules. `xapp` en `cinnamon-desktop` zijn tijdelijk als onbevroren upstream Git-bronnen opgenomen; hun sub-afhankelijkheden en Flatpak-sandbox/file-operations (GVfs, mounts, portals) moeten eerst geïntegreerd en getest worden. De Flatpak-builder kan dus falen. Dit recept is een uitgangspunt, geen bewezen bouwbaar Flatpak-pakket en nog niet geschikt voor Flathub. Een bestandsbeheerder vereist bredere filesystemrechten dan een typische sandbox-app; controleer die rechten zorgvuldig.

### Identiteit versus upstream-erfgoed

`nemo-*.c` en `Nemo*` C-symbolen blijven staan als interne implementatie (inclusief copyright/licenties). Die namen zijn geen gepubliceerde app-ID. Een complete overstap naar andere publieke C-API/extension-ABI wordt bewust niet als afgewerkt voorgesteld.

### Releasecontrole

Gebruik `python3 packaging/check-identity.py` voor statische checks en test nadien Dakkar én Nemo tegelijkertijd in een schone VM. De bouwpakketten zijn in deze omgeving niet native gecompileerd. Publicatie als *geteste* release vereist een succesvolle build en rooktest per distributie.

### Dakkar-icoon

Dakkar gebruikt `org.dakkar.Files.png` voor GTK-vensters, desktoplaunchers
en AppStream-softwaremetadata. Het venstericoon zit ook ingebed als GResource,
zodat dit werkt bij een lokale build zonder geïnstalleerd icon theme.

Een lokaal `.deb`-bestand zelf behoudt normaal het standaard pakket-bestandsicoon.
Sommige grafische pakketinstallers geven in hun eigen pakketvoorvertoning
ook altijd een generiek icoon weer. Dit is door de installer bepaald en kan
niet universeel vanuit een Debian-controlbestand worden afgedwongen.
Zie `packaging/ICON-VERIFICATION.md` voor de inspectiecommando's.
