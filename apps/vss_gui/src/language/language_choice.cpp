//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "language/language_choice.hpp"

#include <vss/core/profile.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QTranslator>

namespace vss::gui
{

namespace
{

QString g_description;

/// The code of the catalogue that serves a tag, or an empty string when there
/// is none for it.
QString catalogue_for(const QString& tag, const QStringList& available)
{
    const QStringList parts =
        QString(tag).replace(u'_', u'-').split(u'-', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    QString language = parts.front().toLower();
    // The old codes that systems still send for Hebrew and Yiddish.
    if (language == QStringLiteral("iw"))
        language = QStringLiteral("he");
    else if (language == QStringLiteral("ji"))
        language = QStringLiteral("yi");

    QString code = language;
    if (language == QStringLiteral("zh"))
    {
        // Simplified, the one there is, unless the tag says Traditional.
        for (const QString& part : parts)
        {
            const QString lower = part.toLower();
            if (lower == QStringLiteral("hant") ||
                lower == QStringLiteral("tw") ||
                lower == QStringLiteral("hk") || lower == QStringLiteral("mo"))
            {
                return {};
            }
        }
        code = QStringLiteral("zh_CN");
    }
    return available.contains(code) ? code : QString();
}

QString catalogue_path(const QString& code)
{
    return QStringLiteral(":/i18n/vss_gui_%1.qm").arg(code);
}

} // namespace

QStringList supported_languages()
{
    return {
        QStringLiteral("en"), QStringLiteral("fr"),    QStringLiteral("es"),
        QStringLiteral("de"), QStringLiteral("he"),    QStringLiteral("yi"),
        QStringLiteral("ja"), QStringLiteral("zh_CN"), QStringLiteral("ar")};
}

language_choice choose_language(const QStringList& preferred,
                                const QStringList& available)
{
    VSS_PROFILE_ZONE("gui.language.choose");
    for (const QString& tag : preferred)
    {
        const QString code = catalogue_for(tag, available);
        if (!code.isEmpty())
            return {code, tag};
    }
    return {QStringLiteral("en"), {}};
}

bool is_right_to_left(const QString& code)
{
    return code == QStringLiteral("he") || code == QStringLiteral("yi") ||
           code == QStringLiteral("ar");
}

void record_language_choice(const language_choice& choice,
                            const QStringList& preferred)
{
    g_description =
        QStringLiteral("%1 (%2; the system offers %3)")
            .arg(choice.code,
                 choice.matched.isEmpty()
                     ? QStringLiteral("nothing matched")
                     : QStringLiteral("from ") + choice.matched,
                 preferred.isEmpty() ? QStringLiteral("nothing")
                                     : preferred.join(QStringLiteral(", ")));
}

QString language_choice_description()
{
    return g_description.isEmpty() ? QStringLiteral("en (not chosen yet)")
                                   : g_description;
}

language_choice apply_language()
{
    VSS_PROFILE_ZONE("gui.language.apply");
    // Only a language whose catalogue is in the program can be chosen, so the
    // layout direction never mirrors English text.
    QStringList available{QStringLiteral("en")};
    for (const QString& code : supported_languages())
    {
        if (code != QStringLiteral("en") && QFile::exists(catalogue_path(code)))
            available.append(code);
    }

    QStringList preferred;
    const QString forced = qEnvironmentVariable("VSS_LANGUAGE");
    if (!forced.isEmpty())
        preferred.append(forced);
    else
        preferred = QLocale::system().uiLanguages();

    const language_choice choice = choose_language(preferred, available);
    record_language_choice(choice, preferred);

    if (choice.code != QStringLiteral("en"))
    {
        auto* translator = new QTranslator(QCoreApplication::instance());
        if (translator->load(catalogue_path(choice.code)))
            QCoreApplication::installTranslator(translator);
        else
            delete translator;
    }
    QGuiApplication::setLayoutDirection(
        is_right_to_left(choice.code) ? Qt::RightToLeft : Qt::LeftToRight);
    return choice;
}

} // namespace vss::gui
