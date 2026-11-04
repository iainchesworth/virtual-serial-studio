//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "language/language_choice.hpp"

#include <QGuiApplication>
#include <QStringList>

namespace
{

QStringList everything()
{
    return vss::gui::supported_languages();
}

} // namespace

TEST_CASE("language: any English is English")
{
    for (const char* tag : {"en", "en-GB", "en_AU", "en-Latn-US", "EN-us"})
    {
        const auto choice =
            vss::gui::choose_language({QString::fromLatin1(tag)}, everything());
        REQUIRE_EQ(choice.code, QStringLiteral("en"));
        REQUIRE_EQ(choice.matched, QString::fromLatin1(tag));
    }
}

TEST_CASE("language: the first preference with a catalogue wins")
{
    // A phone that lists English (UK), German and French gets English.
    REQUIRE_EQ(vss::gui::choose_language({QStringLiteral("en-GB"),
                                          QStringLiteral("de-DE"),
                                          QStringLiteral("fr")},
                                         everything())
                   .code,
               QStringLiteral("en"));
    // A preference without a catalogue is passed over.
    const auto choice = vss::gui::choose_language(
        {QStringLiteral("it-IT"), QStringLiteral("de-DE")},
        {QStringLiteral("en"), QStringLiteral("de")});
    REQUIRE_EQ(choice.code, QStringLiteral("de"));
    REQUIRE_EQ(choice.matched, QStringLiteral("de-DE"));
}

TEST_CASE("language: the old codes for Hebrew and Yiddish")
{
    REQUIRE_EQ(
        vss::gui::choose_language({QStringLiteral("iw-IL")}, everything()).code,
        QStringLiteral("he"));
    REQUIRE_EQ(
        vss::gui::choose_language({QStringLiteral("ji")}, everything()).code,
        QStringLiteral("yi"));
    REQUIRE_EQ(
        vss::gui::choose_language({QStringLiteral("ar-EG")}, everything()).code,
        QStringLiteral("ar"));
}

TEST_CASE("language: Chinese is Simplified unless the tag says Traditional")
{
    for (const char* tag : {"zh", "zh-CN", "zh-Hans-CN", "zh_SG"})
    {
        REQUIRE_EQ(
            vss::gui::choose_language({QString::fromLatin1(tag)}, everything())
                .code,
            QStringLiteral("zh_CN"));
    }
    for (const char* tag : {"zh-TW", "zh-Hant", "zh-HK", "zh-Hant-MO"})
    {
        const auto choice = vss::gui::choose_language(
            {QString::fromLatin1(tag), QStringLiteral("fr")}, everything());
        REQUIRE_EQ(choice.code, QStringLiteral("fr"));
    }
}

TEST_CASE("language: nothing that matches is English")
{
    const auto choice = vss::gui::choose_language(
        {QStringLiteral("sv-SE"), QStringLiteral("")}, everything());
    REQUIRE_EQ(choice.code, QStringLiteral("en"));
    REQUIRE(choice.matched.isEmpty());
    REQUIRE_EQ(vss::gui::choose_language({}, everything()).code,
               QStringLiteral("en"));
}

TEST_CASE("language: Hebrew, Yiddish and Arabic are right to left")
{
    for (const char* code : {"he", "yi", "ar"})
        REQUIRE(vss::gui::is_right_to_left(QString::fromLatin1(code)));
    for (const char* code : {"en", "fr", "es", "de", "ja", "zh_CN"})
        REQUIRE(!vss::gui::is_right_to_left(QString::fromLatin1(code)));
}

TEST_CASE("language: every language of the standard is supported")
{
    const QStringList expected{"en", "fr", "es",    "de", "he",
                               "yi", "ja", "zh_CN", "ar"};
    REQUIRE_EQ(vss::gui::supported_languages(), expected);
}

TEST_CASE("language: About can say what was chosen and what was offered")
{
    vss::gui::record_language_choice(
        {QStringLiteral("de"), QStringLiteral("de-AT")},
        {QStringLiteral("de-AT"), QStringLiteral("en")});
    const QString text = vss::gui::language_choice_description();
    REQUIRE(text.startsWith(QStringLiteral("de ")));
    REQUIRE(text.contains(QStringLiteral("from de-AT")));
    REQUIRE(text.contains(QStringLiteral("de-AT, en")));

    vss::gui::record_language_choice({QStringLiteral("en"), {}}, {});
    REQUIRE(vss::gui::language_choice_description().contains(
        QStringLiteral("nothing matched")));
}

TEST_CASE("language: without a catalogue the program stays English")
{
    // No catalogue is built into this test, so a request for German falls back
    // to English and the layout stays left to right.
    qputenv("VSS_LANGUAGE", QByteArrayLiteral("de"));
    const auto choice = vss::gui::apply_language();
    qunsetenv("VSS_LANGUAGE");
    REQUIRE_EQ(choice.code, QStringLiteral("en"));
    REQUIRE(QGuiApplication::layoutDirection() == Qt::LeftToRight);
}
