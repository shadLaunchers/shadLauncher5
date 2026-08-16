// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Friendly names and value decoders for PS5 param.json fields.
//
// Keys here are the "lookup keys" produced by Param::GetEntries(): full dotted
// paths with array indices collapsed, e.g. "pubtools.creationDate" or
// "gameIntent.permittedIntents[].intentType". Fields whose names are data --
// per-region ageLevel entries, per-locale titles -- are handled by
// paramDisplayName() rather than by a map entry.
//
// Reference: https://www.psdevwiki.com/ps5/Param.json

#include <functional>
#include <QMap>
#include <QString>
#include <QStringList>

struct ParamKeyInfo {
    QString displayName;                           // Friendly name for the UI
    std::function<QString(const QString&)> decode; // Raw text -> readable text
};

inline QString paramIdentityDecoder(const QString& v) {
    return v;
}

namespace ParamKeyDecoders {

// "applicationCategoryType"
inline QString decodeApplicationCategoryType(const QString& v) {
    bool ok = false;
    const qint64 val = v.toLongLong(&ok);
    if (!ok) {
        return v;
    }
    static const QMap<qint64, QString> map = {
        {0, QStringLiteral("Native Game")},
        {65536, QStringLiteral("Prospero Native Media App")},
        {65792, QStringLiteral("RNPS Media App")},
        {66048, QStringLiteral("Web Based Media App")},
        {131328, QStringLiteral("System Built-in App")},
        {131584, QStringLiteral("Big Daemon")},
        {16777216, QStringLiteral("ShellUI")},
        {33554432, QStringLiteral("Daemon")},
        {50331648, QStringLiteral("CommonDialog")},
        {67108864, QStringLiteral("ShellApp")},
    };
    const auto it = map.find(val);
    return it != map.end() ? QStringLiteral("%1 (%2)").arg(*it).arg(val)
                           : QStringLiteral("Unknown (%1)").arg(val);
}

// "applicationDrmType"
inline QString decodeApplicationDrmType(const QString& v) {
    static const QMap<QString, QString> map = {
        {QStringLiteral("upgradable"), QStringLiteral("Upgradable (trial that unlocks in place)")},
        {QStringLiteral("standard"), QStringLiteral("Standard (full paid application)")},
        {QStringLiteral("demo"), QStringLiteral("Demo")},
        {QStringLiteral("free"), QStringLiteral("Free")},
    };
    const auto it = map.find(v.trimmed());
    return it != map.end() ? QStringLiteral("%1 - %2").arg(v.trimmed(), *it) : v;
}

// Shared bitfield renderer for the three attribute words. Known bits are named;
// anything else is reported by position rather than silently dropped, so an
// unrecognised value is still legible.
inline QString decodeBitfield(const QString& v, const QMap<int, QString>& bits) {
    bool ok = false;
    quint64 val = v.toULongLong(&ok);
    if (!ok) {
        val = v.toULongLong(&ok, 16);
        if (!ok) {
            return v;
        }
    }
    if (val == 0) {
        return QStringLiteral("0 - none set");
    }

    QStringList flags;
    for (int bit = 0; bit < 64; ++bit) {
        if ((val & (quint64(1) << bit)) == 0) {
            continue;
        }
        const auto it = bits.find(bit);
        flags << (it != bits.end() ? *it : QStringLiteral("Unknown (bit %1)").arg(bit));
    }
    return QStringLiteral("%1 - %2").arg(val).arg(flags.join(QStringLiteral(", ")));
}

// "attribute". The wiki documents whole values rather than individual bits;
// these names are the bits those documented values differ by.
inline QString decodeAttribute(const QString& v) {
    static const QMap<int, QString> bits = {
        {0, QStringLiteral("Supports the initial user's logout")},
        {25, QStringLiteral("Supports Text to Speech (TTS)")},
        {29, QStringLiteral("Supports HDR")},
        {30, QStringLiteral("Requests HDCP 2.2 at startup if possible")},
    };
    return decodeBitfield(v, bits);
}

// "attribute2"
inline QString decodeAttribute2(const QString& v) {
    static const QMap<int, QString> bits = {
        {2, QStringLiteral("Supports the Content Search feature")},
    };
    return decodeBitfield(v, bits);
}

// "attribute3". The documented values overlap in ways the wiki's own table does
// not fully resolve, so only the bits that are unambiguous there are named.
inline QString decodeAttribute3(const QString& v) {
    static const QMap<int, QString> bits = {
        {2, QStringLiteral("Receives video-out info")},
        {4, QStringLiteral("Uses the Share Library Capture API")},
        {6, QStringLiteral("Supports HFR")},
        {12, QStringLiteral("Supports High Framerate Mode")},
        {18, QStringLiteral("Auto Scaling applied for the non-media application")},
    };
    return decodeBitfield(v, bits);
}

// "contentBadgeType"
inline QString decodeContentBadgeType(const QString& v) {
    bool ok = false;
    const int val = v.toInt(&ok);
    if (!ok) {
        return v;
    }
    switch (val) {
    case 0:
        return QStringLiteral("0 - N/A");
    case 1:
        return QStringLiteral("1 - Game");
    case 2:
        return QStringLiteral("2 - Other");
    default:
        return QStringLiteral("Unknown (%1)").arg(val);
    }
}

// "requiredSystemSoftwareVersion" / "sdkVersion": "0x0320000000000000" -> "3.20".
inline QString decodeHexVersion(const QString& v) {
    QString hex = v.trimmed();
    if (hex.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hex = hex.mid(2);
    }
    bool ok = false;
    const quint64 raw = hex.toULongLong(&ok, 16);
    if (!ok) {
        return v;
    }
    const quint32 top = static_cast<quint32>(raw >> 32);
    const quint8 major_bcd = (top >> 24) & 0xFF;
    const quint8 minor_bcd = (top >> 16) & 0xFF;
    const int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
    const int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);
    return QStringLiteral("%1.%2 (%3)")
        .arg(major)
        .arg(minor, 2, 10, QLatin1Char('0'))
        .arg(v.trimmed());
}

