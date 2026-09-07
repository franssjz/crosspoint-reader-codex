(() => {
  const LANGUAGE_KEY = "cprToolsLanguage";
  const THEME_KEY = "cprToolsTheme";
  const LEGACY_LANGUAGE_KEY = "cprStatsLanguage";
  const LEGACY_THEME_KEY = "cprStatsTheme";
  const languages = [
    ["be", "Беларуская"],
    ["ca", "Català"],
    ["cs", "Čeština"],
    ["da", "Dansk"],
    ["nl", "Nederlands"],
    ["en", "English"],
    ["fi", "Suomi"],
    ["fr", "Français"],
    ["de", "Deutsch"],
    ["hu", "Magyar"],
    ["it", "Italiano"],
    ["kk", "Қазақша"],
    ["lt", "Lietuvių"],
    ["pl", "Polski"],
    ["pt", "Português (Brasil)"],
    ["ro", "Română"],
    ["ru", "Русский"],
    ["si", "Slovenščina"],
    ["es", "Español"],
    ["sv", "Svenska"],
    ["tr", "Türkçe"],
    ["uk", "Українська"],
    ["vi", "Tiếng Việt"]
  ];
  const locales = {
    be: "be",
    ca: "ca",
    cs: "cs",
    da: "da",
    nl: "nl",
    en: "en",
    fi: "fi",
    fr: "fr",
    de: "de",
    hu: "hu",
    it: "it",
    kk: "kk",
    lt: "lt",
    pl: "pl",
    pt: "pt-BR",
    ro: "ro",
    ru: "ru",
    si: "sl",
    es: "es",
    sv: "sv",
    tr: "tr",
    uk: "uk",
    vi: "vi"
  };
  const messages = {
    en: {
      navHome: "Home",
      navStats: "Reading Stats Editor",
      navFlash: "Auto Flash",
      navTools: "Tools",
      navGithub: "GitHub",
      navSupport: "Support",
      darkMode: "Dark mode",
      lightMode: "Light mode",
      homeTitle: "CPR-vCodex Tools",
      homeSubtitle: "Small browser tools for CPR-vCodex. Everything runs locally in your browser.",
      latestFirmware: "Latest firmware: 1.5.0.24-cpr-vcodex",
      homeFlash: "Auto Flash",
      homeStats: "Reading Stats Editor",
      homeTools: "Tools",
      homeGithub: "GitHub",
      toolsTitle: "Tools",
      toolsSubtitle: "Useful browser tools for CPR-vCodex plus external utilities that fit the Xteink workflow.",
      toolsStatsTitle: "CPR-vCodex Reading Stats Editor",
      toolsStatsBody: "Local editor for exported reading stats. No upload, no server.",
      toolsOpenEditor: "Open editor",
      toolsRecommendations: "Recommendations",
      toolsEpubkitBody: "Best tool to optimize EPUB files for Xteink devices.",
      toolsXtcjsBody: "Best tool to optimize CBZ/CBR comics and manga for Xteink devices.",
      toolsWallpaperConverterBody: "Create your own adapted wallpapers.",
      toolsWallpaperCollectionBody: "Collection of adapted wallpapers.",
      toolsOpenEpubkit: "Open epubkit.ink",
      toolsOpenXtcjs: "Open xtcjs.app",
      toolsOpenConverter: "Open converter",
      toolsOpenCollection: "Open collection",
      flashEyebrow: "Browser-based installer",
      flashTitle: "Flash CPR-vCodex",
      flashSubtitle: "Install the latest CPR-vCodex firmware on your Xteink X3 or X4 from Chrome or Edge using Web Serial. It writes the inactive app partition and then switches boot to it.",
      beforeFlash: "Before you flash",
      backupTitle: "Back up first",
      backupText: "Skip this step if you have flashed before. If this is your first time, keep a \"Save Full Flash\" backup from https://xteink.dve.al/.",
      cableTitle: "Use a data USB-C cable",
      cableText: "Connect the Xteink X3 or X4 to your computer. If no serial port appears, wake and unlock the device, then reconnect it.",
      browserTitle: "Use Chrome or Edge on desktop",
      browserText: "Firefox and Safari do not support Web Serial. The browser will ask you to choose the ESP32-C3 USB serial port.",
      unplugTitle: "Do not unplug while flashing",
      unplugText: "The page validates the partition table, writes the inactive OTA app slot, updates <code>otadata</code>, and resets the device when it finishes.",
      latestPackage: "Latest firmware package",
      deviceModelTitle: "Device model",
      deviceModelText: "Choose the exact model before flashing. X3 and X4 use different OTA app partition sizes.",
      deviceX4: "Xteink X4",
      deviceX3: "Xteink X3",
      deviceX4Pro: "Xteink X4 Pro",
      deviceX4ProStatus: "Not supported · ESP32-S3",
      deviceX4ProUnsupported: "X4 Pro is not compatible with the current ESP32-C3 firmware. It needs a separate ESP32-S3 build; do not select X4 for an X4 Pro.",
      firmwareSource: "Source",
      firmwareAsset: "Asset",
      firmwareSize: "Size",
      firmwareSha256: "SHA-256",
      firmwareSourceUnknown: "Unknown",
      firmwareSizeUnknown: "Unknown size",
      flashWarning: "Use at your own risk. Flashing custom firmware can fail if the cable disconnects or the device loses power.",
      unsupported: "Web Serial is not available in this browser. Use Chrome or Edge on a desktop computer, or use the manual download below.",
      flashButton: "Flash CPR-vCodex Firmware",
      flashButtonForDevice: "Flash CPR-vCodex Firmware for {device}",
      flashing: "Flashing...",
      readyStatus: "Ready. Clicking Flash will open the browser serial device chooser.",
      choosePort: "Choose the ESP32-C3 serial port in the browser prompt.",
      openingConnection: "Opening bootloader connection...",
      partitionOk: "{device} partition table found.",
      fetchingFirmware: "Fetching firmware package...",
      firmwareDownloadFailed: "Firmware download failed",
      firmwareTooLarge: "Firmware is too large for the {device} app partition: {size} downloaded, {max} max.",
      firmwareTooSmall: "Firmware looks too small. Refusing to flash a suspicious file.",
      firmwareSizeMismatch: "Firmware size mismatch. Expected {expected}, downloaded {actual}.",
      firmwareHashUnavailable: "This browser cannot verify the firmware checksum.",
      firmwareHashMismatch: "Firmware checksum mismatch. Refusing to flash a file that differs from the published release.",
      currentBootTarget: "Current boot: {current}. Next flash target: {target}.",
      writingOffset: "Writing to offset {offset}...",
      partitionUnexpected: "Unexpected partition configuration for {device}. Select the right device model and try again.",
      partitionUnsupportedX4Pro: "Xteink X4 Pro detected. CPR-vCodex does not currently publish an ESP32-S3 X4 Pro firmware. Nothing was written.",
      unknownFlashError: "Unknown flash error",
      flashComplete: "Flash complete. The device should reboot into CPR-vCodex.",
      flashStopped: "Flash stopped. Check the message above before trying again.",
      downloadBin: "Download .bin",
      communityFlasher: "Community flasher",
      flashNote: "The automatic flasher uses a local firmware copy synchronized from the latest published GitHub release.",
      manualTitle: "Manual PlatformIO flash",
      manualText: "For development builds, clone the repo, set the upload port, then use PlatformIO.",
      stepPending: "Pending",
      stepRunning: "Running",
      stepDone: "Done",
      stepFailed: "Failed",
      stepConnect: "Connect to device",
      stepPartition: "Validate partition table",
      stepDownload: "Download firmware",
      stepReadOtadata: "Read otadata partition",
      stepFlashApp: "Flash app partition",
      stepFlashOtadata: "Flash otadata partition",
      stepReset: "Reset device"
    },
    es: {
      navHome: "Inicio",
      navStats: "Editor de estadísticas",
      navFlash: "Auto Flash",
      navTools: "Herramientas",
      navGithub: "GitHub",
      navSupport: "Soporte",
      darkMode: "Modo oscuro",
      lightMode: "Modo claro",
      homeTitle: "Herramientas CPR-vCodex",
      homeSubtitle: "Pequeñas herramientas de navegador para CPR-vCodex. Todo se ejecuta localmente en tu navegador.",
      latestFirmware: "Último firmware: 1.5.0.24-cpr-vcodex",
      homeFlash: "Auto Flash",
      homeStats: "Editor de estadísticas",
      homeTools: "Herramientas",
      homeGithub: "GitHub",
      toolsTitle: "Herramientas",
      toolsSubtitle: "Herramientas de navegador para CPR-vCodex y utilidades externas útiles para el flujo de trabajo Xteink.",
      toolsStatsTitle: "Editor de estadísticas de lectura CPR-vCodex",
      toolsStatsBody: "Editor local para estadísticas de lectura exportadas. Sin subida, sin servidor.",
      toolsOpenEditor: "Abrir editor",
      toolsRecommendations: "Recomendaciones",
      toolsEpubkitBody: "Mejor herramienta para optimizar EPUB para dispositivos Xteink.",
      toolsXtcjsBody: "Mejor herramienta para optimizar cómics y manga CBZ/CBR para dispositivos Xteink.",
      toolsWallpaperConverterBody: "Crea tus propios fondos adaptados.",
      toolsWallpaperCollectionBody: "Colección de fondos adaptados.",
      toolsOpenEpubkit: "Abrir epubkit.ink",
      toolsOpenXtcjs: "Abrir xtcjs.app",
      toolsOpenConverter: "Abrir conversor",
      toolsOpenCollection: "Abrir colección",
      flashEyebrow: "Instalador desde navegador",
      flashTitle: "Flashear CPR-vCodex",
      flashSubtitle: "Instala el último firmware CPR-vCodex en tu Xteink X3 o X4 desde Chrome o Edge usando Web Serial. Escribe la partición de app inactiva y luego arranca desde ella.",
      beforeFlash: "Antes de flashear",
      backupTitle: "Haz copia primero",
      backupText: "Ignora este paso si ya has flasheado otras veces. Si es la primera vez, recuerda tener un \"Save Full Flash\" en https://xteink.dve.al/.",
      cableTitle: "Usa un cable USB-C de datos",
      cableText: "Conecta el Xteink X3 o X4 al ordenador. Si no aparece el puerto serie, despierta y desbloquea el dispositivo y vuelve a conectarlo.",
      browserTitle: "Usa Chrome o Edge en escritorio",
      browserText: "Firefox y Safari no soportan Web Serial. El navegador te pedirá elegir el puerto USB serie ESP32-C3.",
      unplugTitle: "No desconectes durante el flasheo",
      unplugText: "La página valida la tabla de particiones, escribe el slot OTA inactivo, actualiza <code>otadata</code> y reinicia el dispositivo al terminar.",
      latestPackage: "Último paquete de firmware",
      deviceModelTitle: "Modelo de dispositivo",
      deviceModelText: "Elige el modelo exacto antes de flashear. X3 y X4 usan tamaños distintos de partición OTA de app.",
      deviceX4: "Xteink X4",
      deviceX3: "Xteink X3",
      firmwareSource: "Origen",
      firmwareAsset: "Asset",
      firmwareSize: "Tamaño",
      firmwareSha256: "SHA-256",
      firmwareSourceUnknown: "Desconocido",
      firmwareSizeUnknown: "Tamaño desconocido",
      flashWarning: "Úsalo bajo tu responsabilidad. Flashear firmware personalizado puede fallar si se desconecta el cable o el dispositivo pierde energía.",
      unsupported: "Web Serial no está disponible en este navegador. Usa Chrome o Edge en un ordenador de escritorio, o descarga el binario manualmente.",
      flashButton: "Flashear firmware CPR-vCodex",
      flashButtonForDevice: "Flashear firmware CPR-vCodex para {device}",
      flashing: "Flasheando...",
      readyStatus: "Listo. Al pulsar Flash se abrirá el selector de dispositivo serie del navegador.",
      choosePort: "Elige el puerto serie ESP32-C3 en el aviso del navegador.",
      openingConnection: "Abriendo conexión con el bootloader...",
      partitionOk: "Tabla de particiones de {device} detectada.",
      fetchingFirmware: "Descargando paquete de firmware...",
      firmwareDownloadFailed: "Falló la descarga del firmware",
      firmwareTooLarge: "El firmware es demasiado grande para la partición de app de {device}: {size} descargados, {max} máximo.",
      firmwareTooSmall: "El firmware parece demasiado pequeño. No se flasheará un archivo sospechoso.",
      firmwareSizeMismatch: "El tamaño del firmware no coincide. Esperado: {expected}; descargado: {actual}.",
      firmwareHashUnavailable: "Este navegador no puede verificar la suma de comprobación del firmware.",
      firmwareHashMismatch: "La suma de comprobación del firmware no coincide. No se flasheará un archivo distinto a la release publicada.",
      currentBootTarget: "Arranque actual: {current}. Próximo destino de flasheo: {target}.",
      writingOffset: "Escribiendo en offset {offset}...",
      partitionUnexpected: "Configuración de particiones inesperada para {device}. Elige el modelo correcto y vuelve a intentarlo.",
      unknownFlashError: "Error de flasheo desconocido",
      flashComplete: "Flasheo completado. El dispositivo debería reiniciar en CPR-vCodex.",
      flashStopped: "Flasheo detenido. Revisa el mensaje anterior antes de intentarlo de nuevo.",
      downloadBin: "Descargar .bin",
      communityFlasher: "Flasher comunitario",
      flashNote: "El flasher automático usa una copia local sincronizada desde la última release publicada en GitHub.",
      manualTitle: "Flasheo manual con PlatformIO",
      manualText: "Para builds de desarrollo, clona el repo, configura el puerto de subida y usa PlatformIO.",
      stepPending: "Pendiente",
      stepRunning: "En curso",
      stepDone: "Hecho",
      stepFailed: "Error",
      stepConnect: "Conectar al dispositivo",
      stepPartition: "Validar tabla de particiones",
      stepDownload: "Descargar firmware",
      stepReadOtadata: "Leer partición otadata",
      stepFlashApp: "Flashear partición app",
      stepFlashOtadata: "Flashear partición otadata",
      stepReset: "Reiniciar dispositivo"
    }
  };

  Object.assign(messages, {
    ca: {
      navHome: "Inici", navStats: "Editor d'estadístiques", darkMode: "Mode fosc", lightMode: "Mode clar",
      homeSubtitle: "Petites eines de navegador per a CPR-vCodex. Tot s'executa localment al navegador.", latestFirmware: "Últim firmware: 1.5.0.24-cpr-vcodex", homeStats: "Editor d'estadístiques",
      flashEyebrow: "Instal·lador des del navegador", flashTitle: "Flasheja CPR-vCodex", flashSubtitle: "Instal·la l'últim firmware CPR-vCodex al teu Xteink X3/X4 des de Chrome o Edge amb Web Serial. Escriu la partició d'app inactiva i després hi canvia l'arrencada.",
      beforeFlash: "Abans de flashejar", backupTitle: "Fes una còpia abans", backupText: "Aquest flasheig tipus OTA conserva el bootloader, la taula de particions, la targeta SD i la configuració NVS, però copiar la SD continua sent prudent.",
      cableTitle: "Fes servir un cable USB-C de dades", cableText: "Connecta l'Xteink X3/X4 a l'ordinador. Si no apareix cap port sèrie, desperta i desbloqueja el dispositiu i torna'l a connectar.",
      browserTitle: "Fes servir Chrome o Edge d'escriptori", browserText: "Firefox i Safari no suporten Web Serial. El navegador et demanarà triar el port USB sèrie ESP32-C3.",
      unplugTitle: "No desconnectis durant el flasheig", unplugText: "La pàgina valida la taula de particions, escriu el slot OTA inactiu, actualitza <code>otadata</code> i reinicia el dispositiu quan acaba.",
      latestPackage: "Últim paquet de firmware", flashWarning: "Fes-ho sota la teva responsabilitat. Flashejar firmware personalitzat pot fallar si el cable es desconnecta o el dispositiu perd alimentació.",
      unsupported: "Web Serial no està disponible en aquest navegador. Fes servir Chrome o Edge en un ordinador d'escriptori, o descarrega el binari manualment.",
      flashButton: "Flasheja el firmware CPR-vCodex", flashing: "Flashejant...", readyStatus: "A punt. En prémer Flash s'obrirà el selector de port sèrie del navegador.", choosePort: "Tria el port sèrie ESP32-C3 a l'avís del navegador.",
      openingConnection: "Obrint connexió amb el bootloader...", partitionOk: "S'ha trobat la taula de particions OTA CrossPoint/Xteink per defecte.", fetchingFirmware: "Descarregant paquet de firmware...",
      firmwareDownloadFailed: "Ha fallat la descàrrega del firmware", firmwareTooLarge: "El firmware és massa gran per a la partició d'app: {size}.", firmwareTooSmall: "El firmware sembla massa petit. No es flashejarà un fitxer sospitós.",
      currentBootTarget: "Arrencada actual: {current}. Proper destí de flasheig: {target}.", writingOffset: "Escrivint a l'offset {offset}...", partitionUnexpected: "Configuració de particions inesperada. Aquest flasher de navegador només suporta dispositius amb la taula OTA CrossPoint/Xteink per defecte.", unknownFlashError: "Error de flasheig desconegut",
      flashComplete: "Flasheig completat. El dispositiu hauria de reiniciar amb CPR-vCodex.", flashStopped: "Flasheig aturat. Revisa el missatge anterior abans de tornar-ho a intentar.", downloadBin: "Descarrega .bin", communityFlasher: "Flasher comunitari",
      flashNote: "El flasher automàtic usa la còpia del firmware publicada amb aquesta pàgina. El botó de descàrrega usa l'asset de la release de GitHub.",
      manualTitle: "Flasheig manual amb PlatformIO", manualText: "Per a builds de desenvolupament, clona el repo, configura el port de pujada i usa PlatformIO.",
      stepPending: "Pendent", stepRunning: "En curs", stepDone: "Fet", stepFailed: "Error", stepConnect: "Connectar al dispositiu", stepPartition: "Validar taula de particions", stepDownload: "Descarregar firmware", stepReadOtadata: "Llegir partició otadata", stepFlashApp: "Flashejar partició app", stepFlashOtadata: "Flashejar partició otadata", stepReset: "Reiniciar dispositiu"
    },
    fr: {
      navHome: "Accueil", navStats: "Éditeur de statistiques", darkMode: "Mode sombre", lightMode: "Mode clair",
      homeSubtitle: "Petits outils de navigateur pour CPR-vCodex. Tout s'exécute localement dans votre navigateur.", latestFirmware: "Dernier firmware : 1.5.0.24-cpr-vcodex", homeStats: "Éditeur de statistiques",
      flashEyebrow: "Installateur dans le navigateur", flashTitle: "Flasher CPR-vCodex", flashSubtitle: "Installez le dernier firmware CPR-vCodex sur votre Xteink X3/X4 depuis Chrome ou Edge avec Web Serial. Il écrit la partition d'application inactive puis bascule le démarrage dessus.",
      beforeFlash: "Avant de flasher", backupTitle: "Sauvegardez d'abord", backupText: "Ce flash de type OTA préserve le bootloader, la table de partitions, la carte SD et les réglages NVS, mais sauvegarder la SD reste prudent.",
      cableTitle: "Utilisez un câble USB-C de données", cableText: "Connectez le Xteink X3/X4 à l'ordinateur. Si aucun port série n'apparaît, réveillez et déverrouillez l'appareil, puis reconnectez-le.",
      browserTitle: "Utilisez Chrome ou Edge sur ordinateur", browserText: "Firefox et Safari ne prennent pas en charge Web Serial. Le navigateur vous demandera de choisir le port série USB ESP32-C3.",
      unplugTitle: "Ne débranchez pas pendant le flash", unplugText: "La page valide la table de partitions, écrit le slot OTA inactif, met à jour <code>otadata</code> et réinitialise l'appareil à la fin.",
      latestPackage: "Dernier paquet firmware", flashWarning: "À vos risques. Flasher un firmware personnalisé peut échouer si le câble se déconnecte ou si l'appareil perd l'alimentation.",
      unsupported: "Web Serial n'est pas disponible dans ce navigateur. Utilisez Chrome ou Edge sur ordinateur, ou téléchargez le binaire manuellement.",
      flashButton: "Flasher le firmware CPR-vCodex", flashing: "Flash en cours...", readyStatus: "Prêt. Cliquer sur Flash ouvrira le sélecteur de port série du navigateur.", choosePort: "Choisissez le port série ESP32-C3 dans la fenêtre du navigateur.",
      openingConnection: "Ouverture de la connexion au bootloader...", partitionOk: "Table de partitions OTA CrossPoint/Xteink par défaut détectée.", fetchingFirmware: "Téléchargement du paquet firmware...",
      firmwareDownloadFailed: "Échec du téléchargement du firmware", firmwareTooLarge: "Le firmware est trop grand pour la partition d'application : {size}.", firmwareTooSmall: "Le firmware semble trop petit. Refus de flasher un fichier suspect.",
      currentBootTarget: "Démarrage actuel : {current}. Prochaine cible : {target}.", writingOffset: "Écriture à l'offset {offset}...", partitionUnexpected: "Configuration de partitions inattendue. Ce flasher navigateur ne prend en charge que la table OTA CrossPoint/Xteink par défaut.", unknownFlashError: "Erreur de flash inconnue",
      flashComplete: "Flash terminé. L'appareil devrait redémarrer sur CPR-vCodex.", flashStopped: "Flash arrêté. Vérifiez le message ci-dessus avant de réessayer.", downloadBin: "Télécharger .bin", communityFlasher: "Flasher communautaire",
      flashNote: "Le flasher automatique utilise la copie du firmware publiée avec cette page. Le bouton de téléchargement utilise l'asset de la release GitHub.",
      manualTitle: "Flash manuel avec PlatformIO", manualText: "Pour les builds de développement, clonez le dépôt, configurez le port d'envoi, puis utilisez PlatformIO.",
      stepPending: "En attente", stepRunning: "En cours", stepDone: "Terminé", stepFailed: "Échec", stepConnect: "Connecter l'appareil", stepPartition: "Valider la table de partitions", stepDownload: "Télécharger le firmware", stepReadOtadata: "Lire la partition otadata", stepFlashApp: "Flasher la partition app", stepFlashOtadata: "Flasher la partition otadata", stepReset: "Réinitialiser l'appareil"
    },
    de: {
      navHome: "Start", navStats: "Statistik-Editor", darkMode: "Dunkler Modus", lightMode: "Heller Modus",
      homeSubtitle: "Kleine Browser-Tools für CPR-vCodex. Alles läuft lokal in Ihrem Browser.", latestFirmware: "Neueste Firmware: 1.5.0.24-cpr-vcodex", homeStats: "Statistik-Editor",
      flashEyebrow: "Browserbasierter Installer", flashTitle: "CPR-vCodex flashen", flashSubtitle: "Installieren Sie die neueste CPR-vCodex-Firmware auf Ihrem Xteink X3/X4 mit Chrome oder Edge und Web Serial. Die inaktive App-Partition wird beschrieben und anschließend als Bootziel gesetzt.",
      beforeFlash: "Vor dem Flashen", backupTitle: "Zuerst sichern", backupText: "Dieser OTA-artige Flash erhält Bootloader, Partitionstabelle, SD-Karte und NVS-Einstellungen, eine SD-Sicherung ist dennoch sinnvoll.",
      cableTitle: "USB-C-Datenkabel verwenden", cableText: "Verbinden Sie den Xteink X3/X4 mit dem Computer. Wenn kein serieller Port erscheint, Gerät aufwecken und entsperren, dann erneut verbinden.",
      browserTitle: "Chrome oder Edge am Desktop verwenden", browserText: "Firefox und Safari unterstützen Web Serial nicht. Der Browser fragt nach dem ESP32-C3-USB-Seriellport.",
      unplugTitle: "Während des Flashens nicht trennen", unplugText: "Die Seite prüft die Partitionstabelle, schreibt den inaktiven OTA-App-Slot, aktualisiert <code>otadata</code> und setzt das Gerät danach zurück.",
      latestPackage: "Neuestes Firmware-Paket", flashWarning: "Benutzung auf eigenes Risiko. Custom-Firmware kann fehlschlagen, wenn das Kabel getrennt wird oder das Gerät die Stromversorgung verliert.",
      unsupported: "Web Serial ist in diesem Browser nicht verfügbar. Verwenden Sie Chrome oder Edge auf einem Desktop-Computer oder laden Sie die Binärdatei manuell herunter.",
      flashButton: "CPR-vCodex-Firmware flashen", flashing: "Flashe...", readyStatus: "Bereit. Ein Klick auf Flash öffnet die serielle Geräteauswahl des Browsers.", choosePort: "Wählen Sie im Browserdialog den ESP32-C3-Seriellport.",
      openingConnection: "Bootloader-Verbindung wird geöffnet...", partitionOk: "Standardmäßige CrossPoint/Xteink-OTA-Partitionstabelle gefunden.", fetchingFirmware: "Firmware-Paket wird heruntergeladen...",
      firmwareDownloadFailed: "Firmware-Download fehlgeschlagen", firmwareTooLarge: "Die Firmware ist zu groß für die App-Partition: {size}.", firmwareTooSmall: "Die Firmware wirkt zu klein. Eine verdächtige Datei wird nicht geflasht.",
      currentBootTarget: "Aktueller Boot: {current}. Nächstes Flash-Ziel: {target}.", writingOffset: "Schreibe an Offset {offset}...", partitionUnexpected: "Unerwartete Partitionskonfiguration. Dieser Browser-Flasher unterstützt nur Geräte mit der standardmäßigen CrossPoint/Xteink-OTA-Tabelle.", unknownFlashError: "Unbekannter Flash-Fehler",
      flashComplete: "Flash abgeschlossen. Das Gerät sollte in CPR-vCodex neu starten.", flashStopped: "Flash gestoppt. Prüfen Sie die Meldung oben, bevor Sie es erneut versuchen.", downloadBin: ".bin herunterladen", communityFlasher: "Community-Flasher",
      flashNote: "Der automatische Flasher nutzt die mit dieser Seite veröffentlichte Firmware-Kopie. Der Download-Button nutzt das GitHub-Release-Asset.",
      manualTitle: "Manueller PlatformIO-Flash", manualText: "Für Entwicklungsbuilds das Repo klonen, den Upload-Port setzen und PlatformIO verwenden.",
      stepPending: "Ausstehend", stepRunning: "Läuft", stepDone: "Fertig", stepFailed: "Fehler", stepConnect: "Mit Gerät verbinden", stepPartition: "Partitionstabelle prüfen", stepDownload: "Firmware herunterladen", stepReadOtadata: "otadata-Partition lesen", stepFlashApp: "App-Partition flashen", stepFlashOtadata: "otadata-Partition flashen", stepReset: "Gerät zurücksetzen"
    },
    it: {
      navHome: "Home", navStats: "Editor statistiche", darkMode: "Modalità scura", lightMode: "Modalità chiara",
      homeSubtitle: "Piccoli strumenti browser per CPR-vCodex. Tutto viene eseguito localmente nel browser.", latestFirmware: "Firmware più recente: 1.5.0.24-cpr-vcodex", homeStats: "Editor statistiche",
      flashEyebrow: "Installer nel browser", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Installa l'ultimo firmware CPR-vCodex sul tuo Xteink X3/X4 da Chrome o Edge usando Web Serial. Scrive la partizione app inattiva e poi avvia da quella.",
      beforeFlash: "Prima del flash", backupTitle: "Fai prima un backup", backupText: "Questo flash in stile OTA conserva bootloader, tabella partizioni, scheda SD e impostazioni NVS, ma fare un backup della SD è comunque prudente.",
      cableTitle: "Usa un cavo USB-C dati", cableText: "Collega Xteink X3/X4 al computer. Se non compare una porta seriale, riattiva e sblocca il dispositivo, poi ricollegalo.",
      browserTitle: "Usa Chrome o Edge desktop", browserText: "Firefox e Safari non supportano Web Serial. Il browser ti chiederà di scegliere la porta seriale USB ESP32-C3.",
      unplugTitle: "Non scollegare durante il flash", unplugText: "La pagina valida la tabella partizioni, scrive lo slot OTA inattivo, aggiorna <code>otadata</code> e riavvia il dispositivo alla fine.",
      latestPackage: "Ultimo pacchetto firmware", flashWarning: "Usalo a tuo rischio. Il flash di firmware personalizzato può fallire se il cavo si scollega o il dispositivo perde alimentazione.",
      unsupported: "Web Serial non è disponibile in questo browser. Usa Chrome o Edge su un computer desktop, oppure scarica manualmente il binario.",
      flashButton: "Flash firmware CPR-vCodex", flashing: "Flash in corso...", readyStatus: "Pronto. Premendo Flash si aprirà il selettore della porta seriale del browser.", choosePort: "Scegli la porta seriale ESP32-C3 nel prompt del browser.",
      openingConnection: "Apertura connessione al bootloader...", partitionOk: "Trovata la tabella partizioni OTA CrossPoint/Xteink predefinita.", fetchingFirmware: "Download del pacchetto firmware...",
      firmwareDownloadFailed: "Download firmware non riuscito", firmwareTooLarge: "Il firmware è troppo grande per la partizione app: {size}.", firmwareTooSmall: "Il firmware sembra troppo piccolo. File sospetto non flashato.",
      currentBootTarget: "Boot attuale: {current}. Prossima destinazione flash: {target}.", writingOffset: "Scrittura all'offset {offset}...", partitionUnexpected: "Configurazione partizioni inattesa. Questo flasher browser supporta solo dispositivi con tabella OTA CrossPoint/Xteink predefinita.", unknownFlashError: "Errore flash sconosciuto",
      flashComplete: "Flash completato. Il dispositivo dovrebbe riavviarsi in CPR-vCodex.", flashStopped: "Flash interrotto. Controlla il messaggio sopra prima di riprovare.", downloadBin: "Scarica .bin", communityFlasher: "Flasher community",
      flashNote: "Il flasher automatico usa la copia firmware pubblicata con questa pagina. Il pulsante di download usa l'asset della release GitHub.",
      manualTitle: "Flash manuale con PlatformIO", manualText: "Per build di sviluppo, clona il repo, imposta la porta di upload e usa PlatformIO.",
      stepPending: "In attesa", stepRunning: "In corso", stepDone: "Fatto", stepFailed: "Errore", stepConnect: "Connetti dispositivo", stepPartition: "Valida tabella partizioni", stepDownload: "Scarica firmware", stepReadOtadata: "Leggi partizione otadata", stepFlashApp: "Flash partizione app", stepFlashOtadata: "Flash partizione otadata", stepReset: "Riavvia dispositivo"
    },
    pt: {
      navHome: "Início", navStats: "Editor de estatísticas", darkMode: "Modo escuro", lightMode: "Modo claro",
      homeSubtitle: "Pequenas ferramentas de navegador para CPR-vCodex. Tudo roda localmente no seu navegador.", latestFirmware: "Firmware mais recente: 1.5.0.24-cpr-vcodex", homeStats: "Editor de estatísticas",
      flashEyebrow: "Instalador no navegador", flashTitle: "Gravar CPR-vCodex", flashSubtitle: "Instale o firmware CPR-vCodex mais recente no seu Xteink X3/X4 pelo Chrome ou Edge usando Web Serial. Ele grava a partição de app inativa e depois muda o boot para ela.",
      beforeFlash: "Antes de gravar", backupTitle: "Faça backup primeiro", backupText: "Este flash estilo OTA preserva bootloader, tabela de partições, cartão SD e ajustes NVS, mas ainda é sensato fazer backup do SD.",
      cableTitle: "Use um cabo USB-C de dados", cableText: "Conecte o Xteink X3/X4 ao computador. Se nenhuma porta serial aparecer, acorde e desbloqueie o dispositivo e reconecte.",
      browserTitle: "Use Chrome ou Edge no desktop", browserText: "Firefox e Safari não suportam Web Serial. O navegador pedirá para escolher a porta serial USB ESP32-C3.",
      unplugTitle: "Não desconecte durante o flash", unplugText: "A página valida a tabela de partições, grava o slot OTA inativo, atualiza <code>otadata</code> e reinicia o dispositivo ao terminar.",
      latestPackage: "Pacote de firmware mais recente", flashWarning: "Use por sua conta e risco. Gravar firmware personalizado pode falhar se o cabo desconectar ou o dispositivo perder energia.",
      unsupported: "Web Serial não está disponível neste navegador. Use Chrome ou Edge em um computador desktop, ou baixe o binário manualmente.",
      flashButton: "Gravar firmware CPR-vCodex", flashing: "Gravando...", readyStatus: "Pronto. Clicar em Flash abrirá o seletor de dispositivo serial do navegador.", choosePort: "Escolha a porta serial ESP32-C3 no prompt do navegador.",
      openingConnection: "Abrindo conexão com o bootloader...", partitionOk: "Tabela de partições OTA padrão CrossPoint/Xteink encontrada.", fetchingFirmware: "Baixando pacote de firmware...",
      firmwareDownloadFailed: "Falha ao baixar firmware", firmwareTooLarge: "O firmware é grande demais para a partição de app: {size}.", firmwareTooSmall: "O firmware parece pequeno demais. Arquivo suspeito não será gravado.",
      currentBootTarget: "Boot atual: {current}. Próximo destino de flash: {target}.", writingOffset: "Gravando no offset {offset}...", partitionUnexpected: "Configuração de partições inesperada. Este flasher de navegador só suporta dispositivos com a tabela OTA CrossPoint/Xteink padrão.", unknownFlashError: "Erro de flash desconhecido",
      flashComplete: "Flash concluído. O dispositivo deve reiniciar no CPR-vCodex.", flashStopped: "Flash interrompido. Verifique a mensagem acima antes de tentar de novo.", downloadBin: "Baixar .bin", communityFlasher: "Flasher da comunidade",
      flashNote: "O flasher automático usa a cópia do firmware publicada com esta página. O botão de download usa o asset da release do GitHub.",
      manualTitle: "Flash manual com PlatformIO", manualText: "Para builds de desenvolvimento, clone o repo, configure a porta de upload e use PlatformIO.",
      stepPending: "Pendente", stepRunning: "Em execução", stepDone: "Concluído", stepFailed: "Falhou", stepConnect: "Conectar ao dispositivo", stepPartition: "Validar tabela de partições", stepDownload: "Baixar firmware", stepReadOtadata: "Ler partição otadata", stepFlashApp: "Gravar partição app", stepFlashOtadata: "Gravar partição otadata", stepReset: "Reiniciar dispositivo"
    },
    nl: {
      navHome: "Home", navStats: "Statistiekeditor", darkMode: "Donkere modus", lightMode: "Lichte modus",
      homeSubtitle: "Kleine browsertools voor CPR-vCodex. Alles draait lokaal in je browser.", latestFirmware: "Nieuwste firmware: 1.5.0.24-cpr-vcodex", homeStats: "Statistiekeditor",
      flashEyebrow: "Browserinstallatie", flashTitle: "CPR-vCodex flashen", flashSubtitle: "Installeer de nieuwste CPR-vCodex-firmware op je Xteink X3/X4 vanuit Chrome of Edge met Web Serial. De inactieve app-partitie wordt geschreven en daarna als bootdoel ingesteld.",
      beforeFlash: "Voor het flashen", backupTitle: "Maak eerst een backup", backupText: "Deze OTA-achtige flash behoudt bootloader, partitietabel, SD-kaart en NVS-instellingen, maar een SD-backup blijft verstandig.",
      cableTitle: "Gebruik een USB-C-datakabel", cableText: "Verbind de Xteink X3/X4 met je computer. Verschijnt er geen seriële poort, wek en ontgrendel het apparaat en sluit het opnieuw aan.",
      browserTitle: "Gebruik Chrome of Edge op desktop", browserText: "Firefox en Safari ondersteunen Web Serial niet. De browser vraagt je de ESP32-C3 USB-seriële poort te kiezen.",
      unplugTitle: "Niet loskoppelen tijdens flashen", unplugText: "De pagina valideert de partitietabel, schrijft de inactieve OTA-appslot, werkt <code>otadata</code> bij en reset het apparaat wanneer het klaar is.",
      latestPackage: "Nieuwste firmwarepakket", flashWarning: "Gebruik op eigen risico. Custom firmware flashen kan mislukken als de kabel losraakt of het apparaat stroom verliest.",
      unsupported: "Web Serial is niet beschikbaar in deze browser. Gebruik Chrome of Edge op een desktopcomputer, of download het binaire bestand handmatig.",
      flashButton: "CPR-vCodex-firmware flashen", flashing: "Flashen...", readyStatus: "Klaar. Klik op Flash om de seriële apparaatkiezer van de browser te openen.", choosePort: "Kies de ESP32-C3 seriële poort in de browsermelding.",
      openingConnection: "Bootloaderverbinding openen...", partitionOk: "Standaard CrossPoint/Xteink OTA-partitietabel gevonden.", fetchingFirmware: "Firmwarepakket downloaden...",
      firmwareDownloadFailed: "Firmwaredownload mislukt", firmwareTooLarge: "De firmware is te groot voor de app-partitie: {size}.", firmwareTooSmall: "De firmware lijkt te klein. Verdacht bestand wordt niet geflasht.",
      currentBootTarget: "Huidige boot: {current}. Volgend flashdoel: {target}.", writingOffset: "Schrijven naar offset {offset}...", partitionUnexpected: "Onverwachte partitieconfiguratie. Deze browserflasher ondersteunt alleen apparaten met de standaard CrossPoint/Xteink OTA-tabel.", unknownFlashError: "Onbekende flashfout",
      flashComplete: "Flash voltooid. Het apparaat zou in CPR-vCodex moeten herstarten.", flashStopped: "Flash gestopt. Controleer het bericht hierboven voordat je opnieuw probeert.", downloadBin: ".bin downloaden", communityFlasher: "Communityflasher",
      flashNote: "De automatische flasher gebruikt de firmwarekopie die met deze pagina is gepubliceerd. De downloadknop gebruikt het GitHub-releasebestand.",
      manualTitle: "Handmatig flashen met PlatformIO", manualText: "Voor ontwikkelbuilds: clone de repo, stel de uploadpoort in en gebruik PlatformIO.",
      stepPending: "In wachtrij", stepRunning: "Bezig", stepDone: "Klaar", stepFailed: "Mislukt", stepConnect: "Verbinden met apparaat", stepPartition: "Partitietabel valideren", stepDownload: "Firmware downloaden", stepReadOtadata: "otadata-partitie lezen", stepFlashApp: "App-partitie flashen", stepFlashOtadata: "otadata-partitie flashen", stepReset: "Apparaat resetten"
    }
  });

  Object.assign(messages, {
    be: {
      navHome: "Галоўная", navStats: "Рэдактар статыстыкі", darkMode: "Цёмны рэжым", lightMode: "Светлы рэжым", homeSubtitle: "Невялікія браузерныя інструменты для CPR-vCodex. Усё працуе лакальна ў вашым браузеры.", latestFirmware: "Апошняя прашыўка: 1.5.0.24-cpr-vcodex", homeStats: "Рэдактар статыстыкі",
      flashEyebrow: "Усталёўшчык у браузеры", flashTitle: "Прашыць CPR-vCodex", flashSubtitle: "Усталюйце апошнюю прашыўку CPR-vCodex на Xteink X3/X4 з Chrome або Edge праз Web Serial. Яна запісвае неактыўны app-раздзел і пераключае загрузку на яго.",
      beforeFlash: "Перад прашыўкай", backupTitle: "Спачатку зрабіце копію", backupText: "Гэтая OTA-прашыўка захоўвае bootloader, табліцу раздзелаў, SD-карту і налады NVS, але рэзервовая копія SD усё яшчэ разумная.", cableTitle: "Выкарыстоўвайце USB-C кабель для даных", cableText: "Падключыце Xteink X3/X4 да камп'ютара. Калі серыйны порт не з'явіўся, абудзіце і разблакуйце прыладу, затым падключыце зноў.", browserTitle: "Выкарыстоўвайце Chrome або Edge на камп'ютары", browserText: "Firefox і Safari не падтрымліваюць Web Serial. Браузер папросіць выбраць USB серыйны порт ESP32-C3.", unplugTitle: "Не адключайце падчас прашыўкі", unplugText: "Старонка правярае табліцу раздзелаў, запісвае неактыўны OTA app slot, абнаўляе <code>otadata</code> і перазапускае прыладу пасля завяршэння.",
      latestPackage: "Апошні пакет прашыўкі", flashWarning: "Выкарыстоўвайце на сваю рызыку. Прашыўка кастомнага firmware можа не атрымацца, калі кабель адключыцца або прылада страціць сілкаванне.", unsupported: "Web Serial недаступны ў гэтым браузеры. Выкарыстоўвайце Chrome або Edge на камп'ютары або спампуйце binary уручную.", flashButton: "Прашыць CPR-vCodex", flashing: "Прашыўка...", readyStatus: "Гатова. Націск Flash адкрые выбар серыйнай прылады ў браузеры.", choosePort: "Выберыце серыйны порт ESP32-C3 у акне браузера.", openingConnection: "Адкрыццё злучэння з bootloader...", partitionOk: "Знойдзена стандартная OTA табліца раздзелаў CrossPoint/Xteink.", fetchingFirmware: "Спампоўваецца пакет прашыўкі...",
      firmwareDownloadFailed: "Не ўдалося спампаваць прашыўку", firmwareTooLarge: "Прашыўка занадта вялікая для app-раздзела: {size}.", firmwareTooSmall: "Прашыўка выглядае занадта малой. Падазроны файл не будзе прашыты.", currentBootTarget: "Бягучая загрузка: {current}. Наступная мэта прашыўкі: {target}.", writingOffset: "Запіс у offset {offset}...", partitionUnexpected: "Нечаканая канфігурацыя раздзелаў. Гэты браузерны flasher падтрымлівае толькі стандартную OTA табліцу CrossPoint/Xteink.", unknownFlashError: "Невядомая памылка прашыўкі", flashComplete: "Прашыўка завершана. Прылада павінна запусціцца ў CPR-vCodex.", flashStopped: "Прашыўка спынена. Праверце паведамленне вышэй перад паўторнай спробай.", downloadBin: "Спампаваць .bin", communityFlasher: "Community flasher", flashNote: "Аўтаматычны flasher выкарыстоўвае копію прашыўкі, апублікаваную з гэтай старонкай. Кнопка спампоўкі выкарыстоўвае asset з GitHub release.", manualTitle: "Ручная прашыўка праз PlatformIO", manualText: "Для dev-зборак клануйце repo, задайце upload port і выкарыстоўвайце PlatformIO.", stepPending: "Чакае", stepRunning: "Выконваецца", stepDone: "Гатова", stepFailed: "Памылка", stepConnect: "Падключыць прыладу", stepPartition: "Праверыць табліцу раздзелаў", stepDownload: "Спампаваць прашыўку", stepReadOtadata: "Прачытаць otadata", stepFlashApp: "Прашыць app-раздзел", stepFlashOtadata: "Прашыць otadata", stepReset: "Перазапусціць прыладу"
    },
    kk: {
      navHome: "Басты бет", navStats: "Статистика редакторы", darkMode: "Қараңғы режим", lightMode: "Жарық режим", homeSubtitle: "CPR-vCodex үшін шағын браузер құралдары. Барлығы браузерде жергілікті орындалады.", latestFirmware: "Соңғы firmware: 1.5.0.24-cpr-vcodex", homeStats: "Статистика редакторы",
      flashEyebrow: "Браузердегі орнатқыш", flashTitle: "CPR-vCodex flash", flashSubtitle: "Соңғы CPR-vCodex firmware нұсқасын Xteink X3/X4 құрылғысына Chrome немесе Edge арқылы Web Serial көмегімен орнатыңыз. Ол белсенді емес app бөлімін жазады және boot-ты соған ауыстырады.",
      beforeFlash: "Flash алдында", backupTitle: "Алдымен сақтық көшірме жасаңыз", backupText: "Бұл OTA түріндегі flash bootloader, бөлімдер кестесі, SD карта және NVS баптауларын сақтайды, бірақ SD сақтық көшірмесі бәрібір пайдалы.", cableTitle: "Деректерге арналған USB-C кабелін қолданыңыз", cableText: "Xteink X3/X4 құрылғысын компьютерге қосыңыз. Serial port көрінбесе, құрылғыны оятып, құлпын ашып қайта қосыңыз.", browserTitle: "Desktop-та Chrome немесе Edge қолданыңыз", browserText: "Firefox және Safari Web Serial қолдамайды. Браузер ESP32-C3 USB serial портын таңдауды сұрайды.", unplugTitle: "Flash кезінде ажыратпаңыз", unplugText: "Бет бөлімдер кестесін тексереді, белсенді емес OTA app slot жазады, <code>otadata</code> жаңартады және соңында құрылғыны қайта іске қосады.",
      latestPackage: "Соңғы firmware пакеті", flashWarning: "Өз жауапкершілігіңізбен қолданыңыз. Custom firmware flash кабель ажыраса немесе қуат жоғалса сәтсіз болуы мүмкін.", unsupported: "Бұл браузерде Web Serial қолжетімді емес. Desktop-та Chrome немесе Edge қолданыңыз немесе binary файлын қолмен жүктеңіз.", flashButton: "CPR-vCodex firmware flash", flashing: "Flash жүріп жатыр...", readyStatus: "Дайын. Flash басқанда браузердің serial құрылғы таңдағышы ашылады.", choosePort: "Браузер терезесінде ESP32-C3 serial портын таңдаңыз.", openingConnection: "Bootloader қосылымы ашылуда...", partitionOk: "Әдепкі CrossPoint/Xteink OTA бөлімдер кестесі табылды.", fetchingFirmware: "Firmware пакеті жүктелуде...",
      firmwareDownloadFailed: "Firmware жүктеу сәтсіз", firmwareTooLarge: "Firmware app бөлімі үшін тым үлкен: {size}.", firmwareTooSmall: "Firmware тым кішкентай көрінеді. Күдікті файл жазылмайды.", currentBootTarget: "Ағымдағы boot: {current}. Келесі flash нысаны: {target}.", writingOffset: "{offset} offset-іне жазылуда...", partitionUnexpected: "Күтпеген бөлімдер конфигурациясы. Бұл браузер flasher тек әдепкі CrossPoint/Xteink OTA кестесін қолдайды.", unknownFlashError: "Белгісіз flash қатесі", flashComplete: "Flash аяқталды. Құрылғы CPR-vCodex-ке қайта жүктелуі керек.", flashStopped: "Flash тоқтады. Қайталап көрмес бұрын жоғарыдағы хабарды тексеріңіз.", downloadBin: ".bin жүктеу", communityFlasher: "Қауымдастық flasher", flashNote: "Автоматты flasher осы бетпен жарияланған firmware көшірмесін қолданады. Жүктеу түймесі GitHub release asset қолданады.", manualTitle: "PlatformIO арқылы қолмен flash", manualText: "Dev build үшін repo-ны клондап, upload port орнатыңыз және PlatformIO қолданыңыз.", stepPending: "Күтуде", stepRunning: "Орындалуда", stepDone: "Дайын", stepFailed: "Қате", stepConnect: "Құрылғыға қосылу", stepPartition: "Бөлімдер кестесін тексеру", stepDownload: "Firmware жүктеу", stepReadOtadata: "otadata оқу", stepFlashApp: "app бөлімін flash", stepFlashOtadata: "otadata flash", stepReset: "Құрылғыны қайта қосу"
    },
    si: {
      navHome: "Domov", navStats: "Urejevalnik statistik", darkMode: "Temni način", lightMode: "Svetli način", homeSubtitle: "Majhna brskalniška orodja za CPR-vCodex. Vse teče lokalno v brskalniku.", latestFirmware: "Najnovejši firmware: 1.5.0.24-cpr-vcodex", homeStats: "Urejevalnik statistik",
      flashEyebrow: "Namestilnik v brskalniku", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Namestite najnovejši CPR-vCodex firmware na Xteink X3/X4 iz Chroma ali Edge z Web Serial. Zapiše neaktivno app particijo in nato preklopi boot nanjo.",
      beforeFlash: "Pred flashanjem", backupTitle: "Najprej varnostna kopija", backupText: "Ta OTA podoben flash ohrani bootloader, tabelo particij, SD kartico in NVS nastavitve, vendar je varnostna kopija SD še vedno smiselna.", cableTitle: "Uporabite podatkovni USB-C kabel", cableText: "Povežite Xteink X3/X4 z računalnikom. Če se serijska vrata ne prikažejo, zbudite in odklenite napravo ter jo znova povežite.", browserTitle: "Uporabite Chrome ali Edge na namizju", browserText: "Firefox in Safari ne podpirata Web Serial. Brskalnik vas bo prosil, da izberete ESP32-C3 USB serijska vrata.", unplugTitle: "Med flashanjem ne odklapljajte", unplugText: "Stran preveri tabelo particij, zapiše neaktivni OTA app slot, posodobi <code>otadata</code> in na koncu ponastavi napravo.",
      latestPackage: "Najnovejši paket firmware", flashWarning: "Uporabljate na lastno odgovornost. Flash custom firmware lahko spodleti, če se kabel odklopi ali naprava izgubi napajanje.", unsupported: "Web Serial v tem brskalniku ni na voljo. Uporabite Chrome ali Edge na namizju ali ročno prenesite binarno datoteko.", flashButton: "Flash CPR-vCodex firmware", flashing: "Flashanje...", readyStatus: "Pripravljeno. Klik na Flash odpre izbirnik serijske naprave v brskalniku.", choosePort: "Izberite ESP32-C3 serijska vrata v pozivu brskalnika.", openingConnection: "Odpiranje povezave z bootloaderjem...", partitionOk: "Najdena privzeta CrossPoint/Xteink OTA tabela particij.", fetchingFirmware: "Prenos paketa firmware...",
      firmwareDownloadFailed: "Prenos firmware ni uspel", firmwareTooLarge: "Firmware je prevelik za app particijo: {size}.", firmwareTooSmall: "Firmware je videti premajhen. Sumljiva datoteka ne bo flashana.", currentBootTarget: "Trenutni boot: {current}. Naslednji flash cilj: {target}.", writingOffset: "Zapis na offset {offset}...", partitionUnexpected: "Nepričakovana konfiguracija particij. Ta brskalniški flasher podpira samo privzeto CrossPoint/Xteink OTA tabelo.", unknownFlashError: "Neznana napaka flashanja", flashComplete: "Flash končan. Naprava bi se morala zagnati v CPR-vCodex.", flashStopped: "Flash ustavljen. Pred ponovnim poskusom preverite zgornje sporočilo.", downloadBin: "Prenesi .bin", communityFlasher: "Skupnostni flasher", flashNote: "Samodejni flasher uporablja kopijo firmware, objavljeno s to stranjo. Gumb za prenos uporablja GitHub release asset.", manualTitle: "Ročni PlatformIO flash", manualText: "Za razvojne build-e klonirajte repo, nastavite upload port in uporabite PlatformIO.", stepPending: "Čaka", stepRunning: "Teče", stepDone: "Končano", stepFailed: "Napaka", stepConnect: "Poveži napravo", stepPartition: "Preveri tabelo particij", stepDownload: "Prenesi firmware", stepReadOtadata: "Preberi otadata", stepFlashApp: "Flash app particije", stepFlashOtadata: "Flash otadata particije", stepReset: "Ponastavi napravo"
    }
  });

  Object.assign(messages, {
    hu: {
      navHome: "Kezdőlap", navStats: "Statisztika szerkesztő", darkMode: "Sötét mód", lightMode: "Világos mód", homeSubtitle: "Kis böngészős eszközök a CPR-vCodexhez. Minden helyben fut a böngésződben.", latestFirmware: "Legújabb firmware: 1.5.0.24-cpr-vcodex", homeStats: "Statisztika szerkesztő",
      flashEyebrow: "Böngészős telepítő", flashTitle: "CPR-vCodex flash", flashSubtitle: "Telepítsd a legújabb CPR-vCodex firmware-t az Xteink X3/X4-re Chrome vagy Edge alatt Web Serial segítségével. Az inaktív app partíciót írja, majd arra állítja a bootot.",
      beforeFlash: "Flash előtt", backupTitle: "Előbb készíts mentést", backupText: "Ez az OTA-szerű flash megtartja a bootloadert, partíciós táblát, SD-kártyát és NVS beállításokat, de az SD mentése továbbra is ajánlott.", cableTitle: "Használj adat USB-C kábelt", cableText: "Csatlakoztasd az Xteink X3/X4-et a számítógéphez. Ha nem jelenik meg soros port, ébreszd fel és oldd fel az eszközt, majd csatlakoztasd újra.", browserTitle: "Használj Chrome-ot vagy Edge-et asztali gépen", browserText: "A Firefox és Safari nem támogatja a Web Serialt. A böngésző kérni fogja az ESP32-C3 USB soros port kiválasztását.", unplugTitle: "Flash közben ne húzd ki", unplugText: "Az oldal ellenőrzi a partíciós táblát, írja az inaktív OTA app slotot, frissíti az <code>otadata</code>-t, majd újraindítja az eszközt.",
      latestPackage: "Legújabb firmware csomag", flashWarning: "Saját felelősségre használd. Egyedi firmware flash-elése hibázhat, ha a kábel lecsatlakozik vagy az eszköz elveszíti az áramot.", unsupported: "A Web Serial nem érhető el ebben a böngészőben. Használj Chrome-ot vagy Edge-et asztali gépen, vagy töltsd le kézzel a binárist.", flashButton: "CPR-vCodex firmware flash", flashing: "Flash folyamatban...", readyStatus: "Kész. A Flash gomb megnyitja a böngésző soros eszközválasztóját.", choosePort: "Válaszd ki az ESP32-C3 soros portot a böngésző ablakában.", openingConnection: "Bootloader kapcsolat megnyitása...", partitionOk: "Alapértelmezett CrossPoint/Xteink OTA partíciós tábla található.", fetchingFirmware: "Firmware csomag letöltése...",
      firmwareDownloadFailed: "Firmware letöltése sikertelen", firmwareTooLarge: "A firmware túl nagy az app partícióhoz: {size}.", firmwareTooSmall: "A firmware túl kicsinek tűnik. Gyanús fájl nem kerül flash-elésre.", currentBootTarget: "Jelenlegi boot: {current}. Következő flash cél: {target}.", writingOffset: "Írás erre az offsetre: {offset}...", partitionUnexpected: "Váratlan partíciós konfiguráció. Ez a böngészős flasher csak az alapértelmezett CrossPoint/Xteink OTA táblát támogatja.", unknownFlashError: "Ismeretlen flash hiba", flashComplete: "Flash kész. Az eszköznek CPR-vCodexbe kell újraindulnia.", flashStopped: "Flash leállt. Újrapróbálás előtt nézd meg a fenti üzenetet.", downloadBin: ".bin letöltése", communityFlasher: "Közösségi flasher", flashNote: "Az automata flasher az ezzel az oldallal publikált firmware másolatot használja. A letöltés gomb a GitHub release assetet használja.", manualTitle: "Kézi PlatformIO flash", manualText: "Fejlesztői buildekhez klónozd a repót, állítsd be az upload portot és használd a PlatformIO-t.", stepPending: "Várakozik", stepRunning: "Fut", stepDone: "Kész", stepFailed: "Hiba", stepConnect: "Csatlakozás eszközhöz", stepPartition: "Partíciós tábla ellenőrzése", stepDownload: "Firmware letöltése", stepReadOtadata: "otadata partíció olvasása", stepFlashApp: "app partíció flash", stepFlashOtadata: "otadata partíció flash", stepReset: "Eszköz újraindítása"
    },
    lt: {
      navHome: "Pradžia", navStats: "Statistikos redaktorius", darkMode: "Tamsus režimas", lightMode: "Šviesus režimas", homeSubtitle: "Maži naršyklės įrankiai CPR-vCodex. Viskas veikia lokaliai jūsų naršyklėje.", latestFirmware: "Naujausia programinė įranga: 1.5.0.24-cpr-vcodex", homeStats: "Statistikos redaktorius",
      flashEyebrow: "Diegiklis naršyklėje", flashTitle: "Įrašyti CPR-vCodex", flashSubtitle: "Įdiekite naujausią CPR-vCodex firmware į Xteink X3/X4 per Chrome arba Edge su Web Serial. Įrašoma neaktyvi app particija ir į ją perjungiamas paleidimas.",
      beforeFlash: "Prieš įrašymą", backupTitle: "Pirmiausia atsarginė kopija", backupText: "Šis OTA tipo įrašymas išsaugo bootloader, particijų lentelę, SD kortelę ir NVS nustatymus, bet SD kopija vis tiek naudinga.", cableTitle: "Naudokite duomenų USB-C kabelį", cableText: "Prijunkite Xteink X3/X4 prie kompiuterio. Jei serijinis portas nepasirodo, pažadinkite ir atrakinkite įrenginį, tada prijunkite iš naujo.", browserTitle: "Naudokite Chrome arba Edge kompiuteryje", browserText: "Firefox ir Safari nepalaiko Web Serial. Naršyklė paprašys pasirinkti ESP32-C3 USB serijinį portą.", unplugTitle: "Neatjunkite įrašymo metu", unplugText: "Puslapis patikrina particijų lentelę, įrašo neaktyvų OTA app slotą, atnaujina <code>otadata</code> ir pabaigoje perkrauna įrenginį.",
      latestPackage: "Naujausias firmware paketas", flashWarning: "Naudokite savo rizika. Individualaus firmware įrašymas gali nepavykti, jei kabelis atsijungs arba įrenginys neteks maitinimo.", unsupported: "Web Serial šioje naršyklėje nepasiekiamas. Naudokite Chrome arba Edge kompiuteryje arba atsisiųskite binarinį failą rankiniu būdu.", flashButton: "Įrašyti CPR-vCodex firmware", flashing: "Įrašoma...", readyStatus: "Paruošta. Paspaudus Flash atsidarys naršyklės serijinio įrenginio pasirinkimas.", choosePort: "Naršyklės lange pasirinkite ESP32-C3 serijinį portą.", openingConnection: "Atidaromas bootloader ryšys...", partitionOk: "Rasta numatytoji CrossPoint/Xteink OTA particijų lentelė.", fetchingFirmware: "Atsisiunčiamas firmware paketas...",
      firmwareDownloadFailed: "Firmware atsisiųsti nepavyko", firmwareTooLarge: "Firmware per didelis app particijai: {size}.", firmwareTooSmall: "Firmware atrodo per mažas. Įtartinas failas nebus įrašomas.", currentBootTarget: "Dabartinis boot: {current}. Kitas įrašymo tikslas: {target}.", writingOffset: "Rašoma į offset {offset}...", partitionUnexpected: "Netikėta particijų konfigūracija. Šis naršyklės flasher palaiko tik numatytąją CrossPoint/Xteink OTA lentelę.", unknownFlashError: "Nežinoma įrašymo klaida", flashComplete: "Įrašymas baigtas. Įrenginys turėtų persikrauti į CPR-vCodex.", flashStopped: "Įrašymas sustabdytas. Prieš bandydami dar kartą patikrinkite pranešimą aukščiau.", downloadBin: "Atsisiųsti .bin", communityFlasher: "Bendruomenės flasher", flashNote: "Automatinis flasher naudoja su šiuo puslapiu publikuotą firmware kopiją. Atsisiuntimo mygtukas naudoja GitHub release asset.", manualTitle: "Rankinis PlatformIO įrašymas", manualText: "Kūrimo versijoms klonuokite repo, nustatykite upload portą ir naudokite PlatformIO.", stepPending: "Laukia", stepRunning: "Vykdoma", stepDone: "Atlikta", stepFailed: "Klaida", stepConnect: "Prijungti įrenginį", stepPartition: "Tikrinti particijų lentelę", stepDownload: "Atsisiųsti firmware", stepReadOtadata: "Skaityti otadata particiją", stepFlashApp: "Įrašyti app particiją", stepFlashOtadata: "Įrašyti otadata particiją", stepReset: "Perkrauti įrenginį"
    },
    ro: {
      navHome: "Acasă", navStats: "Editor statistici", darkMode: "Mod întunecat", lightMode: "Mod luminos", homeSubtitle: "Mici unelte de browser pentru CPR-vCodex. Totul rulează local în browser.", latestFirmware: "Cel mai nou firmware: 1.5.0.24-cpr-vcodex", homeStats: "Editor statistici",
      flashEyebrow: "Instalator în browser", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Instalează cel mai nou firmware CPR-vCodex pe Xteink X3/X4 din Chrome sau Edge folosind Web Serial. Scrie partiția app inactivă și apoi pornește de pe ea.",
      beforeFlash: "Înainte de flash", backupTitle: "Fă backup mai întâi", backupText: "Acest flash de tip OTA păstrează bootloaderul, tabela de partiții, cardul SD și setările NVS, dar un backup al SD-ului rămâne recomandat.", cableTitle: "Folosește un cablu USB-C de date", cableText: "Conectează Xteink X3/X4 la computer. Dacă nu apare un port serial, trezește și deblochează dispozitivul, apoi reconectează-l.", browserTitle: "Folosește Chrome sau Edge pe desktop", browserText: "Firefox și Safari nu suportă Web Serial. Browserul îți va cere să alegi portul serial USB ESP32-C3.", unplugTitle: "Nu deconecta în timpul flashului", unplugText: "Pagina validează tabela de partiții, scrie slotul OTA app inactiv, actualizează <code>otadata</code> și resetează dispozitivul la final.",
      latestPackage: "Cel mai nou pachet firmware", flashWarning: "Folosește pe propria răspundere. Flashul unui firmware custom poate eșua dacă se deconectează cablul sau dispozitivul pierde alimentarea.", unsupported: "Web Serial nu este disponibil în acest browser. Folosește Chrome sau Edge pe desktop sau descarcă manual binarul.", flashButton: "Flash firmware CPR-vCodex", flashing: "Se face flash...", readyStatus: "Gata. Apăsarea Flash va deschide selectorul de dispozitiv serial al browserului.", choosePort: "Alege portul serial ESP32-C3 în dialogul browserului.", openingConnection: "Se deschide conexiunea cu bootloaderul...", partitionOk: "A fost găsită tabela OTA CrossPoint/Xteink implicită.", fetchingFirmware: "Se descarcă pachetul firmware...",
      firmwareDownloadFailed: "Descărcarea firmware a eșuat", firmwareTooLarge: "Firmware-ul este prea mare pentru partiția app: {size}.", firmwareTooSmall: "Firmware-ul pare prea mic. Fișierul suspect nu va fi scris.", currentBootTarget: "Boot curent: {current}. Următoarea țintă flash: {target}.", writingOffset: "Scriere la offset {offset}...", partitionUnexpected: "Configurație de partiții neașteptată. Acest flasher de browser suportă doar tabela OTA CrossPoint/Xteink implicită.", unknownFlashError: "Eroare flash necunoscută", flashComplete: "Flash complet. Dispozitivul ar trebui să repornească în CPR-vCodex.", flashStopped: "Flash oprit. Verifică mesajul de mai sus înainte de a reîncerca.", downloadBin: "Descarcă .bin", communityFlasher: "Flasher comunitar", flashNote: "Flasherul automat folosește copia firmware publicată cu această pagină. Butonul de descărcare folosește assetul release-ului GitHub.", manualTitle: "Flash manual cu PlatformIO", manualText: "Pentru builduri de dezvoltare, clonează repo-ul, setează portul de upload și folosește PlatformIO.", stepPending: "În așteptare", stepRunning: "Rulează", stepDone: "Gata", stepFailed: "Eroare", stepConnect: "Conectare la dispozitiv", stepPartition: "Validare tabelă partiții", stepDownload: "Descărcare firmware", stepReadOtadata: "Citire partiție otadata", stepFlashApp: "Flash partiție app", stepFlashOtadata: "Flash partiție otadata", stepReset: "Resetare dispozitiv"
    },
    sv: {
      navHome: "Hem", navStats: "Statistikredigerare", darkMode: "Mörkt läge", lightMode: "Ljust läge", homeSubtitle: "Små webbläsarverktyg för CPR-vCodex. Allt körs lokalt i din webbläsare.", latestFirmware: "Senaste firmware: 1.5.0.24-cpr-vcodex", homeStats: "Statistikredigerare",
      flashEyebrow: "Webbläsarbaserad installerare", flashTitle: "Flasha CPR-vCodex", flashSubtitle: "Installera senaste CPR-vCodex-firmware på din Xteink X3/X4 från Chrome eller Edge med Web Serial. Den skriver den inaktiva app-partitionen och växlar sedan boot till den.",
      beforeFlash: "Innan du flashar", backupTitle: "Säkerhetskopiera först", backupText: "Denna OTA-liknande flash behåller bootloader, partitionstabell, SD-kort och NVS-inställningar, men backup av SD-kortet är fortfarande klokt.", cableTitle: "Använd en USB-C-datakabel", cableText: "Anslut Xteink X3/X4 till datorn. Om ingen serieport visas, väck och lås upp enheten och anslut igen.", browserTitle: "Använd Chrome eller Edge på desktop", browserText: "Firefox och Safari stöder inte Web Serial. Webbläsaren ber dig välja ESP32-C3 USB-serieporten.", unplugTitle: "Koppla inte ur under flash", unplugText: "Sidan validerar partitionstabellen, skriver den inaktiva OTA-appslotten, uppdaterar <code>otadata</code> och återställer enheten när den är klar.",
      latestPackage: "Senaste firmwarepaket", flashWarning: "Använd på egen risk. Flash av anpassad firmware kan misslyckas om kabeln kopplas ur eller enheten tappar ström.", unsupported: "Web Serial är inte tillgängligt i denna webbläsare. Använd Chrome eller Edge på en desktop, eller ladda ner binärfilen manuellt.", flashButton: "Flasha CPR-vCodex-firmware", flashing: "Flashar...", readyStatus: "Redo. Klick på Flash öppnar webbläsarens serieenhetsväljare.", choosePort: "Välj ESP32-C3-serieporten i webbläsarens prompt.", openingConnection: "Öppnar bootloaderanslutning...", partitionOk: "Standard CrossPoint/Xteink OTA-partitionstabell hittades.", fetchingFirmware: "Hämtar firmwarepaket...",
      firmwareDownloadFailed: "Firmwarehämtning misslyckades", firmwareTooLarge: "Firmware är för stor för app-partitionen: {size}.", firmwareTooSmall: "Firmware verkar för liten. Misstänkt fil flashas inte.", currentBootTarget: "Aktuell boot: {current}. Nästa flashmål: {target}.", writingOffset: "Skriver till offset {offset}...", partitionUnexpected: "Oväntad partitionskonfiguration. Denna webbläsarflasher stöder endast standard CrossPoint/Xteink OTA-tabellen.", unknownFlashError: "Okänt flashfel", flashComplete: "Flash klar. Enheten bör starta om till CPR-vCodex.", flashStopped: "Flash stoppad. Kontrollera meddelandet ovan innan du försöker igen.", downloadBin: "Ladda ner .bin", communityFlasher: "Community-flasher", flashNote: "Den automatiska flashern använder firmwarekopian som publiceras med denna sida. Nedladdningsknappen använder GitHub release-asset.", manualTitle: "Manuell PlatformIO-flash", manualText: "För utvecklingsbyggen, klona repot, ställ in upload-porten och använd PlatformIO.", stepPending: "Väntar", stepRunning: "Kör", stepDone: "Klart", stepFailed: "Fel", stepConnect: "Anslut till enhet", stepPartition: "Validera partitionstabell", stepDownload: "Hämta firmware", stepReadOtadata: "Läs otadata-partition", stepFlashApp: "Flasha app-partition", stepFlashOtadata: "Flasha otadata-partition", stepReset: "Återställ enhet"
    },
    tr: {
      navHome: "Ana sayfa", navStats: "İstatistik düzenleyici", darkMode: "Karanlık mod", lightMode: "Aydınlık mod", homeSubtitle: "CPR-vCodex için küçük tarayıcı araçları. Her şey tarayıcınızda yerel çalışır.", latestFirmware: "En son firmware: 1.5.0.24-cpr-vcodex", homeStats: "İstatistik düzenleyici",
      flashEyebrow: "Tarayıcı tabanlı kurucu", flashTitle: "CPR-vCodex flashla", flashSubtitle: "En son CPR-vCodex firmware'ini Chrome veya Edge üzerinden Web Serial ile Xteink X3/X4'e kurun. Pasif app bölümünü yazar ve boot'u ona geçirir.",
      beforeFlash: "Flashlamadan önce", backupTitle: "Önce yedek alın", backupText: "Bu OTA tarzı flash bootloader, bölüm tablosu, SD kart ve NVS ayarlarını korur; yine de SD yedeği almak mantıklıdır.", cableTitle: "Veri USB-C kablosu kullanın", cableText: "Xteink X3/X4'ü bilgisayara bağlayın. Seri port görünmezse cihazı uyandırıp kilidini açın ve yeniden bağlayın.", browserTitle: "Masaüstünde Chrome veya Edge kullanın", browserText: "Firefox ve Safari Web Serial desteklemez. Tarayıcı ESP32-C3 USB seri portunu seçmenizi ister.", unplugTitle: "Flash sırasında çıkarmayın", unplugText: "Sayfa bölüm tablosunu doğrular, pasif OTA app slotunu yazar, <code>otadata</code> günceller ve bitince cihazı sıfırlar.",
      latestPackage: "En son firmware paketi", flashWarning: "Kendi sorumluluğunuzda kullanın. Özel firmware flashlama, kablo koparsa veya cihaz güç kaybederse başarısız olabilir.", unsupported: "Web Serial bu tarayıcıda mevcut değil. Masaüstünde Chrome veya Edge kullanın ya da ikili dosyayı manuel indirin.", flashButton: "CPR-vCodex firmware flashla", flashing: "Flashlanıyor...", readyStatus: "Hazır. Flash'a basınca tarayıcının seri cihaz seçicisi açılır.", choosePort: "Tarayıcı penceresinde ESP32-C3 seri portunu seçin.", openingConnection: "Bootloader bağlantısı açılıyor...", partitionOk: "Varsayılan CrossPoint/Xteink OTA bölüm tablosu bulundu.", fetchingFirmware: "Firmware paketi indiriliyor...",
      firmwareDownloadFailed: "Firmware indirilemedi", firmwareTooLarge: "Firmware app bölümü için çok büyük: {size}.", firmwareTooSmall: "Firmware çok küçük görünüyor. Şüpheli dosya flashlanmayacak.", currentBootTarget: "Geçerli boot: {current}. Sonraki flash hedefi: {target}.", writingOffset: "{offset} offsetine yazılıyor...", partitionUnexpected: "Beklenmeyen bölüm yapılandırması. Bu tarayıcı flasher yalnızca varsayılan CrossPoint/Xteink OTA tablosunu destekler.", unknownFlashError: "Bilinmeyen flash hatası", flashComplete: "Flash tamamlandı. Cihaz CPR-vCodex'e yeniden başlamalı.", flashStopped: "Flash durdu. Yeniden denemeden önce yukarıdaki mesajı kontrol edin.", downloadBin: ".bin indir", communityFlasher: "Topluluk flasher", flashNote: "Otomatik flasher bu sayfayla yayınlanan firmware kopyasını kullanır. İndirme düğmesi GitHub release assetini kullanır.", manualTitle: "PlatformIO ile manuel flash", manualText: "Geliştirme buildleri için repoyu klonlayın, upload portunu ayarlayın ve PlatformIO kullanın.", stepPending: "Bekliyor", stepRunning: "Çalışıyor", stepDone: "Bitti", stepFailed: "Hata", stepConnect: "Cihaza bağlan", stepPartition: "Bölüm tablosunu doğrula", stepDownload: "Firmware indir", stepReadOtadata: "otadata bölümünü oku", stepFlashApp: "app bölümünü flashla", stepFlashOtadata: "otadata bölümünü flashla", stepReset: "Cihazı sıfırla"
    },
    uk: {
      navHome: "Головна", navStats: "Редактор статистики", darkMode: "Темний режим", lightMode: "Світлий режим", homeSubtitle: "Невеликі браузерні інструменти для CPR-vCodex. Усе працює локально у браузері.", latestFirmware: "Остання прошивка: 1.5.0.24-cpr-vcodex", homeStats: "Редактор статистики",
      flashEyebrow: "Інсталятор у браузері", flashTitle: "Прошити CPR-vCodex", flashSubtitle: "Встановіть останню прошивку CPR-vCodex на Xteink X3/X4 з Chrome або Edge через Web Serial. Вона записує неактивний app-розділ і перемикає завантаження на нього.",
      beforeFlash: "Перед прошивкою", backupTitle: "Спочатку зробіть копію", backupText: "OTA-прошивка зберігає bootloader, таблицю розділів, SD-карту та налаштування NVS, але резервна копія SD все одно бажана.", cableTitle: "Використовуйте USB-C кабель для даних", cableText: "Підключіть Xteink X3/X4 до комп'ютера. Якщо послідовний порт не з'явився, розбудіть і розблокуйте пристрій, потім підключіть знову.", browserTitle: "Використовуйте Chrome або Edge на комп'ютері", browserText: "Firefox і Safari не підтримують Web Serial. Браузер попросить вибрати USB послідовний порт ESP32-C3.", unplugTitle: "Не від'єднуйте під час прошивки", unplugText: "Сторінка перевіряє таблицю розділів, записує неактивний OTA app slot, оновлює <code>otadata</code> і перезавантажує пристрій після завершення.",
      latestPackage: "Останній пакет прошивки", flashWarning: "Використовуйте на власний ризик. Прошивка кастомного firmware може не вдатися, якщо кабель від'єднається або пристрій втратить живлення.", unsupported: "Web Serial недоступний у цьому браузері. Використовуйте Chrome або Edge на комп'ютері або завантажте binary вручну.", flashButton: "Прошити CPR-vCodex", flashing: "Прошивка...", readyStatus: "Готово. Натискання Flash відкриє вибір послідовного пристрою в браузері.", choosePort: "Виберіть послідовний порт ESP32-C3 у вікні браузера.", openingConnection: "Відкриття з'єднання з bootloader...", partitionOk: "Знайдено стандартну OTA таблицю розділів CrossPoint/Xteink.", fetchingFirmware: "Завантаження пакета прошивки...",
      firmwareDownloadFailed: "Не вдалося завантажити прошивку", firmwareTooLarge: "Прошивка завелика для app-розділу: {size}.", firmwareTooSmall: "Прошивка виглядає замалою. Підозрілий файл не буде прошито.", currentBootTarget: "Поточне завантаження: {current}. Наступна ціль прошивки: {target}.", writingOffset: "Запис за offset {offset}...", partitionUnexpected: "Неочікувана конфігурація розділів. Цей браузерний flasher підтримує лише стандартну OTA таблицю CrossPoint/Xteink.", unknownFlashError: "Невідома помилка прошивки", flashComplete: "Прошивку завершено. Пристрій має перезапуститися в CPR-vCodex.", flashStopped: "Прошивку зупинено. Перевірте повідомлення вище перед повторною спробою.", downloadBin: "Завантажити .bin", communityFlasher: "Community flasher", flashNote: "Автоматичний flasher використовує копію прошивки, опубліковану з цією сторінкою. Кнопка завантаження використовує asset GitHub release.", manualTitle: "Ручна прошивка через PlatformIO", manualText: "Для dev-збірок склонуйте repo, задайте upload port і використовуйте PlatformIO.", stepPending: "Очікує", stepRunning: "Виконується", stepDone: "Готово", stepFailed: "Помилка", stepConnect: "Підключити пристрій", stepPartition: "Перевірити таблицю розділів", stepDownload: "Завантажити прошивку", stepReadOtadata: "Прочитати otadata", stepFlashApp: "Прошити app-розділ", stepFlashOtadata: "Прошити otadata", stepReset: "Перезапустити пристрій"
    },
    vi: {
      navHome: "Trang chủ", navStats: "Trình sửa thống kê", darkMode: "Chế độ tối", lightMode: "Chế độ sáng", homeSubtitle: "Các công cụ trình duyệt nhỏ cho CPR-vCodex. Mọi thứ chạy cục bộ trong trình duyệt của bạn.", latestFirmware: "Firmware mới nhất: 1.5.0.24-cpr-vcodex", homeStats: "Trình sửa thống kê",
      flashEyebrow: "Trình cài đặt trên trình duyệt", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Cài firmware CPR-vCodex mới nhất lên Xteink X3/X4 bằng Chrome hoặc Edge qua Web Serial. Nó ghi phân vùng app không hoạt động rồi chuyển boot sang đó.",
      beforeFlash: "Trước khi flash", backupTitle: "Sao lưu trước", backupText: "Kiểu flash OTA này giữ bootloader, bảng phân vùng, thẻ SD và cài đặt NVS, nhưng sao lưu SD vẫn là điều nên làm.", cableTitle: "Dùng cáp USB-C dữ liệu", cableText: "Kết nối Xteink X3/X4 với máy tính. Nếu không thấy cổng serial, hãy đánh thức và mở khóa thiết bị rồi kết nối lại.", browserTitle: "Dùng Chrome hoặc Edge trên desktop", browserText: "Firefox và Safari không hỗ trợ Web Serial. Trình duyệt sẽ yêu cầu chọn cổng USB serial ESP32-C3.", unplugTitle: "Không rút cáp khi đang flash", unplugText: "Trang sẽ kiểm tra bảng phân vùng, ghi slot OTA app không hoạt động, cập nhật <code>otadata</code> và reset thiết bị khi hoàn tất.",
      latestPackage: "Gói firmware mới nhất", flashWarning: "Tự chịu rủi ro khi sử dụng. Flash firmware tùy chỉnh có thể lỗi nếu cáp bị rút hoặc thiết bị mất nguồn.", unsupported: "Web Serial không có trong trình duyệt này. Hãy dùng Chrome hoặc Edge trên máy tính, hoặc tải file nhị phân thủ công.", flashButton: "Flash firmware CPR-vCodex", flashing: "Đang flash...", readyStatus: "Sẵn sàng. Nhấn Flash sẽ mở bộ chọn thiết bị serial của trình duyệt.", choosePort: "Chọn cổng serial ESP32-C3 trong hộp thoại trình duyệt.", openingConnection: "Đang mở kết nối bootloader...", partitionOk: "Đã tìm thấy bảng phân vùng OTA CrossPoint/Xteink mặc định.", fetchingFirmware: "Đang tải gói firmware...",
      firmwareDownloadFailed: "Tải firmware thất bại", firmwareTooLarge: "Firmware quá lớn cho phân vùng app: {size}.", firmwareTooSmall: "Firmware có vẻ quá nhỏ. Không flash file đáng ngờ.", currentBootTarget: "Boot hiện tại: {current}. Mục tiêu flash tiếp theo: {target}.", writingOffset: "Đang ghi tại offset {offset}...", partitionUnexpected: "Cấu hình phân vùng không mong đợi. Flasher trình duyệt này chỉ hỗ trợ bảng OTA CrossPoint/Xteink mặc định.", unknownFlashError: "Lỗi flash không xác định", flashComplete: "Flash hoàn tất. Thiết bị sẽ khởi động lại vào CPR-vCodex.", flashStopped: "Flash đã dừng. Kiểm tra thông báo phía trên trước khi thử lại.", downloadBin: "Tải .bin", communityFlasher: "Flasher cộng đồng", flashNote: "Flasher tự động dùng bản firmware được xuất bản cùng trang này. Nút tải dùng asset của GitHub release.", manualTitle: "Flash thủ công bằng PlatformIO", manualText: "Với bản build phát triển, clone repo, đặt cổng upload rồi dùng PlatformIO.", stepPending: "Chờ", stepRunning: "Đang chạy", stepDone: "Xong", stepFailed: "Lỗi", stepConnect: "Kết nối thiết bị", stepPartition: "Kiểm tra bảng phân vùng", stepDownload: "Tải firmware", stepReadOtadata: "Đọc phân vùng otadata", stepFlashApp: "Flash phân vùng app", stepFlashOtadata: "Flash phân vùng otadata", stepReset: "Reset thiết bị"
    }
  });

  Object.assign(messages, {
    cs: {
      navHome: "Domů", navStats: "Editor statistik", darkMode: "Tmavý režim", lightMode: "Světlý režim", homeSubtitle: "Malé nástroje v prohlížeči pro CPR-vCodex. Vše běží lokálně ve vašem prohlížeči.", latestFirmware: "Nejnovější firmware: 1.5.0.24-cpr-vcodex", homeStats: "Editor statistik",
      flashEyebrow: "Instalátor v prohlížeči", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Nainstalujte nejnovější firmware CPR-vCodex na Xteink X3/X4 z Chromu nebo Edge pomocí Web Serial. Zapíše neaktivní app oddíl a poté na něj přepne bootování.",
      beforeFlash: "Před flashováním", backupTitle: "Nejprve zálohujte", backupText: "Tento OTA flash zachová bootloader, tabulku oddílů, SD kartu a nastavení NVS, ale záloha SD je stále rozumná.", cableTitle: "Použijte datový USB-C kabel", cableText: "Připojte Xteink X3/X4 k počítači. Pokud se sériový port nezobrazí, probuďte a odemkněte zařízení a znovu jej připojte.", browserTitle: "Použijte Chrome nebo Edge na desktopu", browserText: "Firefox a Safari nepodporují Web Serial. Prohlížeč vás požádá o výběr USB sériového portu ESP32-C3.", unplugTitle: "Během flashování neodpojujte", unplugText: "Stránka ověří tabulku oddílů, zapíše neaktivní OTA slot, aktualizuje <code>otadata</code> a po dokončení zařízení resetuje.",
      latestPackage: "Nejnovější balíček firmwaru", flashWarning: "Použití na vlastní riziko. Flash vlastního firmwaru může selhat, pokud se odpojí kabel nebo zařízení ztratí napájení.", unsupported: "Web Serial není v tomto prohlížeči dostupný. Použijte Chrome nebo Edge na desktopu, nebo stáhněte binárku ručně.", flashButton: "Flashovat firmware CPR-vCodex", flashing: "Flashování...", readyStatus: "Připraveno. Kliknutí na Flash otevře výběr sériového zařízení v prohlížeči.", choosePort: "Vyberte sériový port ESP32-C3 v dialogu prohlížeče.", openingConnection: "Otevírání spojení s bootloaderem...", partitionOk: "Nalezena výchozí OTA tabulka oddílů CrossPoint/Xteink.", fetchingFirmware: "Stahování balíčku firmwaru...",
      firmwareDownloadFailed: "Stažení firmwaru selhalo", firmwareTooLarge: "Firmware je pro app oddíl příliš velký: {size}.", firmwareTooSmall: "Firmware vypadá příliš malý. Podezřelý soubor nebude flashován.", currentBootTarget: "Aktuální boot: {current}. Další cíl flashování: {target}.", writingOffset: "Zápis na offset {offset}...", partitionUnexpected: "Neočekávaná konfigurace oddílů. Tento prohlížečový flasher podporuje jen zařízení s výchozí OTA tabulkou CrossPoint/Xteink.", unknownFlashError: "Neznámá chyba flashování", flashComplete: "Flash dokončen. Zařízení by mělo nabootovat do CPR-vCodex.", flashStopped: "Flash zastaven. Před dalším pokusem zkontrolujte zprávu výše.", downloadBin: "Stáhnout .bin", communityFlasher: "Komunitní flasher", flashNote: "Automatický flasher používá kopii firmwaru publikovanou s touto stránkou. Tlačítko stažení používá asset z GitHub release.", manualTitle: "Ruční flash přes PlatformIO", manualText: "Pro vývojové buildy naklonujte repo, nastavte upload port a použijte PlatformIO.", stepPending: "Čeká", stepRunning: "Běží", stepDone: "Hotovo", stepFailed: "Chyba", stepConnect: "Připojit zařízení", stepPartition: "Ověřit tabulku oddílů", stepDownload: "Stáhnout firmware", stepReadOtadata: "Číst oddíl otadata", stepFlashApp: "Flashovat app oddíl", stepFlashOtadata: "Flashovat oddíl otadata", stepReset: "Resetovat zařízení"
    },
    da: {
      navHome: "Hjem", navStats: "Statistikeditor", darkMode: "Mørk tilstand", lightMode: "Lys tilstand", homeSubtitle: "Små browserværktøjer til CPR-vCodex. Alt kører lokalt i din browser.", latestFirmware: "Nyeste firmware: 1.5.0.24-cpr-vcodex", homeStats: "Statistikeditor",
      flashEyebrow: "Browserbaseret installer", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Installer den nyeste CPR-vCodex-firmware på din Xteink X3/X4 fra Chrome eller Edge med Web Serial. Den skriver den inaktive app-partition og skifter derefter boot til den.",
      beforeFlash: "Før du flasher", backupTitle: "Tag backup først", backupText: "Denne OTA-lignende flash bevarer bootloader, partitionstabel, SD-kort og NVS-indstillinger, men backup af SD-kortet er stadig fornuftigt.", cableTitle: "Brug et USB-C datakabel", cableText: "Tilslut Xteink X3/X4 til computeren. Hvis ingen seriel port vises, væk og lås enheden op, og tilslut igen.", browserTitle: "Brug Chrome eller Edge på desktop", browserText: "Firefox og Safari understøtter ikke Web Serial. Browseren beder dig vælge ESP32-C3 USB-serielporten.", unplugTitle: "Afbryd ikke under flash", unplugText: "Siden validerer partitionstabellen, skriver den inaktive OTA-appslot, opdaterer <code>otadata</code> og nulstiller enheden, når den er færdig.",
      latestPackage: "Nyeste firmwarepakke", flashWarning: "Brug på eget ansvar. Flash af brugerdefineret firmware kan fejle, hvis kablet afbrydes eller enheden mister strøm.", unsupported: "Web Serial er ikke tilgængelig i denne browser. Brug Chrome eller Edge på en desktop, eller download binærfilen manuelt.", flashButton: "Flash CPR-vCodex firmware", flashing: "Flasher...", readyStatus: "Klar. Klik på Flash for at åbne browserens serielle enhedsvælger.", choosePort: "Vælg ESP32-C3-serielporten i browserens prompt.", openingConnection: "Åbner bootloaderforbindelse...", partitionOk: "Standard CrossPoint/Xteink OTA-partitionstabel fundet.", fetchingFirmware: "Downloader firmwarepakke...",
      firmwareDownloadFailed: "Firmwaredownload mislykkedes", firmwareTooLarge: "Firmwaren er for stor til app-partitionen: {size}.", firmwareTooSmall: "Firmwaren ser for lille ud. Mistænkelig fil flashes ikke.", currentBootTarget: "Aktuel boot: {current}. Næste flashmål: {target}.", writingOffset: "Skriver til offset {offset}...", partitionUnexpected: "Uventet partitionskonfiguration. Denne browserflasher understøtter kun enheder med standard CrossPoint/Xteink OTA-tabellen.", unknownFlashError: "Ukendt flashfejl", flashComplete: "Flash fuldført. Enheden bør genstarte i CPR-vCodex.", flashStopped: "Flash stoppet. Tjek beskeden ovenfor før du prøver igen.", downloadBin: "Download .bin", communityFlasher: "Community-flasher", flashNote: "Den automatiske flasher bruger firmwarekopien publiceret med denne side. Downloadknappen bruger GitHub release-asset.", manualTitle: "Manuel PlatformIO flash", manualText: "For udviklingsbuilds, klon repoet, indstil uploadporten og brug PlatformIO.", stepPending: "Afventer", stepRunning: "Kører", stepDone: "Færdig", stepFailed: "Fejl", stepConnect: "Forbind til enhed", stepPartition: "Valider partitionstabel", stepDownload: "Download firmware", stepReadOtadata: "Læs otadata-partition", stepFlashApp: "Flash app-partition", stepFlashOtadata: "Flash otadata-partition", stepReset: "Nulstil enhed"
    },
    fi: {
      navHome: "Etusivu", navStats: "Tilastoeditori", darkMode: "Tumma tila", lightMode: "Vaalea tila", homeSubtitle: "Pieniä selaintyökaluja CPR-vCodexille. Kaikki toimii paikallisesti selaimessasi.", latestFirmware: "Uusin firmware: 1.5.0.24-cpr-vcodex", homeStats: "Tilastoeditori",
      flashEyebrow: "Selainpohjainen asennin", flashTitle: "Flashaa CPR-vCodex", flashSubtitle: "Asenna uusin CPR-vCodex-firmware Xteink X3/X4:ään Chromella tai Edgellä Web Serialin avulla. Se kirjoittaa passiivisen app-osion ja vaihtaa käynnistyksen siihen.",
      beforeFlash: "Ennen flashausta", backupTitle: "Ota ensin varmuuskopio", backupText: "Tämä OTA-tyylinen flash säilyttää bootloaderin, osiotaulun, SD-kortin ja NVS-asetukset, mutta SD-kortin varmuuskopio on silti järkevä.", cableTitle: "Käytä data-USB-C-kaapelia", cableText: "Liitä Xteink X3/X4 tietokoneeseen. Jos sarjaporttia ei näy, herätä ja avaa laite ja liitä se uudelleen.", browserTitle: "Käytä Chromea tai Edgeä työpöydällä", browserText: "Firefox ja Safari eivät tue Web Serialia. Selain pyytää valitsemaan ESP32-C3 USB -sarjaportin.", unplugTitle: "Älä irrota flashauksen aikana", unplugText: "Sivu tarkistaa osiotaulun, kirjoittaa passiivisen OTA-app-slotin, päivittää <code>otadata</code>-osion ja nollaa laitteen lopuksi.",
      latestPackage: "Uusin firmwarepaketti", flashWarning: "Käytä omalla vastuulla. Mukautetun firmwaren flashaus voi epäonnistua, jos kaapeli irtoaa tai laite menettää virran.", unsupported: "Web Serial ei ole käytettävissä tässä selaimessa. Käytä Chromea tai Edgeä työpöytäkoneella tai lataa binääri käsin.", flashButton: "Flashaa CPR-vCodex-firmware", flashing: "Flashataan...", readyStatus: "Valmis. Flash-painike avaa selaimen sarjalaitteen valitsimen.", choosePort: "Valitse ESP32-C3-sarjaportti selaimen kehotteessa.", openingConnection: "Avataan bootloader-yhteyttä...", partitionOk: "Oletus CrossPoint/Xteink OTA -osiotaulu löytyi.", fetchingFirmware: "Ladataan firmwarepakettia...",
      firmwareDownloadFailed: "Firmwaren lataus epäonnistui", firmwareTooLarge: "Firmware on liian suuri app-osiolle: {size}.", firmwareTooSmall: "Firmware näyttää liian pieneltä. Epäilyttävää tiedostoa ei flashata.", currentBootTarget: "Nykyinen boot: {current}. Seuraava flash-kohde: {target}.", writingOffset: "Kirjoitetaan offsetiin {offset}...", partitionUnexpected: "Odottamaton osiomääritys. Tämä selainflasher tukee vain oletus CrossPoint/Xteink OTA -osiotaulua.", unknownFlashError: "Tuntematon flashausvirhe", flashComplete: "Flash valmis. Laitteen pitäisi käynnistyä CPR-vCodexiin.", flashStopped: "Flash pysäytetty. Tarkista yllä oleva viesti ennen uutta yritystä.", downloadBin: "Lataa .bin", communityFlasher: "Yhteisöflasher", flashNote: "Automaattinen flasher käyttää tämän sivun kanssa julkaistua firmwarekopiota. Latauspainike käyttää GitHub-releasen assettia.", manualTitle: "Manuaalinen PlatformIO-flash", manualText: "Kehitysversioita varten kloonaa repo, aseta upload-portti ja käytä PlatformIO:ta.", stepPending: "Odottaa", stepRunning: "Käynnissä", stepDone: "Valmis", stepFailed: "Virhe", stepConnect: "Yhdistä laitteeseen", stepPartition: "Tarkista osiotaulu", stepDownload: "Lataa firmware", stepReadOtadata: "Lue otadata-osio", stepFlashApp: "Flashaa app-osio", stepFlashOtadata: "Flashaa otadata-osio", stepReset: "Nollaa laite"
    },
    pl: {
      navHome: "Start", navStats: "Edytor statystyk", darkMode: "Tryb ciemny", lightMode: "Tryb jasny", homeSubtitle: "Małe narzędzia przeglądarkowe dla CPR-vCodex. Wszystko działa lokalnie w przeglądarce.", latestFirmware: "Najnowszy firmware: 1.5.0.24-cpr-vcodex", homeStats: "Edytor statystyk",
      flashEyebrow: "Instalator w przeglądarce", flashTitle: "Flash CPR-vCodex", flashSubtitle: "Zainstaluj najnowszy firmware CPR-vCodex na Xteink X3/X4 z Chrome lub Edge przez Web Serial. Zapisuje nieaktywną partycję app i przełącza na nią bootowanie.",
      beforeFlash: "Przed flashowaniem", backupTitle: "Najpierw kopia zapasowa", backupText: "Flash w stylu OTA zachowuje bootloader, tablicę partycji, kartę SD i ustawienia NVS, ale kopia SD nadal jest rozsądna.", cableTitle: "Użyj kabla USB-C do danych", cableText: "Podłącz Xteink X3/X4 do komputera. Jeśli port szeregowy się nie pojawia, wybudź i odblokuj urządzenie, a potem podłącz ponownie.", browserTitle: "Użyj Chrome lub Edge na desktopie", browserText: "Firefox i Safari nie obsługują Web Serial. Przeglądarka poprosi o wybór portu USB szeregowego ESP32-C3.", unplugTitle: "Nie odłączaj podczas flashowania", unplugText: "Strona sprawdza tablicę partycji, zapisuje nieaktywny slot OTA, aktualizuje <code>otadata</code> i resetuje urządzenie po zakończeniu.",
      latestPackage: "Najnowszy pakiet firmware", flashWarning: "Używasz na własne ryzyko. Flash niestandardowego firmware może się nie udać, jeśli kabel zostanie odłączony lub urządzenie straci zasilanie.", unsupported: "Web Serial nie jest dostępny w tej przeglądarce. Użyj Chrome lub Edge na komputerze desktopowym albo pobierz binarkę ręcznie.", flashButton: "Flashuj firmware CPR-vCodex", flashing: "Flashowanie...", readyStatus: "Gotowe. Kliknięcie Flash otworzy wybór urządzenia szeregowego w przeglądarce.", choosePort: "Wybierz port szeregowy ESP32-C3 w oknie przeglądarki.", openingConnection: "Otwieranie połączenia z bootloaderem...", partitionOk: "Znaleziono domyślną tablicę partycji OTA CrossPoint/Xteink.", fetchingFirmware: "Pobieranie pakietu firmware...",
      firmwareDownloadFailed: "Pobieranie firmware nie powiodło się", firmwareTooLarge: "Firmware jest za duży dla partycji app: {size}.", firmwareTooSmall: "Firmware wygląda na zbyt mały. Podejrzany plik nie zostanie flashowany.", currentBootTarget: "Aktualny boot: {current}. Następny cel flashowania: {target}.", writingOffset: "Zapis do offsetu {offset}...", partitionUnexpected: "Nieoczekiwana konfiguracja partycji. Ten flasher obsługuje tylko urządzenia z domyślną tabelą OTA CrossPoint/Xteink.", unknownFlashError: "Nieznany błąd flashowania", flashComplete: "Flash zakończony. Urządzenie powinno uruchomić CPR-vCodex.", flashStopped: "Flash zatrzymany. Sprawdź komunikat powyżej przed kolejną próbą.", downloadBin: "Pobierz .bin", communityFlasher: "Flasher społeczności", flashNote: "Automatyczny flasher używa kopii firmware opublikowanej z tą stroną. Przycisk pobierania używa assetu z release GitHub.", manualTitle: "Ręczny flash przez PlatformIO", manualText: "Dla buildów deweloperskich sklonuj repo, ustaw port uploadu i użyj PlatformIO.", stepPending: "Oczekuje", stepRunning: "W toku", stepDone: "Gotowe", stepFailed: "Błąd", stepConnect: "Połącz z urządzeniem", stepPartition: "Sprawdź tablicę partycji", stepDownload: "Pobierz firmware", stepReadOtadata: "Czytaj partycję otadata", stepFlashApp: "Flashuj partycję app", stepFlashOtadata: "Flashuj partycję otadata", stepReset: "Resetuj urządzenie"
    },
    ru: {
      navHome: "Главная", navStats: "Редактор статистики", darkMode: "Тёмный режим", lightMode: "Светлый режим", homeSubtitle: "Небольшие браузерные инструменты для CPR-vCodex. Всё работает локально в браузере.", latestFirmware: "Последняя прошивка: 1.5.0.24-cpr-vcodex", homeStats: "Редактор статистики",
      flashEyebrow: "Установщик в браузере", flashTitle: "Прошить CPR-vCodex", flashSubtitle: "Установите последнюю прошивку CPR-vCodex на Xteink X3/X4 из Chrome или Edge через Web Serial. Она записывает неактивный app-раздел и переключает загрузку на него.",
      beforeFlash: "Перед прошивкой", backupTitle: "Сначала сделайте копию", backupText: "OTA-прошивка сохраняет bootloader, таблицу разделов, SD-карту и настройки NVS, но резервная копия SD всё равно полезна.", cableTitle: "Используйте USB-C кабель для данных", cableText: "Подключите Xteink X3/X4 к компьютеру. Если последовательный порт не появился, разбудите и разблокируйте устройство, затем подключите снова.", browserTitle: "Используйте Chrome или Edge на компьютере", browserText: "Firefox и Safari не поддерживают Web Serial. Браузер попросит выбрать USB последовательный порт ESP32-C3.", unplugTitle: "Не отключайте во время прошивки", unplugText: "Страница проверяет таблицу разделов, записывает неактивный OTA app slot, обновляет <code>otadata</code> и перезагружает устройство после завершения.",
      latestPackage: "Последний пакет прошивки", flashWarning: "Используйте на свой риск. Прошивка кастомного firmware может не удаться, если кабель отключится или устройство потеряет питание.", unsupported: "Web Serial недоступен в этом браузере. Используйте Chrome или Edge на компьютере либо скачайте бинарный файл вручную.", flashButton: "Прошить CPR-vCodex", flashing: "Прошивка...", readyStatus: "Готово. Нажатие Flash откроет выбор последовательного устройства в браузере.", choosePort: "Выберите последовательный порт ESP32-C3 в окне браузера.", openingConnection: "Открытие соединения с bootloader...", partitionOk: "Найдена стандартная OTA таблица разделов CrossPoint/Xteink.", fetchingFirmware: "Загрузка пакета прошивки...",
      firmwareDownloadFailed: "Не удалось скачать прошивку", firmwareTooLarge: "Прошивка слишком велика для app-раздела: {size}.", firmwareTooSmall: "Прошивка выглядит слишком маленькой. Подозрительный файл не будет прошит.", currentBootTarget: "Текущая загрузка: {current}. Следующая цель прошивки: {target}.", writingOffset: "Запись по offset {offset}...", partitionUnexpected: "Неожиданная конфигурация разделов. Этот браузерный flasher поддерживает только устройства со стандартной OTA таблицей CrossPoint/Xteink.", unknownFlashError: "Неизвестная ошибка прошивки", flashComplete: "Прошивка завершена. Устройство должно загрузиться в CPR-vCodex.", flashStopped: "Прошивка остановлена. Проверьте сообщение выше перед повторной попыткой.", downloadBin: "Скачать .bin", communityFlasher: "Community flasher", flashNote: "Автоматический flasher использует копию прошивки, опубликованную с этой страницей. Кнопка загрузки использует asset из GitHub release.", manualTitle: "Ручная прошивка через PlatformIO", manualText: "Для dev-сборок клонируйте repo, задайте upload port и используйте PlatformIO.", stepPending: "Ожидает", stepRunning: "Выполняется", stepDone: "Готово", stepFailed: "Ошибка", stepConnect: "Подключить устройство", stepPartition: "Проверить таблицу разделов", stepDownload: "Скачать прошивку", stepReadOtadata: "Прочитать раздел otadata", stepFlashApp: "Прошить app-раздел", stepFlashOtadata: "Прошить otadata", stepReset: "Перезагрузить устройство"
    }
  });

  const genericLanguageNames = {
    be: "Беларуская", cs: "Čeština", da: "Dansk", fi: "Suomi", hu: "Magyar", kk: "Қазақша", lt: "Lietuvių", pl: "Polski", ro: "Română", ru: "Русский", si: "Slovenščina", sv: "Svenska", tr: "Türkçe", uk: "Українська", vi: "Tiếng Việt"
  };
  for (const language of Object.keys(genericLanguageNames)) {
    messages[language] = messages[language] || {...messages.en};
    messages[language] = {
      ...messages.en,
      ...messages[language],
      navStats: genericLanguageNames[language] === "Español" ? messages.es.navStats : messages[language].navStats || messages.en.navStats
    };
  }

  const localizedTools = {
    be: {
      navTools: "Інструменты",
      navSupport: "Падтрымаць",
      toolsSubtitle: "Браўзерныя інструменты для CPR-vCodex і карысныя знешнія сэрвісы для Xteink.",
      toolsStatsTitle: "Рэдактар статыстыкі чытання CPR-vCodex",
      toolsStatsBody: "Лакальны рэдактар экспартаванай статыстыкі. Без загрузкі на сервер.",
      toolsOpenEditor: "Адкрыць рэдактар",
      toolsRecommendations: "Рэкамендацыі",
      toolsEpubkitBody: "Лепшы інструмент для аптымізацыі EPUB для прылад Xteink.",
      toolsXtcjsBody: "Лепшы інструмент для аптымізацыі коміксаў і мангі CBZ/CBR для Xteink.",
      toolsWallpaperConverterBody: "Стварайце ўласныя адаптаваныя шпалеры.",
      toolsWallpaperCollectionBody: "Калекцыя адаптаваных шпалер.",
      toolsOpenEpubkit: "Адкрыць epubkit.ink",
      toolsOpenXtcjs: "Адкрыць xtcjs.app",
      toolsOpenConverter: "Адкрыць канвертар",
      toolsOpenCollection: "Адкрыць калекцыю"
    },
    ca: {
      navTools: "Eines",
      navSupport: "Suport",
      toolsSubtitle: "Eines de navegador per a CPR-vCodex i utilitats externes útils per al flux Xteink.",
      toolsStatsTitle: "Editor d'estadístiques de lectura CPR-vCodex",
      toolsStatsBody: "Editor local per a estadístiques exportades. Sense pujades, sense servidor.",
      toolsOpenEditor: "Obrir editor",
      toolsRecommendations: "Recomanacions",
      toolsEpubkitBody: "Millor eina per optimitzar EPUB per a dispositius Xteink.",
      toolsXtcjsBody: "Millor eina per optimitzar còmics i manga CBZ/CBR per a Xteink.",
      toolsWallpaperConverterBody: "Crea els teus propis fons adaptats.",
      toolsWallpaperCollectionBody: "Col·lecció de fons adaptats.",
      toolsOpenEpubkit: "Obrir epubkit.ink",
      toolsOpenXtcjs: "Obrir xtcjs.app",
      toolsOpenConverter: "Obrir conversor",
      toolsOpenCollection: "Obrir col·lecció"
    },
    cs: {
      navTools: "Nástroje",
      navSupport: "Podpora",
      toolsSubtitle: "Nástroje v prohlížeči pro CPR-vCodex a užitečné externí služby pro Xteink.",
      toolsStatsTitle: "Editor statistik čtení CPR-vCodex",
      toolsStatsBody: "Lokální editor exportovaných statistik. Bez nahrávání, bez serveru.",
      toolsOpenEditor: "Otevřít editor",
      toolsRecommendations: "Doporučení",
      toolsEpubkitBody: "Nejlepší nástroj pro optimalizaci EPUB pro zařízení Xteink.",
      toolsXtcjsBody: "Nejlepší nástroj pro optimalizaci komiksů a mangy CBZ/CBR pro Xteink.",
      toolsWallpaperConverterBody: "Vytvořte si vlastní přizpůsobené tapety.",
      toolsWallpaperCollectionBody: "Kolekce přizpůsobených tapet.",
      toolsOpenEpubkit: "Otevřít epubkit.ink",
      toolsOpenXtcjs: "Otevřít xtcjs.app",
      toolsOpenConverter: "Otevřít převodník",
      toolsOpenCollection: "Otevřít kolekci"
    },
    da: {
      navTools: "Værktøjer",
      navSupport: "Støtte",
      toolsSubtitle: "Browserværktøjer til CPR-vCodex og nyttige eksterne tjenester til Xteink.",
      toolsStatsTitle: "CPR-vCodex læsestatistik-editor",
      toolsStatsBody: "Lokal editor til eksporteret læsestatistik. Ingen upload, ingen server.",
      toolsOpenEditor: "Åbn editor",
      toolsRecommendations: "Anbefalinger",
      toolsEpubkitBody: "Bedste værktøj til at optimere EPUB-filer til Xteink-enheder.",
      toolsXtcjsBody: "Bedste værktøj til at optimere CBZ/CBR-tegneserier og manga til Xteink.",
      toolsWallpaperConverterBody: "Lav dine egne tilpassede baggrunde.",
      toolsWallpaperCollectionBody: "Samling af tilpassede baggrunde.",
      toolsOpenEpubkit: "Åbn epubkit.ink",
      toolsOpenXtcjs: "Åbn xtcjs.app",
      toolsOpenConverter: "Åbn konverter",
      toolsOpenCollection: "Åbn samling"
    },
    nl: {
      navTools: "Hulpmiddelen",
      navSupport: "Steun",
      toolsSubtitle: "Browserhulpmiddelen voor CPR-vCodex en nuttige externe diensten voor Xteink.",
      toolsStatsTitle: "CPR-vCodex leesstatistieken-editor",
      toolsStatsBody: "Lokale editor voor geëxporteerde leesstatistieken. Geen upload, geen server.",
      toolsOpenEditor: "Editor openen",
      toolsRecommendations: "Aanbevelingen",
      toolsEpubkitBody: "Beste hulpmiddel om EPUB-bestanden voor Xteink-apparaten te optimaliseren.",
      toolsXtcjsBody: "Beste hulpmiddel om CBZ/CBR-strips en manga voor Xteink te optimaliseren.",
      toolsWallpaperConverterBody: "Maak je eigen aangepaste achtergronden.",
      toolsWallpaperCollectionBody: "Collectie aangepaste achtergronden.",
      toolsOpenEpubkit: "Open epubkit.ink",
      toolsOpenXtcjs: "Open xtcjs.app",
      toolsOpenConverter: "Open converter",
      toolsOpenCollection: "Open collectie"
    },
    fi: {
      navTools: "Työkalut",
      navSupport: "Tuki",
      toolsSubtitle: "Selaintyökaluja CPR-vCodexille ja hyödyllisiä ulkoisia palveluja Xteinkille.",
      toolsStatsTitle: "CPR-vCodex-lukutilastojen editori",
      toolsStatsBody: "Paikallinen editori viedyille lukutilastoille. Ei lähetystä, ei palvelinta.",
      toolsOpenEditor: "Avaa editori",
      toolsRecommendations: "Suositukset",
      toolsEpubkitBody: "Paras työkalu EPUB-tiedostojen optimointiin Xteink-laitteille.",
      toolsXtcjsBody: "Paras työkalu CBZ/CBR-sarjakuvien ja mangan optimointiin Xteinkille.",
      toolsWallpaperConverterBody: "Luo omat mukautetut taustakuvat.",
      toolsWallpaperCollectionBody: "Kokoelma mukautettuja taustakuvia.",
      toolsOpenEpubkit: "Avaa epubkit.ink",
      toolsOpenXtcjs: "Avaa xtcjs.app",
      toolsOpenConverter: "Avaa muunnin",
      toolsOpenCollection: "Avaa kokoelma"
    },
    fr: {
      navTools: "Outils",
      navSupport: "Soutien",
      toolsSubtitle: "Outils de navigateur pour CPR-vCodex et services externes utiles pour Xteink.",
      toolsStatsTitle: "Éditeur de statistiques de lecture CPR-vCodex",
      toolsStatsBody: "Éditeur local pour les statistiques exportées. Aucun envoi, aucun serveur.",
      toolsOpenEditor: "Ouvrir l'éditeur",
      toolsRecommendations: "Recommandations",
      toolsEpubkitBody: "Meilleur outil pour optimiser les EPUB pour les appareils Xteink.",
      toolsXtcjsBody: "Meilleur outil pour optimiser les comics et mangas CBZ/CBR pour Xteink.",
      toolsWallpaperConverterBody: "Créez vos propres fonds adaptés.",
      toolsWallpaperCollectionBody: "Collection de fonds adaptés.",
      toolsOpenEpubkit: "Ouvrir epubkit.ink",
      toolsOpenXtcjs: "Ouvrir xtcjs.app",
      toolsOpenConverter: "Ouvrir le convertisseur",
      toolsOpenCollection: "Ouvrir la collection"
    },
    de: {
      navTools: "Werkzeuge",
      navSupport: "Unterstützen",
      toolsSubtitle: "Browser-Werkzeuge für CPR-vCodex und nützliche externe Dienste für Xteink.",
      toolsStatsTitle: "CPR-vCodex Lesestatistik-Editor",
      toolsStatsBody: "Lokaler Editor für exportierte Lesestatistiken. Kein Upload, kein Server.",
      toolsOpenEditor: "Editor öffnen",
      toolsRecommendations: "Empfehlungen",
      toolsEpubkitBody: "Bestes Werkzeug zum Optimieren von EPUB-Dateien für Xteink-Geräte.",
      toolsXtcjsBody: "Bestes Werkzeug zum Optimieren von CBZ/CBR-Comics und Manga für Xteink.",
      toolsWallpaperConverterBody: "Erstelle eigene angepasste Hintergrundbilder.",
      toolsWallpaperCollectionBody: "Sammlung angepasster Hintergrundbilder.",
      toolsOpenEpubkit: "epubkit.ink öffnen",
      toolsOpenXtcjs: "xtcjs.app öffnen",
      toolsOpenConverter: "Konverter öffnen",
      toolsOpenCollection: "Sammlung öffnen"
    },
    hu: {
      navTools: "Eszközök",
      navSupport: "Támogatás",
      toolsSubtitle: "Böngészős eszközök CPR-vCodexhez és hasznos külső szolgáltatások Xteinkhez.",
      toolsStatsTitle: "CPR-vCodex olvasási statisztika szerkesztő",
      toolsStatsBody: "Helyi szerkesztő exportált statisztikákhoz. Nincs feltöltés, nincs szerver.",
      toolsOpenEditor: "Szerkesztő megnyitása",
      toolsRecommendations: "Ajánlások",
      toolsEpubkitBody: "A legjobb eszköz EPUB-fájlok optimalizálásához Xteink eszközökre.",
      toolsXtcjsBody: "A legjobb eszköz CBZ/CBR képregények és mangák optimalizálásához Xteinkre.",
      toolsWallpaperConverterBody: "Készíts saját igazított háttérképeket.",
      toolsWallpaperCollectionBody: "Igazított háttérképek gyűjteménye.",
      toolsOpenEpubkit: "epubkit.ink megnyitása",
      toolsOpenXtcjs: "xtcjs.app megnyitása",
      toolsOpenConverter: "Konverter megnyitása",
      toolsOpenCollection: "Gyűjtemény megnyitása"
    },
    it: {
      navTools: "Strumenti",
      navSupport: "Supporto",
      toolsSubtitle: "Strumenti browser per CPR-vCodex e servizi esterni utili per Xteink.",
      toolsStatsTitle: "Editor statistiche di lettura CPR-vCodex",
      toolsStatsBody: "Editor locale per statistiche esportate. Nessun upload, nessun server.",
      toolsOpenEditor: "Apri editor",
      toolsRecommendations: "Consigli",
      toolsEpubkitBody: "Miglior strumento per ottimizzare EPUB per dispositivi Xteink.",
      toolsXtcjsBody: "Miglior strumento per ottimizzare fumetti e manga CBZ/CBR per Xteink.",
      toolsWallpaperConverterBody: "Crea i tuoi sfondi adattati.",
      toolsWallpaperCollectionBody: "Collezione di sfondi adattati.",
      toolsOpenEpubkit: "Apri epubkit.ink",
      toolsOpenXtcjs: "Apri xtcjs.app",
      toolsOpenConverter: "Apri convertitore",
      toolsOpenCollection: "Apri collezione"
    },
    kk: {
      navTools: "Құралдар",
      navSupport: "Қолдау",
      toolsSubtitle: "CPR-vCodex үшін браузер құралдары және Xteink үшін пайдалы сыртқы сервистер.",
      toolsStatsTitle: "CPR-vCodex оқу статистикасы редакторы",
      toolsStatsBody: "Экспортталған статистикаға арналған жергілікті редактор. Жүктеу жоқ, сервер жоқ.",
      toolsOpenEditor: "Редакторды ашу",
      toolsRecommendations: "Ұсыныстар",
      toolsEpubkitBody: "Xteink құрылғылары үшін EPUB файлдарын оңтайландыруға арналған ең жақсы құрал.",
      toolsXtcjsBody: "Xteink үшін CBZ/CBR комикстері мен мангасын оңтайландыруға арналған ең жақсы құрал.",
      toolsWallpaperConverterBody: "Өзіңізге бейімделген тұсқағаздар жасаңыз.",
      toolsWallpaperCollectionBody: "Бейімделген тұсқағаздар жинағы.",
      toolsOpenEpubkit: "epubkit.ink ашу",
      toolsOpenXtcjs: "xtcjs.app ашу",
      toolsOpenConverter: "Конвертерді ашу",
      toolsOpenCollection: "Жинақты ашу"
    },
    lt: {
      navTools: "Įrankiai",
      navSupport: "Palaikyti",
      toolsSubtitle: "Naršyklės įrankiai CPR-vCodex ir naudingi išoriniai servisai Xteink.",
      toolsStatsTitle: "CPR-vCodex skaitymo statistikos redaktorius",
      toolsStatsBody: "Vietinis eksportuotos statistikos redaktorius. Be įkėlimo, be serverio.",
      toolsOpenEditor: "Atidaryti redaktorių",
      toolsRecommendations: "Rekomendacijos",
      toolsEpubkitBody: "Geriausias įrankis EPUB failams optimizuoti Xteink įrenginiams.",
      toolsXtcjsBody: "Geriausias įrankis CBZ/CBR komiksams ir mangai optimizuoti Xteink.",
      toolsWallpaperConverterBody: "Kurkite savo pritaikytus fonus.",
      toolsWallpaperCollectionBody: "Pritaikytų fonų kolekcija.",
      toolsOpenEpubkit: "Atidaryti epubkit.ink",
      toolsOpenXtcjs: "Atidaryti xtcjs.app",
      toolsOpenConverter: "Atidaryti konverterį",
      toolsOpenCollection: "Atidaryti kolekciją"
    },
    pl: {
      navTools: "Narzędzia",
      navSupport: "Wesprzyj",
      toolsSubtitle: "Narzędzia przeglądarkowe dla CPR-vCodex i przydatne zewnętrzne usługi dla Xteink.",
      toolsStatsTitle: "Edytor statystyk czytania CPR-vCodex",
      toolsStatsBody: "Lokalny edytor wyeksportowanych statystyk. Bez wysyłania, bez serwera.",
      toolsOpenEditor: "Otwórz edytor",
      toolsRecommendations: "Rekomendacje",
      toolsEpubkitBody: "Najlepsze narzędzie do optymalizacji EPUB dla urządzeń Xteink.",
      toolsXtcjsBody: "Najlepsze narzędzie do optymalizacji komiksów i mangi CBZ/CBR dla Xteink.",
      toolsWallpaperConverterBody: "Twórz własne dopasowane tapety.",
      toolsWallpaperCollectionBody: "Kolekcja dopasowanych tapet.",
      toolsOpenEpubkit: "Otwórz epubkit.ink",
      toolsOpenXtcjs: "Otwórz xtcjs.app",
      toolsOpenConverter: "Otwórz konwerter",
      toolsOpenCollection: "Otwórz kolekcję"
    },
    pt: {
      navTools: "Ferramentas",
      navSupport: "Apoiar",
      toolsSubtitle: "Ferramentas de navegador para CPR-vCodex e serviços externos úteis para Xteink.",
      toolsStatsTitle: "Editor de estatísticas de leitura CPR-vCodex",
      toolsStatsBody: "Editor local para estatísticas exportadas. Sem upload, sem servidor.",
      toolsOpenEditor: "Abrir editor",
      toolsRecommendations: "Recomendações",
      toolsEpubkitBody: "Melhor ferramenta para otimizar EPUB para dispositivos Xteink.",
      toolsXtcjsBody: "Melhor ferramenta para otimizar quadrinhos e mangás CBZ/CBR para Xteink.",
      toolsWallpaperConverterBody: "Crie seus próprios papéis de parede adaptados.",
      toolsWallpaperCollectionBody: "Coleção de papéis de parede adaptados.",
      toolsOpenEpubkit: "Abrir epubkit.ink",
      toolsOpenXtcjs: "Abrir xtcjs.app",
      toolsOpenConverter: "Abrir conversor",
      toolsOpenCollection: "Abrir coleção"
    },
    ro: {
      navTools: "Unelte",
      navSupport: "Susține",
      toolsSubtitle: "Unelte în browser pentru CPR-vCodex și servicii externe utile pentru Xteink.",
      toolsStatsTitle: "Editor statistici de citire CPR-vCodex",
      toolsStatsBody: "Editor local pentru statistici exportate. Fără upload, fără server.",
      toolsOpenEditor: "Deschide editorul",
      toolsRecommendations: "Recomandări",
      toolsEpubkitBody: "Cea mai bună unealtă pentru optimizarea EPUB pentru dispozitive Xteink.",
      toolsXtcjsBody: "Cea mai bună unealtă pentru optimizarea benzilor desenate și manga CBZ/CBR pentru Xteink.",
      toolsWallpaperConverterBody: "Creează propriile fundaluri adaptate.",
      toolsWallpaperCollectionBody: "Colecție de fundaluri adaptate.",
      toolsOpenEpubkit: "Deschide epubkit.ink",
      toolsOpenXtcjs: "Deschide xtcjs.app",
      toolsOpenConverter: "Deschide convertorul",
      toolsOpenCollection: "Deschide colecția"
    },
    ru: {
      navTools: "Инструменты",
      navSupport: "Поддержать",
      toolsSubtitle: "Браузерные инструменты для CPR-vCodex и полезные внешние сервисы для Xteink.",
      toolsStatsTitle: "Редактор статистики чтения CPR-vCodex",
      toolsStatsBody: "Локальный редактор экспортированной статистики. Без загрузки, без сервера.",
      toolsOpenEditor: "Открыть редактор",
      toolsRecommendations: "Рекомендации",
      toolsEpubkitBody: "Лучший инструмент для оптимизации EPUB для устройств Xteink.",
      toolsXtcjsBody: "Лучший инструмент для оптимизации комиксов и манги CBZ/CBR для Xteink.",
      toolsWallpaperConverterBody: "Создавайте собственные адаптированные обои.",
      toolsWallpaperCollectionBody: "Коллекция адаптированных обоев.",
      toolsOpenEpubkit: "Открыть epubkit.ink",
      toolsOpenXtcjs: "Открыть xtcjs.app",
      toolsOpenConverter: "Открыть конвертер",
      toolsOpenCollection: "Открыть коллекцию"
    },
    si: {
      navTools: "Orodja",
      navSupport: "Podpora",
      toolsSubtitle: "Brskalniška orodja za CPR-vCodex in uporabne zunanje storitve za Xteink.",
      toolsStatsTitle: "Urejevalnik bralnih statistik CPR-vCodex",
      toolsStatsBody: "Lokalni urejevalnik izvoženih statistik. Brez nalaganja, brez strežnika.",
      toolsOpenEditor: "Odpri urejevalnik",
      toolsRecommendations: "Priporočila",
      toolsEpubkitBody: "Najboljše orodje za optimizacijo EPUB za naprave Xteink.",
      toolsXtcjsBody: "Najboljše orodje za optimizacijo CBZ/CBR stripov in mang za Xteink.",
      toolsWallpaperConverterBody: "Ustvarite lastna prilagojena ozadja.",
      toolsWallpaperCollectionBody: "Zbirka prilagojenih ozadij.",
      toolsOpenEpubkit: "Odpri epubkit.ink",
      toolsOpenXtcjs: "Odpri xtcjs.app",
      toolsOpenConverter: "Odpri pretvornik",
      toolsOpenCollection: "Odpri zbirko"
    },
    sv: {
      navTools: "Verktyg",
      navSupport: "Stöd",
      toolsSubtitle: "Webbläsarverktyg för CPR-vCodex och användbara externa tjänster för Xteink.",
      toolsStatsTitle: "CPR-vCodex lässtatistikredigerare",
      toolsStatsBody: "Lokal redigerare för exporterad statistik. Ingen uppladdning, ingen server.",
      toolsOpenEditor: "Öppna redigerare",
      toolsRecommendations: "Rekommendationer",
      toolsEpubkitBody: "Bästa verktyget för att optimera EPUB-filer för Xteink-enheter.",
      toolsXtcjsBody: "Bästa verktyget för att optimera CBZ/CBR-serier och manga för Xteink.",
      toolsWallpaperConverterBody: "Skapa egna anpassade bakgrunder.",
      toolsWallpaperCollectionBody: "Samling med anpassade bakgrunder.",
      toolsOpenEpubkit: "Öppna epubkit.ink",
      toolsOpenXtcjs: "Öppna xtcjs.app",
      toolsOpenConverter: "Öppna konverterare",
      toolsOpenCollection: "Öppna samling"
    },
    tr: {
      navTools: "Araçlar",
      navSupport: "Destek",
      toolsSubtitle: "CPR-vCodex için tarayıcı araçları ve Xteink için yararlı harici servisler.",
      toolsStatsTitle: "CPR-vCodex okuma istatistikleri düzenleyici",
      toolsStatsBody: "Dışa aktarılan istatistikler için yerel düzenleyici. Yükleme yok, sunucu yok.",
      toolsOpenEditor: "Düzenleyiciyi aç",
      toolsRecommendations: "Öneriler",
      toolsEpubkitBody: "Xteink cihazları için EPUB dosyalarını optimize etmeye yönelik en iyi araç.",
      toolsXtcjsBody: "Xteink için CBZ/CBR çizgi roman ve mangaları optimize etmeye yönelik en iyi araç.",
      toolsWallpaperConverterBody: "Kendi uyarlanmış duvar kağıtlarınızı oluşturun.",
      toolsWallpaperCollectionBody: "Uyarlanmış duvar kağıtları koleksiyonu.",
      toolsOpenEpubkit: "epubkit.ink aç",
      toolsOpenXtcjs: "xtcjs.app aç",
      toolsOpenConverter: "Dönüştürücüyü aç",
      toolsOpenCollection: "Koleksiyonu aç"
    },
    uk: {
      navTools: "Інструменти",
      navSupport: "Підтримати",
      toolsSubtitle: "Браузерні інструменти для CPR-vCodex і корисні зовнішні сервіси для Xteink.",
      toolsStatsTitle: "Редактор статистики читання CPR-vCodex",
      toolsStatsBody: "Локальний редактор експортованої статистики. Без завантаження, без сервера.",
      toolsOpenEditor: "Відкрити редактор",
      toolsRecommendations: "Рекомендації",
      toolsEpubkitBody: "Найкращий інструмент для оптимізації EPUB для пристроїв Xteink.",
      toolsXtcjsBody: "Найкращий інструмент для оптимізації коміксів і манги CBZ/CBR для Xteink.",
      toolsWallpaperConverterBody: "Створюйте власні адаптовані шпалери.",
      toolsWallpaperCollectionBody: "Колекція адаптованих шпалер.",
      toolsOpenEpubkit: "Відкрити epubkit.ink",
      toolsOpenXtcjs: "Відкрити xtcjs.app",
      toolsOpenConverter: "Відкрити конвертер",
      toolsOpenCollection: "Відкрити колекцію"
    },
    vi: {
      navTools: "Công cụ",
      navSupport: "Ủng hộ",
      toolsSubtitle: "Công cụ trình duyệt cho CPR-vCodex và các dịch vụ ngoài hữu ích cho Xteink.",
      toolsStatsTitle: "Trình chỉnh sửa thống kê đọc CPR-vCodex",
      toolsStatsBody: "Trình chỉnh sửa cục bộ cho thống kê đã xuất. Không tải lên, không máy chủ.",
      toolsOpenEditor: "Mở trình chỉnh sửa",
      toolsRecommendations: "Gợi ý",
      toolsEpubkitBody: "Công cụ tốt nhất để tối ưu EPUB cho thiết bị Xteink.",
      toolsXtcjsBody: "Công cụ tốt nhất để tối ưu truyện tranh và manga CBZ/CBR cho Xteink.",
      toolsWallpaperConverterBody: "Tạo hình nền đã điều chỉnh của riêng bạn.",
      toolsWallpaperCollectionBody: "Bộ sưu tập hình nền đã điều chỉnh.",
      toolsOpenEpubkit: "Mở epubkit.ink",
      toolsOpenXtcjs: "Mở xtcjs.app",
      toolsOpenConverter: "Mở trình chuyển đổi",
      toolsOpenCollection: "Mở bộ sưu tập"
    }
  };
  for (const [language, values] of Object.entries(localizedTools)) {
    if (!messages[language]) continue;
    const merged = {...values};
    if (values.navTools) {
      merged.homeTools = values.homeTools || values.navTools;
      merged.toolsTitle = values.toolsTitle || values.navTools;
    }
    Object.assign(messages[language], merged);
  }

  const localizedHomeCards = {
    en: {
      homeSubtitle: "Firmware, auto-flash and local browser tools for CPR-vCodex on Xteink X3/X4.",
      homeFlashBody: "Install the latest stable release from the browser.",
      homeToolsBody: "Open the reading stats editor and recommended Xteink utilities.",
      homeGithubBody: "Review releases, issues and project source.",
      homeSupportBody: "Support CPR-vCodex development on Ko-fi."
    },
    es: {
      homeSubtitle: "Firmware, auto flash y herramientas locales de navegador para CPR-vCodex en Xteink X3/X4.",
      homeFlashBody: "Instala la última release estable desde el navegador.",
      homeToolsBody: "Abre el editor de estadísticas y utilidades recomendadas para Xteink.",
      homeGithubBody: "Consulta releases, issues y el código del proyecto.",
      homeSupportBody: "Apoya el desarrollo de CPR-vCodex en Ko-fi."
    },
    ca: {
      homeSubtitle: "Firmware, flash automàtic i eines locals de navegador per a CPR-vCodex a Xteink X3/X4.",
      homeFlashBody: "Instal·la l'última versió estable des del navegador.",
      homeToolsBody: "Obre l'editor d'estadístiques i utilitats recomanades per a Xteink.",
      homeGithubBody: "Consulta versions, incidències i el codi del projecte.",
      homeSupportBody: "Dona suport al desenvolupament de CPR-vCodex a Ko-fi."
    },
    fr: {
      homeSubtitle: "Firmware, flash automatique et outils locaux de navigateur pour CPR-vCodex sur Xteink X3/X4.",
      homeFlashBody: "Installez la dernière version stable depuis le navigateur.",
      homeToolsBody: "Ouvrez l'éditeur de statistiques et les utilitaires recommandés pour Xteink.",
      homeGithubBody: "Consultez les versions, les tickets et le code du projet.",
      homeSupportBody: "Soutenez le développement de CPR-vCodex sur Ko-fi."
    },
    de: {
      homeSubtitle: "Firmware, Auto-Flash und lokale Browser-Werkzeuge für CPR-vCodex auf dem Xteink X3/X4.",
      homeFlashBody: "Installieren Sie die neueste stabile Version im Browser.",
      homeToolsBody: "Öffnen Sie den Lesestatistik-Editor und empfohlene Xteink-Werkzeuge.",
      homeGithubBody: "Prüfen Sie Releases, Issues und den Quellcode.",
      homeSupportBody: "Unterstützen Sie die Entwicklung von CPR-vCodex auf Ko-fi."
    },
    it: {
      homeSubtitle: "Firmware, flash automatico e strumenti locali del browser per CPR-vCodex su Xteink X3/X4.",
      homeFlashBody: "Installa l'ultima release stabile dal browser.",
      homeToolsBody: "Apri l'editor statistiche e le utilità consigliate per Xteink.",
      homeGithubBody: "Consulta release, issue e codice del progetto.",
      homeSupportBody: "Sostieni lo sviluppo di CPR-vCodex su Ko-fi."
    },
    pt: {
      homeSubtitle: "Firmware, flash automático e ferramentas locais de navegador para CPR-vCodex no Xteink X3/X4.",
      homeFlashBody: "Instale a versão estável mais recente pelo navegador.",
      homeToolsBody: "Abra o editor de estatísticas e utilitários recomendados para Xteink.",
      homeGithubBody: "Veja releases, issues e o código do projeto.",
      homeSupportBody: "Apoie o desenvolvimento do CPR-vCodex no Ko-fi."
    },
    nl: {
      homeSubtitle: "Firmware, auto-flash en lokale browserhulpmiddelen voor CPR-vCodex op Xteink X3/X4.",
      homeFlashBody: "Installeer de nieuwste stabiele release vanuit de browser.",
      homeToolsBody: "Open de leesstatistieken-editor en aanbevolen Xteink-hulpmiddelen.",
      homeGithubBody: "Bekijk releases, issues en de projectbroncode.",
      homeSupportBody: "Steun de ontwikkeling van CPR-vCodex op Ko-fi."
    },
    be: {
      homeSubtitle: "Прашыўка, аўта-flash і лакальныя браўзерныя інструменты для CPR-vCodex на Xteink X3/X4.",
      homeFlashBody: "Усталюйце апошні стабільны рэліз з браўзера.",
      homeToolsBody: "Адкрыйце рэдактар статыстыкі і рэкамендаваныя інструменты Xteink.",
      homeGithubBody: "Праглядзіце рэлізы, issues і зыходны код праекта.",
      homeSupportBody: "Падтрымайце распрацоўку CPR-vCodex на Ko-fi."
    },
    cs: {
      homeSubtitle: "Firmware, automatický flash a lokální nástroje v prohlížeči pro CPR-vCodex na Xteink X3/X4.",
      homeFlashBody: "Nainstalujte nejnovější stabilní release z prohlížeče.",
      homeToolsBody: "Otevřete editor statistik a doporučené nástroje pro Xteink.",
      homeGithubBody: "Prohlédněte si releasy, issues a zdrojový kód projektu.",
      homeSupportBody: "Podpořte vývoj CPR-vCodex na Ko-fi."
    },
    da: {
      homeSubtitle: "Firmware, auto-flash og lokale browserværktøjer til CPR-vCodex på Xteink X3/X4.",
      homeFlashBody: "Installer den seneste stabile release fra browseren.",
      homeToolsBody: "Åbn læsestatistik-editoren og anbefalede Xteink-værktøjer.",
      homeGithubBody: "Se releases, issues og projektets kildekode.",
      homeSupportBody: "Støt udviklingen af CPR-vCodex på Ko-fi."
    },
    fi: {
      homeSubtitle: "Firmware, automaattinen flash ja paikalliset selaintyökalut CPR-vCodexille Xteink X3/X4:ssä.",
      homeFlashBody: "Asenna uusin vakaa julkaisu selaimesta.",
      homeToolsBody: "Avaa lukutilastojen editori ja suositellut Xteink-työkalut.",
      homeGithubBody: "Tarkista julkaisut, issuet ja projektin lähdekoodi.",
      homeSupportBody: "Tue CPR-vCodexin kehitystä Ko-fissa."
    },
    hu: {
      homeSubtitle: "Firmware, automatikus flash és helyi böngészős eszközök CPR-vCodexhez Xteink X3/X4-en.",
      homeFlashBody: "Telepítsd a legújabb stabil kiadást a böngészőből.",
      homeToolsBody: "Nyisd meg az olvasási statisztika szerkesztőt és az ajánlott Xteink eszközöket.",
      homeGithubBody: "Nézd meg a kiadásokat, issue-kat és a projekt forrását.",
      homeSupportBody: "Támogasd a CPR-vCodex fejlesztését Ko-fin."
    },
    kk: {
      homeSubtitle: "Xteink X3/X4 үшін CPR-vCodex firmware, авто-flash және жергілікті браузер құралдары.",
      homeFlashBody: "Соңғы тұрақты релизді браузерден орнатыңыз.",
      homeToolsBody: "Оқу статистикасы редакторын және ұсынылған Xteink құралдарын ашыңыз.",
      homeGithubBody: "Релиздерді, issues және жоба кодын қараңыз.",
      homeSupportBody: "CPR-vCodex әзірлеуін Ko-fi арқылы қолдаңыз."
    },
    lt: {
      homeSubtitle: "Programinė įranga, automatinis flash ir vietiniai naršyklės įrankiai CPR-vCodex Xteink X3/X4.",
      homeFlashBody: "Įdiekite naujausią stabilią versiją iš naršyklės.",
      homeToolsBody: "Atidarykite skaitymo statistikos redaktorių ir rekomenduojamus Xteink įrankius.",
      homeGithubBody: "Peržiūrėkite versijas, issues ir projekto kodą.",
      homeSupportBody: "Paremkite CPR-vCodex kūrimą Ko-fi."
    },
    pl: {
      homeSubtitle: "Firmware, auto flash i lokalne narzędzia przeglądarkowe dla CPR-vCodex na Xteink X3/X4.",
      homeFlashBody: "Zainstaluj najnowsze stabilne wydanie z przeglądarki.",
      homeToolsBody: "Otwórz edytor statystyk czytania i polecane narzędzia Xteink.",
      homeGithubBody: "Sprawdź wydania, issues i kod projektu.",
      homeSupportBody: "Wesprzyj rozwój CPR-vCodex na Ko-fi."
    },
    ro: {
      homeSubtitle: "Firmware, flash automat și unelte locale de browser pentru CPR-vCodex pe Xteink X3/X4.",
      homeFlashBody: "Instalează cea mai recentă versiune stabilă din browser.",
      homeToolsBody: "Deschide editorul de statistici și uneltele Xteink recomandate.",
      homeGithubBody: "Vezi release-uri, issues și codul proiectului.",
      homeSupportBody: "Susține dezvoltarea CPR-vCodex pe Ko-fi."
    },
    ru: {
      homeSubtitle: "Прошивка, авто-flash и локальные браузерные инструменты для CPR-vCodex на Xteink X3/X4.",
      homeFlashBody: "Установите последнюю стабильную версию из браузера.",
      homeToolsBody: "Откройте редактор статистики и рекомендуемые инструменты Xteink.",
      homeGithubBody: "Просмотрите релизы, issues и исходный код проекта.",
      homeSupportBody: "Поддержите разработку CPR-vCodex на Ko-fi."
    },
    si: {
      homeSubtitle: "Firmware, samodejni flash in lokalna brskalniška orodja za CPR-vCodex na Xteink X3/X4.",
      homeFlashBody: "Namestite najnovejšo stabilno izdajo iz brskalnika.",
      homeToolsBody: "Odprite urejevalnik statistik in priporočena orodja Xteink.",
      homeGithubBody: "Preglejte izdaje, issues in izvorno kodo projekta.",
      homeSupportBody: "Podprite razvoj CPR-vCodex na Ko-fi."
    },
    sv: {
      homeSubtitle: "Firmware, auto flash och lokala webbläsarverktyg för CPR-vCodex på Xteink X3/X4.",
      homeFlashBody: "Installera den senaste stabila releasen från webbläsaren.",
      homeToolsBody: "Öppna lässtatistikredigeraren och rekommenderade Xteink-verktyg.",
      homeGithubBody: "Granska releases, issues och projektets källkod.",
      homeSupportBody: "Stöd utvecklingen av CPR-vCodex på Ko-fi."
    },
    tr: {
      homeSubtitle: "Xteink X3/X4 üzerinde CPR-vCodex için firmware, otomatik flash ve yerel tarayıcı araçları.",
      homeFlashBody: "En son kararlı sürümü tarayıcıdan kurun.",
      homeToolsBody: "Okuma istatistikleri düzenleyicisini ve önerilen Xteink araçlarını açın.",
      homeGithubBody: "Sürümleri, issue'ları ve proje kaynak kodunu inceleyin.",
      homeSupportBody: "CPR-vCodex geliştirmesini Ko-fi üzerinde destekleyin."
    },
    uk: {
      homeSubtitle: "Прошивка, авто-flash і локальні браузерні інструменти для CPR-vCodex на Xteink X3/X4.",
      homeFlashBody: "Установіть останній стабільний реліз із браузера.",
      homeToolsBody: "Відкрийте редактор статистики та рекомендовані інструменти Xteink.",
      homeGithubBody: "Перегляньте релізи, issues і вихідний код проєкту.",
      homeSupportBody: "Підтримайте розробку CPR-vCodex на Ko-fi."
    },
    vi: {
      homeSubtitle: "Firmware, auto flash và công cụ trình duyệt cục bộ cho CPR-vCodex trên Xteink X3/X4.",
      homeFlashBody: "Cài đặt bản ổn định mới nhất từ trình duyệt.",
      homeToolsBody: "Mở trình sửa thống kê đọc và các tiện ích Xteink được đề xuất.",
      homeGithubBody: "Xem release, issue và mã nguồn dự án.",
      homeSupportBody: "Ủng hộ phát triển CPR-vCodex trên Ko-fi."
    }
  };
  for (const [language, values] of Object.entries(localizedHomeCards)) {
    if (!messages[language]) continue;
    Object.assign(messages[language], values);
  }

  const localizedTitles = Object.fromEntries(languages.map(([language]) => [language, "CPR-vCodex"]));
  const localizedAutoFlash = {
    be: "Аўта Flash", ca: "Flash automàtic", cs: "Automatický flash", da: "Auto Flash", nl: "Auto Flash", fi: "Automaattinen flash", fr: "Flash automatique", de: "Auto-Flash", hu: "Automatikus flash", it: "Flash automatico", kk: "Авто Flash", lt: "Automatinis flash", pl: "Auto Flash", pt: "Flash automático", ro: "Flash automat", ru: "Авто Flash", si: "Samodejni flash", sv: "Auto Flash", tr: "Otomatik Flash", uk: "Авто Flash", vi: "Flash tự động"
  };
  const localizedBackupText = {
    be: "Прапусціце гэты крок, калі ўжо прашывалі раней. Калі гэта першы раз, зрабіце \"Save Full Flash\" на https://xteink.dve.al/.",
    ca: "Ignora aquest pas si ja has flashejat altres vegades. Si és la primera vegada, recorda tenir un \"Save Full Flash\" a https://xteink.dve.al/.",
    cs: "Tento krok přeskočte, pokud jste už flashovali dříve. Pokud je to poprvé, mějte \"Save Full Flash\" z https://xteink.dve.al/.",
    da: "Spring dette trin over, hvis du har flashet før. Hvis det er første gang, så husk at have en \"Save Full Flash\" fra https://xteink.dve.al/.",
    nl: "Sla deze stap over als je al eerder hebt geflasht. Als dit de eerste keer is, zorg dan voor een \"Save Full Flash\" van https://xteink.dve.al/.",
    fi: "Ohita tämä vaihe, jos olet flashannut aiemmin. Jos tämä on ensimmäinen kerta, pidä \"Save Full Flash\" -varmuuskopio osoitteesta https://xteink.dve.al/.",
    fr: "Ignorez cette étape si vous avez déjà flashé. Si c'est la première fois, gardez un \"Save Full Flash\" depuis https://xteink.dve.al/.",
    de: "Überspringen Sie diesen Schritt, wenn Sie schon einmal geflasht haben. Wenn es das erste Mal ist, behalten Sie ein \"Save Full Flash\" von https://xteink.dve.al/.",
    hu: "Hagyd ki ezt a lépést, ha már flasheltél korábban. Ha ez az első alkalom, legyen egy \"Save Full Flash\" mentésed innen: https://xteink.dve.al/.",
    it: "Ignora questo passaggio se hai già fatto il flash altre volte. Se è la prima volta, tieni un \"Save Full Flash\" da https://xteink.dve.al/.",
    kk: "Бұрын flash жасаған болсаңыз, бұл қадамды өткізіп жіберіңіз. Бірінші рет болса, https://xteink.dve.al/ сайтынан \"Save Full Flash\" сақтық көшірмесін сақтаңыз.",
    lt: "Praleiskite šį žingsnį, jei jau esate flashinę anksčiau. Jei tai pirmas kartas, turėkite \"Save Full Flash\" iš https://xteink.dve.al/.",
    pl: "Pomiń ten krok, jeśli już wcześniej flashowałeś. Jeśli to pierwszy raz, zachowaj \"Save Full Flash\" z https://xteink.dve.al/.",
    pt: "Ignore este passo se já fez flash outras vezes. Se for a primeira vez, mantenha um \"Save Full Flash\" em https://xteink.dve.al/.",
    ro: "Sari peste acest pas dacă ai mai făcut flash înainte. Dacă este prima dată, păstrează un \"Save Full Flash\" de la https://xteink.dve.al/.",
    ru: "Пропустите этот шаг, если уже прошивали раньше. Если это первый раз, сохраните \"Save Full Flash\" с https://xteink.dve.al/.",
    si: "Preskočite ta korak, če ste že flashali. Če je to prvič, imejte \"Save Full Flash\" iz https://xteink.dve.al/.",
    es: "Ignora este paso si ya has flasheado otras veces. Si es la primera vez, recuerda tener un \"Save Full Flash\" en https://xteink.dve.al/.",
    sv: "Hoppa över detta steg om du har flashat tidigare. Om det är första gången, ha en \"Save Full Flash\" från https://xteink.dve.al/.",
    tr: "Daha önce flashladıysanız bu adımı atlayın. İlk kez yapıyorsanız https://xteink.dve.al/ üzerinden bir \"Save Full Flash\" yedeğiniz olsun.",
    uk: "Пропустіть цей крок, якщо вже прошивали раніше. Якщо це перший раз, збережіть \"Save Full Flash\" з https://xteink.dve.al/.",
    vi: "Bỏ qua bước này nếu bạn đã từng flash trước đó. Nếu đây là lần đầu, hãy giữ một bản \"Save Full Flash\" từ https://xteink.dve.al/."
  };
  for (const [language, title] of Object.entries(localizedTitles)) {
    if (!messages[language]) continue;
    messages[language].homeTitle = title;
    messages[language].navFlash = localizedAutoFlash[language] || messages[language].navFlash || messages.en.navFlash;
    messages[language].homeFlash = localizedAutoFlash[language] || messages[language].homeFlash || messages.en.homeFlash;
    messages[language].backupText = localizedBackupText[language] || messages.en.backupText;
  }

  function hasLanguage(language) {
    return languages.some(([code]) => code === language);
  }

  function getLanguage() {
    const saved = localStorage.getItem(LANGUAGE_KEY) || localStorage.getItem(LEGACY_LANGUAGE_KEY);
    if (saved && hasLanguage(saved)) return saved;
    const browser = (navigator.language || "en").toLowerCase();
    const shortCode = browser.split("-")[0];
    return hasLanguage(browser) ? browser : hasLanguage(shortCode) ? shortCode : "en";
  }

  function getTheme() {
    const saved = localStorage.getItem(THEME_KEY) || localStorage.getItem(LEGACY_THEME_KEY);
    if (saved === "light" || saved === "dark") return saved;
    return "dark";
  }

  function t(key, languageOrVars = getLanguage(), vars = {}) {
    let language = languageOrVars;
    let replacements = vars;
    if (typeof languageOrVars === "object" && languageOrVars !== null) {
      language = getLanguage();
      replacements = languageOrVars;
    }
    let text = (messages[language] && messages[language][key]) || messages.en[key] || key;
    for (const [name, value] of Object.entries(replacements)) {
      text = text.replaceAll(`{${name}}`, value);
    }
    return text;
  }

  function populateLanguageSelects(language = getLanguage()) {
    document.querySelectorAll("[data-role='language-select']").forEach(select => {
      select.innerHTML = languages
        .map(([code, label]) => `<option value="${code}">${label}</option>`)
        .join("");
      select.value = hasLanguage(language) ? language : "en";
    });
  }

  function applyTheme(theme = getTheme()) {
    const normalized = theme === "dark" ? "dark" : "light";
    document.documentElement.dataset.theme = normalized;
    if (document.body) document.body.dataset.theme = normalized;
    document.querySelectorAll("[data-role='theme-toggle']").forEach(button => {
      const label = t(normalized === "dark" ? "lightMode" : "darkMode");
      button.setAttribute("aria-label", label);
      button.setAttribute("title", label);
      const use = button.querySelector("use");
      if (use) {
        use.setAttribute("href", normalized === "dark" ? "#icon-sun" : "#icon-moon");
        return;
      }
      const iconHolder = button.querySelector("[data-role='theme-icon']") || button;
      iconHolder.innerHTML = normalized === "dark" ? sunIcon() : moonIcon();
    });
  }

  function applyI18n(language = getLanguage()) {
    const normalized = hasLanguage(language) ? language : "en";
    document.documentElement.lang = locales[normalized] || normalized;
    populateLanguageSelects(normalized);
    document.querySelectorAll("[data-site-i18n]").forEach(node => {
      node.textContent = t(node.dataset.siteI18n, normalized);
    });
    document.querySelectorAll("[data-site-i18n-html]").forEach(node => {
      node.innerHTML = t(node.dataset.siteI18nHtml, normalized);
    });
    document.querySelectorAll("[data-site-i18n-placeholder]").forEach(node => {
      node.placeholder = t(node.dataset.siteI18nPlaceholder, normalized);
    });
    applyTheme(getTheme());
  }

  function setLanguage(language, {emit = true} = {}) {
    const normalized = hasLanguage(language) ? language : "en";
    localStorage.setItem(LANGUAGE_KEY, normalized);
    localStorage.setItem(LEGACY_LANGUAGE_KEY, normalized);
    applyI18n(normalized);
    if (emit) {
      window.dispatchEvent(new CustomEvent("cpr-tools-language-change", {detail: {language: normalized}}));
    }
  }

  function setTheme(theme, {emit = true} = {}) {
    const normalized = theme === "dark" ? "dark" : "light";
    localStorage.setItem(THEME_KEY, normalized);
    localStorage.setItem(LEGACY_THEME_KEY, normalized);
    applyTheme(normalized);
    if (emit) {
      window.dispatchEvent(new CustomEvent("cpr-tools-theme-change", {detail: {theme: normalized}}));
    }
  }

  function wireControls() {
    document.querySelectorAll("[data-role='theme-toggle']").forEach(button => {
      if (button.dataset.cprToolsWired) return;
      button.dataset.cprToolsWired = "true";
      button.addEventListener("click", () => {
        setTheme(getTheme() === "dark" ? "light" : "dark");
      });
    });
    document.querySelectorAll("[data-role='language-select']").forEach(select => {
      if (select.dataset.cprToolsWired) return;
      select.dataset.cprToolsWired = "true";
      select.addEventListener("change", () => setLanguage(select.value));
    });
  }

  function init({wireControls: shouldWireControls = true} = {}) {
    applyTheme(getTheme());
    applyI18n(getLanguage());
    if (shouldWireControls) wireControls();
  }

  window.addEventListener("storage", event => {
    if ([LANGUAGE_KEY, LEGACY_LANGUAGE_KEY].includes(event.key)) {
      applyI18n(getLanguage());
    }
    if ([THEME_KEY, LEGACY_THEME_KEY].includes(event.key)) {
      applyTheme(getTheme());
    }
  });

  document.addEventListener("DOMContentLoaded", () => {
    if (document.documentElement.dataset.cprToolsAutoinit !== "false") {
      init();
    }
  });

  window.CPRTools = {
    init,
    getLanguage,
    getTheme,
    setLanguage,
    setTheme,
    applyI18n,
    applyTheme,
    languages,
    locales,
    t
  };

  function moonIcon() {
    return '<svg class="site-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3a6.8 6.8 0 0 0 8.7 8.7A9 9 0 1 1 12 3"></path></svg>';
  }

  function sunIcon() {
    return '<svg class="site-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"></circle><path d="M12 2v2"></path><path d="M12 20v2"></path><path d="m4.93 4.93 1.41 1.41"></path><path d="m17.66 17.66 1.41 1.41"></path><path d="M2 12h2"></path><path d="M20 12h2"></path><path d="m6.34 17.66-1.41 1.41"></path><path d="m19.07 4.93-1.41 1.41"></path></svg>';
  }
})();
