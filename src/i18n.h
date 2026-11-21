#pragma once

#include <cstddef>

namespace i18n {

enum class Lang : int {
    Fr = 0,
    En = 1,
    Ja = 2,
    Es = 3,
    De = 4,
    Count = 5,
};

enum class Key : int {
    // Tabs
    TabHome = 0,
    TabApps,
    TabFiles,
    TabFtp,
    TabSettings,

    // Common & Buttons
    Launch,
    LaunchA,
    Favorite,
    Pin,
    FavoriteY,
    PinY,
    Details,
    Favorites,
    SeeAll,
    Search,
    SortLabel,
    SortRecent,
    SortAZ,
    SortSize,
    BackB,
    CloseB,
    CancelB,
    DeleteA,
    AppsCount,
    ItemsCount,
    FreeSpace,
    On,
    Off,

    // Home / Apps empty & status
    NoAppFound,
    NoAppHint,
    FtpStandby,
    FtpReady,
    FtpStarted,
    FtpStopped,
    FtpStandbySub,
    LibUpdated,
    SdRefreshed,
    AddedFav,
    RemovedFav,
    LaunchFailed,
    LaunchFailedSub,
    Launching,

    // Detail page
    Author,
    Version,
    Size,
    Format,
    OpenFolderX,
    DetailsHint,

    // Search keyboard headers
    SearchAppsPrompt,
    FilterAppsPrompt,

    // File manager
    FilesTitle,
    ParentB,
    RootSwitch,
    NewFolderZR,
    RenameX,
    DeleteY,
    EmptyFolder,
    EmptyFile,
    FilesFooter,
    NewFolderPrompt,
    NewFolderDefault,
    FolderCreated,
    RenamePrompt,
    Renamed,
    ConfirmDelete,
    Deleted,
    DeleteFailed,
    DirMustBeEmpty,

    // FTP view
    FtpTitle,
    FtpSubtitle,
    FtpStopBtn,
    FtpStartBtn,
    FtpConnAddr,
    FtpServerIdle,
    FtpStorage,
    FtpLiveLog,
    FtpRefreshBtn,
    FtpPortBtn,

    // Settings view
    SettingsTitle,
    SettingsSubtitle,
    SetLanguage,
    SetTheme,
    SetStartupTab,
    SetClockFormat,
    SetBatteryPct,
    SetGlow,
    SetTitleStyle,
    SetLowercase,
    SetHideSelf,
    SetFtpAuto,
    SetFtpPort,
    SetSounds,
    SetExit,

    // Setting values
    GlowSoft,
    GlowNormal,
    GlowIntense,
    TitleSerif,
    TitleSans,
    SoundsOn,
    SoundsMuted,
    ExitDesc,
    Clock24h,
    Clock12h,

    // Theme names
    ThemeFramboise,
    ThemeNuit,
    ThemeIris,
    ThemeLagon,
    ThemePeche,
    ThemeOled,