// "gameIntent.permittedIntents[].intentType"
inline QString decodeIntentType(const QString& v) {
    static const QMap<QString, QString> map = {
        {QStringLiteral("launchActivity"), QStringLiteral("Launch an activity")},
        {QStringLiteral("launchMultiplayerActivity"),
         QStringLiteral("Launch a multiplayer activity")},
        {QStringLiteral("launchByCustomParameters"),
         QStringLiteral("Launch with custom parameters")},
        {QStringLiteral("joinSession"), QStringLiteral("Join a session")},
    };
    const auto it = map.find(v.trimmed());
    return it != map.end() ? QStringLiteral("%1 - %2").arg(v.trimmed(), *it) : v;
}

// "downloadDataSize" and other byte counts.
inline QString decodeByteSize(const QString& v) {
    bool ok = false;
    const qulonglong bytes = v.toULongLong(&ok);
    if (!ok) {
        return v;
    }
    if (bytes == 0) {
        return QStringLiteral("0");
    }
    static const QStringList units = {QStringLiteral("bytes"), QStringLiteral("KiB"),
                                      QStringLiteral("MiB"), QStringLiteral("GiB"),
                                      QStringLiteral("TiB")};
    double scaled = static_cast<double>(bytes);
    int unit = 0;
    while (scaled >= 1024.0 && unit + 1 < units.size()) {
        scaled /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 bytes").arg(bytes);
    }
    return QStringLiteral("%1 (%2 %3)").arg(bytes).arg(scaled, 0, 'f', 2).arg(units.at(unit));
}

// "amm.*" / sizes already expressed in GiB.
inline QString decodeGibSize(const QString& v) {
    bool ok = false;
    const qulonglong gib = v.toULongLong(&ok);
    return ok ? QStringLiteral("%1 GiB").arg(gib) : v;
}

// "kernel.*" sizes, given in bytes.
inline QString decodeMemorySize(const QString& v) {
    return decodeByteSize(v);
}

// "disc[].contents[].contentType"
inline QString decodeDiscContentType(const QString& v) {
    static const QMap<QString, QString> map = {
        {QStringLiteral("PS5GD"), QStringLiteral("PS5 game data")},
        {QStringLiteral("PS5AC"), QStringLiteral("PS5 additional content")},
        {QStringLiteral("PS5GP"), QStringLiteral("PS5 game patch")},
    };
    const auto it = map.find(v.trimmed());
    return it != map.end() ? QStringLiteral("%1 - %2").arg(v.trimmed(), *it) : v;
}

// "pubtools.creationDate": already "yyyy-mm-dd hh:mm:ss", pass through.
inline QString decodeCreationDate(const QString& v) {
    return v.trimmed();
}

} // namespace ParamKeyDecoders

// Locale code -> language name, for "localizedParameters" sub-keys.
inline const QMap<QString, QString>& paramLanguageNames() {
    static const QMap<QString, QString> map = {
        {QStringLiteral("ar-AE"), QStringLiteral("Arabic")},
        {QStringLiteral("cs-CZ"), QStringLiteral("Czech")},
        {QStringLiteral("da-DK"), QStringLiteral("Danish")},
        {QStringLiteral("de-DE"), QStringLiteral("German")},
        {QStringLiteral("el-GR"), QStringLiteral("Greek")},
        {QStringLiteral("en-GB"), QStringLiteral("English (UK)")},
        {QStringLiteral("en-US"), QStringLiteral("English (US)")},
        {QStringLiteral("es-419"), QStringLiteral("Spanish (Latin America)")},
        {QStringLiteral("es-ES"), QStringLiteral("Spanish")},
        {QStringLiteral("fi-FI"), QStringLiteral("Finnish")},
        {QStringLiteral("fr-CA"), QStringLiteral("French (Canada)")},
        {QStringLiteral("fr-FR"), QStringLiteral("French")},
        {QStringLiteral("hu-HU"), QStringLiteral("Hungarian")},
        {QStringLiteral("id-ID"), QStringLiteral("Indonesian")},
        {QStringLiteral("it-IT"), QStringLiteral("Italian")},
        {QStringLiteral("ja-JP"), QStringLiteral("Japanese")},
        {QStringLiteral("ko-KR"), QStringLiteral("Korean")},
        {QStringLiteral("nl-NL"), QStringLiteral("Dutch")},
        {QStringLiteral("no-NO"), QStringLiteral("Norwegian")},
        {QStringLiteral("pl-PL"), QStringLiteral("Polish")},
        {QStringLiteral("pt-BR"), QStringLiteral("Portuguese (BR)")},
        {QStringLiteral("pt-PT"), QStringLiteral("Portuguese (PT)")},
        {QStringLiteral("ro-RO"), QStringLiteral("Romanian")},
        {QStringLiteral("ru-RU"), QStringLiteral("Russian")},
        {QStringLiteral("sv-SE"), QStringLiteral("Swedish")},
        {QStringLiteral("th-TH"), QStringLiteral("Thai")},
        {QStringLiteral("tr-TR"), QStringLiteral("Turkish")},
        {QStringLiteral("uk-UA"), QStringLiteral("Ukrainian")},
        {QStringLiteral("vi-VN"), QStringLiteral("Vietnamese")},
        {QStringLiteral("zh-Hans"), QStringLiteral("Chinese (Simplified)")},
        {QStringLiteral("zh-Hant"), QStringLiteral("Chinese (Traditional)")},
    };
    return map;
}

inline QString paramLanguageName(const QString& locale) {
    const auto it = paramLanguageNames().find(locale);
    return it != paramLanguageNames().end() ? *it : locale;
}