    Count,
};

inline const char *langDisplayName(int langIdx) {
    switch (static_cast<Lang>(langIdx % static_cast<int>(Lang::Count))) {
        case Lang::Fr: return "Français";
        case Lang::En: return "English";
        case Lang::Ja: return "日本語";
        case Lang::Es: return "Español";
        case Lang::De: return "Deutsch";
        default:       return "English";
    }
}

inline const char *tr(int langIdx, Key k) {
    static constexpr const char *kTable[static_cast<int>(Lang::Count)][static_cast<int>(Key::Count)] = {
        // ============================================================
        // FR — Français
        // ============================================================
        {
            "Accueil", "Apps", "Fichiers", "FTP", "Réglages",
            "Lancer", "Lancer (A)", "Favori", "Épingler", "Favori (Y)", "Épingler (Y)",
            "Infos", "Favoris", "Voir tout", "Rechercher",
            "Tri", "Récents", "A → Z", "Taille",
            "Retour (B)", "Fermer (B)", "Annuler (B)", "Supprimer (A)",
            "apps", "éléments", "libres", "Activé", "Désactivé",

            "Aucune app trouvée",
            "Appuie sur B pour réinitialiser le filtre ou envoie un .nro en FTP.",
            "FTP veille", "FTP prêt", "FTP démarré", "FTP arrêté", "Serveur mis en veille",
            "Bibliothèque mise à jour", "Carte SD actualisée",
            "Ajouté aux favoris", "Retiré des favoris",
            "Lancement impossible", "envSetNextLoad indisponible", "Lancement en cours…",

            "Auteur", "Version", "Taille", "Format", "Ouvrir dossier (X)",
            "↑ Infos  •  A Lancer  •  Y Favori  •  X Tri  •  ZL Favoris  •  ZR Apps  •  - FTP",

            "Rechercher une app", "Filtrer les apps",

            "Fichiers", "Parent (B)", "/switch (ZL)", "+ Dossier (ZR)", "Renommer (X)", "Supprimer (Y)",
            "Dossier vide", "(fichier vide)",
            "A Ouvrir  •  B Parent  •  ZL Racine/switch  •  ZR Nouveau dossier  •  X Renommer  •  Y Supprimer",
            "Nom du nouveau dossier", "Nouveau dossier", "Dossier créé",
            "Renommer", "Renommé", "Confirmer la suppression", "Supprimé",
            "Suppression impossible", "Le dossier doit être vide",

            "Serveur FTP intégré",
            "Transfère tes .nro et fichiers sur la microSD sans quitter Tsuki.",
            "Actif — Arrêter (A)", "Démarrer FTP (A)",
            "Adresse de connexion", "Serveur en veille", "Stockage microSD", "Journal en direct",
            "Actualiser la SD (+)", "Port FTP (X)",

            "Réglages",
            "Langue, thèmes Yoru, affichage, contrôleur et serveur FTP.",
            "Langue", "Thème (palette Yoru)", "Écran de démarrage", "Format de l'heure",
            "Pourcentage batterie", "Intensité du glow", "Style des grands titres",
            "Libellés en minuscules", "Masquer Tsuki / hbmenu", "Démarrer le FTP au lancement",
            "Port du serveur FTP", "Sons d'interface", "Quitter Tsuki",

            "Doux", "Normal", "Intense", "Serif éditorial", "Sans moderne",
            "Activés", "Muet", "Retour au chargeur", "24 heures", "12 heures (AM/PM)",

            "Framboise", "Nuit", "Iris", "Lagon", "Pêche", "OLED",
        },
        // ============================================================
        // EN — English
        // ============================================================
        {
            "Home", "Apps", "Files", "FTP", "Settings",
            "Launch", "Launch (A)", "Favorite", "Pin", "Favorite (Y)", "Pin (Y)",
            "Details", "Favorites", "See all", "Search",
            "Sort", "Recent", "A → Z", "Size",
            "Back (B)", "Close (B)", "Cancel (B)", "Delete (A)",
            "apps", "items", "free", "On", "Off",

            "No apps found",
            "Press B to reset filters or transfer an .nro over FTP.",
            "FTP idle", "FTP ready", "FTP started", "FTP stopped", "Server paused",
            "Library updated", "SD card refreshed",
            "Added to favorites", "Removed from favorites",
            "Launch failed", "envSetNextLoad unavailable", "Launching…",

            "Author", "Version", "Size", "Format", "Open folder (X)",
            "↑ Details  •  A Launch  •  Y Favorite  •  X Sort  •  ZL Favorites  •  ZR Apps  •  - FTP",

            "Search apps", "Filter apps",

            "Files", "Parent (B)", "/switch (ZL)", "+ Folder (ZR)", "Rename (X)", "Delete (Y)",
            "Empty folder", "(empty file)",
            "A Open  •  B Parent  •  ZL Root/switch  •  ZR New folder  •  X Rename  •  Y Delete",
            "New folder name", "New folder", "Folder created",
            "Rename", "Renamed", "Confirm deletion", "Deleted",
            "Delete failed", "Folder must be empty",

            "Built-in FTP Server",
            "Transfer .nro homebrews and files to microSD without leaving Tsuki.",
            "Active — Stop (A)", "Start FTP (A)",
            "Connection address", "Server paused", "microSD Storage", "Live log",
            "Refresh SD (+)", "FTP Port (X)",

            "Settings",
            "Language, Yoru themes, layout, display and FTP server.",
            "Language", "Theme (Yoru palette)", "Startup view", "Clock format",
            "Battery percentage", "Glow intensity", "Title typography",
            "Lowercase labels", "Hide Tsuki / hbmenu", "Auto-start FTP",
            "FTP server port", "UI sounds", "Exit Tsuki",

            "Soft", "Normal", "Intense", "Editorial Serif", "Modern Sans",
            "Enabled", "Muted", "Return to loader", "24-hour", "12-hour (AM/PM)",

            "Framboise", "Nuit", "Iris", "Lagon", "Pêche", "OLED",
        },
        // ============================================================
        // JA — 日本語
        // ============================================================
        {
            "ホーム", "アプリ", "ファイル", "FTP", "設定",
            "起動", "起動 (A)", "お気に入り", "ピン留め", "お気に入り (Y)", "ピン留め (Y)",
            "詳細", "お気に入り", "すべて表示", "検索",
            "並び替え", "最近", "名前順", "サイズ順",
            "戻る (B)", "閉じる (B)", "キャンセル (B)", "削除 (A)",
            "件", "項目", "空き", "オン", "オフ",

            "アプリが見つかりません",
            "Bでフィルターを解除するか、FTPで .nro を転送してください。",
            "FTP待機中", "FTP準備完了", "FTPを開始しました", "FTPを停止しました", "サーバー停止中",
            "ライブラリを更新しました", "SDカードを再読み込みしました",
            "お気に入りに追加", "お気に入りから削除",
            "起動できませんでした", "envSetNextLoad が利用できません", "起動しています…",

            "作者", "バージョン", "サイズ", "形式", "フォルダを開く (X)",
            "↑ 詳細  •  A 起動  •  Y お気に入り  •  X 並替  •  ZL お気に入り  •  ZR 一覧  •  - FTP",

            "アプリを検索", "アプリを絞り込む",

            "ファイル", "上へ (B)", "/switch (ZL)", "+ 作成 (ZR)", "名前変更 (X)", "削除 (Y)",
            "空のフォルダ", "(空のファイル)",
            "A 開く  •  B 上へ  •  ZL ルート/switch  •  ZR 新規フォルダ  •  X 名前変更  •  Y 削除",
            "新しいフォルダ名", "新しいフォルダ", "フォルダを作成しました",
            "名前を変更", "名前を変更しました", "削除の確認", "削除しました",
            "削除できませんでした", "フォルダを空にしてください",

            "内蔵FTPサーバー",
            "Tsukiを終了せずに .nro やファイルを microSD へ転送できます。",
            "稼働中 — 停止 (A)", "FTPを開始 (A)",
            "接続アドレス", "サーバー停止中", "microSD ストレージ", "リアルタイムログ",
            "SDを再スキャン (+)", "ポート変更 (X)",

            "設定",
            "言語、Yoruテーマ、表示オプション、FTPサーバーの設定。",
            "言語", "テーマ (Yoruパレット)", "起動時の画面", "時計の形式",
            "バッテリー残量 (%)", "グロー強度", "見出しフォント",
            "小文字スタイル (英字)", "Tsuki / hbmenu を非表示", "起動時にFTPを自動開始",
            "FTPポート番号", "UI効果音", "Tsukiを終了",

            "弱め", "標準", "強め", "セリフ体", "モダンサンセリフ",
            "オン", "ミュート", "ローダーへ戻る", "24時間表示", "12時間表示 (AM/PM)",

            "フランボワーズ", "夜 (Nuit)", "アイリス", "ラグーン", "ピーチ", "OLED",
        },
        // ============================================================
        // ES — Español
        // ============================================================
        {
            "Inicio", "Apps", "Archivos", "FTP", "Ajustes",
            "Iniciar", "Iniciar (A)", "Favorito", "Fijar", "Favorito (Y)", "Fijar (Y)",
            "Detalles", "Favoritos", "Ver todo", "Buscar",
            "Orden", "Recientes", "A → Z", "Tamaño",
            "Volver (B)", "Cerrar (B)", "Cancelar (B)", "Eliminar (A)",
            "apps", "elementos", "libres", "Activado", "Desactivado",

            "No se encontraron apps",
            "Pulsa B para quitar filtros o envía un .nro por FTP.",
            "FTP en espera", "FTP listo", "FTP iniciado", "FTP detenido", "Servidor en pausa",
            "Biblioteca actualizada", "Tarjeta SD actualizada",
            "Añadido a favoritos", "Quitado de favoritos",
            "Error al iniciar", "envSetNextLoad no disponible", "Iniciando…",

            "Autor", "Versión", "Tamaño", "Formato", "Abrir carpeta (X)",
            "↑ Detalles  •  A Iniciar  •  Y Favorito  •  X Orden  •  ZL Favoritos  •  ZR Apps  •  - FTP",

            "Buscar app", "Filtrar apps",

            "Archivos", "Atrás (B)", "/switch (ZL)", "+ Carpeta (ZR)", "Renombrar (X)", "Eliminar (Y)",
            "Carpeta vacía", "(archivo vacío)",
            "A Abrir  •  B Atrás  •  ZL Raíz/switch  •  ZR Nueva carpeta  •  X Renombrar  •  Y Eliminar",
            "Nombre de nueva carpeta", "Nueva carpeta", "Carpeta creada",
            "Renombrar", "Renombrado", "Confirmar eliminación", "Eliminado",
            "No se pudo eliminar", "La carpeta debe estar vacía",

            "Servidor FTP integrado",
            "Transfiere archivos y .nro a tu microSD sin salir de Tsuki.",
            "Activo — Detener (A)", "Iniciar FTP (A)",
            "Dirección de conexión", "Servidor en pausa", "Almacenamiento microSD", "Registro en vivo",
            "Actualizar SD (+)", "Puerto FTP (X)",

            "Ajustes",
            "Idioma, temas Yoru, apariencia y servidor FTP.",
            "Idioma", "Tema (paleta Yoru)", "Vista inicial", "Formato de reloj",
            "Porcentaje de batería", "Intensidad de brillo", "Estilo de títulos",
            "Etiquetas en minúsculas", "Ocultar Tsuki / hbmenu", "Iniciar FTP al abrir",
            "Puerto del servidor FTP", "Sonidos de interfaz", "Salir de Tsuki",

            "Suave", "Normal", "Intenso", "Serif editorial", "Sans moderno",
            "Activados", "Silencio", "Volver al cargador", "24 horas", "12 horas (AM/PM)",

            "Framboise", "Nuit", "Iris", "Lagon", "Pêche", "OLED",
        },
        // ============================================================
        // DE — Deutsch
        // ============================================================
        {
            "Start", "Apps", "Dateien", "FTP", "Optionen",
            "Starten", "Starten (A)", "Favorit", "Anpinnen", "Favorit (Y)", "Anpinnen (Y)",
            "Details", "Favoriten", "Alle anzeigen", "Suchen",
            "Sortierung", "Zuletzt", "A → Z", "Größe",
            "Zurück (B)", "Schließen (B)", "Abbrechen (B)", "Löschen (A)",
            "Apps", "Einträge", "frei", "Ein", "Aus",

            "Keine Apps gefunden",
            "Drücke B zum Zurücksetzen oder übertrage eine .nro per FTP.",
            "FTP inaktiv", "FTP bereit", "FTP gestartet", "FTP gestoppt", "Server pausiert",
            "Bibliothek aktualisiert", "SD-Karte aktualisiert",
            "Zu Favoriten hinzugefügt", "Aus Favoriten entfernt",
            "Start fehlgeschlagen", "envSetNextLoad nicht verfügbar", "Starte…",

            "Autor", "Version", "Größe", "Format", "Ordner öffnen (X)",
            "↑ Details  •  A Starten  •  Y Favorit  •  X Sortieren  •  ZL Favoriten  •  ZR Apps  •  - FTP",

            "App suchen", "Apps filtern",

            "Dateien", "Zurück (B)", "/switch (ZL)", "+ Ordner (ZR)", "Umbenennen (X)", "Löschen (Y)",
            "Leerer Ordner", "(leere Datei)",
            "A Öffnen  •  B Zurück  •  ZL Root/switch  •  ZR Neuer Ordner  •  X Umbenennen  •  Y Löschen",
            "Name des neuen Ordners", "Neuer Ordner", "Ordner erstellt",
            "Umbenennen", "Umbenannt", "Löschen bestätigen", "Gelöscht",
            "Löschen fehlgeschlagen", "Ordner muss leer sein",

            "Integrierter FTP-Server",
            "Übertrage .nro-Homebrews und Dateien direkt ohne Tsuki zu verlassen.",
            "Aktiv — Stoppen (A)", "FTP starten (A)",
            "Verbindungsadresse", "Server pausiert", "microSD-Speicher", "Live-Protokoll",
            "SD aktualisieren (+)", "FTP-Port (X)",

            "Optionen",
            "Sprache, Yoru-Themes, Darstellung und FTP-Server.",
            "Sprache", "Theme (Yoru-Palette)", "Startansicht", "Uhrzeitformat",
            "Akku-Prozentanzeige", "Glow-Intensität", "Titel-Schriftart",
            "Kleinschreibung", "Tsuki / hbmenu ausblenden", "FTP beim Start öffnen",
            "FTP-Server-Port", "UI-Sounds", "Tsuki beenden",

            "Sanft", "Normal", "Intensiv", "Editorial Serif", "Modern Sans",
            "Ein", "Stumm", "Zurück zum Loader", "24 Stunden", "12 Stunden (AM/PM)",

            "Framboise", "Nuit", "Iris", "Lagon", "Pêche", "OLED",
        },
    };

    int li = langIdx % static_cast<int>(Lang::Count);
    if (li < 0) li = 0;
    int ki = static_cast<int>(k);
    if (ki < 0 || ki >= static_cast<int>(Key::Count)) return "";
    return kTable[li][ki];
}

}  // namespace i18n