// Region code -> country name, for the "ageLevel" dictionary.
inline const QMap<QString, QString>& paramRegionNames() {
    static const QMap<QString, QString> map = {
        {QStringLiteral("AE"), QStringLiteral("United Arab Emirates")},
        {QStringLiteral("AR"), QStringLiteral("Argentina")},
        {QStringLiteral("AT"), QStringLiteral("Austria")},
        {QStringLiteral("AU"), QStringLiteral("Australia")},
        {QStringLiteral("BE"), QStringLiteral("Belgium")},
        {QStringLiteral("BG"), QStringLiteral("Bulgaria")},
        {QStringLiteral("BH"), QStringLiteral("Bahrain")},
        {QStringLiteral("BO"), QStringLiteral("Bolivia")},
        {QStringLiteral("BR"), QStringLiteral("Brazil")},
        {QStringLiteral("CA"), QStringLiteral("Canada")},
        {QStringLiteral("CH"), QStringLiteral("Switzerland")},
        {QStringLiteral("CL"), QStringLiteral("Chile")},
        {QStringLiteral("CN"), QStringLiteral("China")},
        {QStringLiteral("CO"), QStringLiteral("Colombia")},
        {QStringLiteral("CR"), QStringLiteral("Costa Rica")},
        {QStringLiteral("CY"), QStringLiteral("Cyprus")},
        {QStringLiteral("CZ"), QStringLiteral("Czechia")},
        {QStringLiteral("DE"), QStringLiteral("Germany")},
        {QStringLiteral("DK"), QStringLiteral("Denmark")},
        {QStringLiteral("EC"), QStringLiteral("Ecuador")},
        {QStringLiteral("ES"), QStringLiteral("Spain")},
        {QStringLiteral("FI"), QStringLiteral("Finland")},
        {QStringLiteral("FR"), QStringLiteral("France")},
        {QStringLiteral("GB"), QStringLiteral("United Kingdom")},
        {QStringLiteral("GR"), QStringLiteral("Greece")},
        {QStringLiteral("GT"), QStringLiteral("Guatemala")},
        {QStringLiteral("HK"), QStringLiteral("Hong Kong")},
        {QStringLiteral("HN"), QStringLiteral("Honduras")},
        {QStringLiteral("HR"), QStringLiteral("Croatia")},
        {QStringLiteral("HU"), QStringLiteral("Hungary")},
        {QStringLiteral("ID"), QStringLiteral("Indonesia")},
        {QStringLiteral("IE"), QStringLiteral("Ireland")},
        {QStringLiteral("IL"), QStringLiteral("Israel")},
        {QStringLiteral("IN"), QStringLiteral("India")},
        {QStringLiteral("IS"), QStringLiteral("Iceland")},
        {QStringLiteral("IT"), QStringLiteral("Italy")},
        {QStringLiteral("JP"), QStringLiteral("Japan")},
        {QStringLiteral("KR"), QStringLiteral("South Korea")},
        {QStringLiteral("KW"), QStringLiteral("Kuwait")},
        {QStringLiteral("LB"), QStringLiteral("Lebanon")},
        {QStringLiteral("LU"), QStringLiteral("Luxembourg")},
        {QStringLiteral("MT"), QStringLiteral("Malta")},
        {QStringLiteral("MX"), QStringLiteral("Mexico")},
        {QStringLiteral("MY"), QStringLiteral("Malaysia")},
        {QStringLiteral("NI"), QStringLiteral("Nicaragua")},
        {QStringLiteral("NL"), QStringLiteral("Netherlands")},
        {QStringLiteral("NO"), QStringLiteral("Norway")},
        {QStringLiteral("NZ"), QStringLiteral("New Zealand")},
        {QStringLiteral("OM"), QStringLiteral("Oman")},
        {QStringLiteral("PA"), QStringLiteral("Panama")},
        {QStringLiteral("PE"), QStringLiteral("Peru")},
        {QStringLiteral("PL"), QStringLiteral("Poland")},
        {QStringLiteral("PT"), QStringLiteral("Portugal")},
        {QStringLiteral("PY"), QStringLiteral("Paraguay")},
        {QStringLiteral("QA"), QStringLiteral("Qatar")},
        {QStringLiteral("RO"), QStringLiteral("Romania")},
        {QStringLiteral("RU"), QStringLiteral("Russia")},
        {QStringLiteral("SA"), QStringLiteral("Saudi Arabia")},
        {QStringLiteral("SE"), QStringLiteral("Sweden")},
        {QStringLiteral("SG"), QStringLiteral("Singapore")},
        {QStringLiteral("SI"), QStringLiteral("Slovenia")},
        {QStringLiteral("SK"), QStringLiteral("Slovakia")},
        {QStringLiteral("SV"), QStringLiteral("El Salvador")},
        {QStringLiteral("TH"), QStringLiteral("Thailand")},
        {QStringLiteral("TR"), QStringLiteral("Turkey")},
        {QStringLiteral("TW"), QStringLiteral("Taiwan")},
        {QStringLiteral("UA"), QStringLiteral("Ukraine")},
        {QStringLiteral("US"), QStringLiteral("United States")},
        {QStringLiteral("UY"), QStringLiteral("Uruguay")},
        {QStringLiteral("ZA"), QStringLiteral("South Africa")},
    };
    return map;
}

inline const QMap<QString, ParamKeyInfo>& paramKeyMap() {
    using namespace ParamKeyDecoders;
    static const QMap<QString, ParamKeyInfo> map = {
        // Identity
        {QStringLiteral("titleId"), {QStringLiteral("Title ID"), paramIdentityDecoder}},
        {QStringLiteral("contentId"), {QStringLiteral("Content ID"), paramIdentityDecoder}},
        {QStringLiteral("conceptId"), {QStringLiteral("Concept ID"), paramIdentityDecoder}},
        {QStringLiteral("contentBadgeType"),
         {QStringLiteral("Content badge type"), decodeContentBadgeType}},
        {QStringLiteral("applicationCategoryType"),
         {QStringLiteral("Application category type"), decodeApplicationCategoryType}},
        {QStringLiteral("applicationDrmType"),
         {QStringLiteral("Application DRM type"), decodeApplicationDrmType}},
        {QStringLiteral("backgroundBasematType"),
         {QStringLiteral("Background basemat type"), paramIdentityDecoder}},
        {QStringLiteral("deeplinkUri"), {QStringLiteral("Deeplink URI"), paramIdentityDecoder}},
        {QStringLiteral("serviceLaunchButtonKeyCode"),
         {QStringLiteral("Service launch button key code"), paramIdentityDecoder}},

        // Versions
        {QStringLiteral("contentVersion"),
         {QStringLiteral("Content version"), paramIdentityDecoder}},
        {QStringLiteral("originContentVersion"),
         {QStringLiteral("Origin content version"), paramIdentityDecoder}},
        {QStringLiteral("targetContentVersion"),
         {QStringLiteral("Target content version"), paramIdentityDecoder}},
        {QStringLiteral("masterVersion"), {QStringLiteral("Master version"), paramIdentityDecoder}},
        {QStringLiteral("requiredSystemSoftwareVersion"),
         {QStringLiteral("Minimum system software"), decodeHexVersion}},
        {QStringLiteral("sdkVersion"), {QStringLiteral("SDK version"), decodeHexVersion}},
        {QStringLiteral("versionFileUri"),
         {QStringLiteral("Version file URI"), paramIdentityDecoder}},

        // Attributes
        {QStringLiteral("attribute"), {QStringLiteral("Attribute"), decodeAttribute}},
        {QStringLiteral("attribute2"), {QStringLiteral("Attribute 2"), decodeAttribute2}},
        {QStringLiteral("attribute3"), {QStringLiteral("Attribute 3"), decodeAttribute3}},

        // Sizes
        {QStringLiteral("downloadDataSize"),
         {QStringLiteral("Download data size"), decodeByteSize}},
        {QStringLiteral("massSize"), {QStringLiteral("Mass size"), paramIdentityDecoder}},
        {QStringLiteral("amm"), {QStringLiteral("Address map manager"), paramIdentityDecoder}},
        {QStringLiteral("amm.vaRangeInGib"), {QStringLiteral("VA range"), decodeGibSize}},
        {QStringLiteral("amm.multimapVaRangeInGib"),
         {QStringLiteral("Multimap VA range"), decodeGibSize}},
        {QStringLiteral("kernel"), {QStringLiteral("Kernel"), paramIdentityDecoder}},
        {QStringLiteral("kernel.cpuPageTableSize"),
         {QStringLiteral("CPU page table size"), decodeMemorySize}},
        {QStringLiteral("kernel.gpuPageTableSize"),
         {QStringLiteral("GPU page table size"), decodeMemorySize}},
        {QStringLiteral("kernel.flexibleMemorySize"),
         {QStringLiteral("Flexible memory size"), decodeMemorySize}},

        // Localized parameters
        {QStringLiteral("localizedParameters"),
         {QStringLiteral("Localized parameters"), paramIdentityDecoder}},
        {QStringLiteral("localizedParameters.defaultLanguage"),
         {QStringLiteral("Default language"), paramIdentityDecoder}},

        // Age rating
        {QStringLiteral("ageLevel"), {QStringLiteral("Age level"), paramIdentityDecoder}},
        {QStringLiteral("ageLevel.default"), {QStringLiteral("Default"), paramIdentityDecoder}},

        // Add-on content
        {QStringLiteral("addcont"), {QStringLiteral("Additional content"), paramIdentityDecoder}},
        {QStringLiteral("addcont.serviceIdForSharing"),
         {QStringLiteral("Service IDs for sharing"), paramIdentityDecoder}},
        {QStringLiteral("addcont.serviceIdForSharing[]"),
         {QStringLiteral("Service ID"), paramIdentityDecoder}},

        // Save data
        {QStringLiteral("savedata"), {QStringLiteral("Save data"), paramIdentityDecoder}},
        {QStringLiteral("savedata.titleIdForSharing"),
         {QStringLiteral("Title ID for sharing"), paramIdentityDecoder}},
        {QStringLiteral("savedata.titleIdForTransferring"),
         {QStringLiteral("Title IDs for transferring"), paramIdentityDecoder}},
        {QStringLiteral("savedata.titleIdForTransferring[]"),
         {QStringLiteral("Title ID"), paramIdentityDecoder}},
        {QStringLiteral("savedata.titleIdForTransferringPs4"),
         {QStringLiteral("PS4 title IDs for transferring"), paramIdentityDecoder}},
        {QStringLiteral("savedata.titleIdForTransferringPs4[]"),
         {QStringLiteral("PS4 title ID"), paramIdentityDecoder}},

        // Game intent
        {QStringLiteral("gameIntent"), {QStringLiteral("Game intent"), paramIdentityDecoder}},
        {QStringLiteral("gameIntent.permittedIntents"),
         {QStringLiteral("Permitted intents"), paramIdentityDecoder}},
        {QStringLiteral("gameIntent.permittedIntents[]"),
         {QStringLiteral("Intent"), paramIdentityDecoder}},
        {QStringLiteral("gameIntent.permittedIntents[].intentType"),
         {QStringLiteral("Intent type"), decodeIntentType}},

        // Disc
        {QStringLiteral("disc"), {QStringLiteral("Disc"), paramIdentityDecoder}},
        {QStringLiteral("disc[]"), {QStringLiteral("Disc entry"), paramIdentityDecoder}},
        {QStringLiteral("disc[].role"), {QStringLiteral("Role"), paramIdentityDecoder}},
        {QStringLiteral("disc[].masterDataId"),
         {QStringLiteral("Master data ID"), paramIdentityDecoder}},
        {QStringLiteral("disc[].contents"), {QStringLiteral("Contents"), paramIdentityDecoder}},
        {QStringLiteral("disc[].contents[]"), {QStringLiteral("Content"), paramIdentityDecoder}},
        {QStringLiteral("disc[].contents[].contentId"),
         {QStringLiteral("Content ID"), paramIdentityDecoder}},
        {QStringLiteral("disc[].contents[].contentType"),
         {QStringLiteral("Content type"), decodeDiscContentType}},
        {QStringLiteral("disc[].files"), {QStringLiteral("Files"), paramIdentityDecoder}},
        {QStringLiteral("disc[].files[]"), {QStringLiteral("File"), paramIdentityDecoder}},
        {QStringLiteral("disc[].files[].fileName"),
         {QStringLiteral("File name"), paramIdentityDecoder}},
        {QStringLiteral("disc[].files[].digests"),
         {QStringLiteral("Digest"), paramIdentityDecoder}},
        {QStringLiteral("discNumber"), {QStringLiteral("Disc number"), paramIdentityDecoder}},
        {QStringLiteral("discTotal"), {QStringLiteral("Total discs"), paramIdentityDecoder}},

        // Publishing tools
        {QStringLiteral("pubtools"), {QStringLiteral("Publishing tools"), paramIdentityDecoder}},
        {QStringLiteral("pubtools.creationDate"),
         {QStringLiteral("Creation date"), decodeCreationDate}},
        {QStringLiteral("pubtools.toolVersion"),
         {QStringLiteral("Tool version"), paramIdentityDecoder}},
        {QStringLiteral("pubtools.submission"),
         {QStringLiteral("Submission"), paramIdentityDecoder}},
        {QStringLiteral("pubtools.loudnessSnd0"),
         {QStringLiteral("snd0 loudness (LUFS)"), paramIdentityDecoder}},
        {QStringLiteral("pubtoolsVersion"),
         {QStringLiteral("Publishing tools version"), paramIdentityDecoder}},

        // PSSR / machine learning upscaler
        {QStringLiteral("psml"),
         {QStringLiteral("Machine learning upscaler"), paramIdentityDecoder}},
        {QStringLiteral("psml.mfsrVersion"),
         {QStringLiteral("MFSR version"), paramIdentityDecoder}},

        // ASA
        {QStringLiteral("asa"), {QStringLiteral("ASA"), paramIdentityDecoder}},
        {QStringLiteral("asa.code"), {QStringLiteral("ASA codes"), paramIdentityDecoder}},
        {QStringLiteral("asa.sign"), {QStringLiteral("ASA signature"), paramIdentityDecoder}},
        {QStringLiteral("asa.sign[]"), {QStringLiteral("Signature block"), paramIdentityDecoder}},

        // Misc
        {QStringLiteral("usbDir"), {QStringLiteral("USB directories"), paramIdentityDecoder}},
        {QStringLiteral("usbDir[]"), {QStringLiteral("Directory"), paramIdentityDecoder}},
        {QStringLiteral("userDefinedParam1"),
         {QStringLiteral("User defined param 1"), paramIdentityDecoder}},
        {QStringLiteral("userDefinedParam2"),
         {QStringLiteral("User defined param 2"), paramIdentityDecoder}},
        {QStringLiteral("userDefinedParam3"),
         {QStringLiteral("User defined param 3"), paramIdentityDecoder}},
        {QStringLiteral("userDefinedParam4"),
         {QStringLiteral("User defined param 4"), paramIdentityDecoder}},
    };
    return map;
}

// Splits "a.b[0].c" into its last component, so unmapped keys still read as a
// field name rather than a full path.
inline QString paramLastPathComponent(const QString& lookupKey) {
    const int dot = lookupKey.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? lookupKey.mid(dot + 1) : lookupKey;
}

// Friendly label for a flattened key. Handles the two field groups whose names
// are data rather than a fixed schema: per-region ageLevel and per-locale
// localizedParameters.
inline QString paramDisplayName(const QString& lookupKey) {
    if (const auto it = paramKeyMap().find(lookupKey); it != paramKeyMap().end()) {
        return it->displayName;
    }

    const QStringList parts = lookupKey.split(QLatin1Char('.'));

    // "ageLevel.<CC>"
    if (parts.size() == 2 && parts.at(0) == QStringLiteral("ageLevel")) {
        const auto region = paramRegionNames().find(parts.at(1));
        if (region != paramRegionNames().end()) {
            return QStringLiteral("%1 (%2)").arg(*region, parts.at(1));
        }
    }

    // "localizedParameters.<locale>" and "localizedParameters.<locale>.titleName"
    if (parts.size() >= 2 && parts.at(0) == QStringLiteral("localizedParameters")) {
        const QString language = paramLanguageName(parts.at(1));
        if (parts.size() == 2) {
            return language;
        }
        if (parts.size() == 3 && parts.at(2) == QStringLiteral("titleName")) {
            return QStringLiteral("Title name (%1)").arg(language);
        }
    }

    return paramLastPathComponent(lookupKey);
}

// Decoded, human-readable form of a value. Unmapped keys pass through unchanged.
inline QString paramDecodeValue(const QString& lookupKey, const QString& rawValue) {
    const auto it = paramKeyMap().find(lookupKey);
    if (it == paramKeyMap().end() || !it->decode) {
        return rawValue;
    }
    return it->decode(rawValue);
}
