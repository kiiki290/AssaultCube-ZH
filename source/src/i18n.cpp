// i18n.cpp: UI translation lookup
//
// Translations are keyed by the exact source text as it is displayed (the
// English original, or a printf-style format template with its specifiers
// intact). Lookups that miss return the original untouched, so chat text,
// player names, numbers and user input are never altered unless a pack
// deliberately defines them.
//
// The lookup happens at the presentation boundary - the text drawing and the
// console/HUD output helpers - rather than at each of the ~400 call sites. That
// way the CubeScript-defined menus are covered with no config changes at all,
// and switching languages re-renders everything immediately.

#include "cube.h"

static hashtable<const char *, const char *> translations;

const char *tr(const char *s)
{
    if(!s || !*s) return s;
    const char **found = (const char **)translations.access(s);
    return found && *found ? *found : s;
}

void addtranslation(const char *en, const char *zh)
{
    if(!en || !*en || !zh) return;
    const char **found = (const char **)translations.access(en);
    if(found)
    {
        if(*found) DELSTRING(*found);
        *found = newstring(zh);
    }
    else translations[newstring(en)] = newstring(zh);
}
COMMANDN(translate, addtranslation, "ss");

static void cleartranslations()
{
    vector<const char *> keys;
    enumeratek(translations, const char *, k, keys.add(k));
    loopv(keys)
    {
        const char **v = (const char **)translations.access(keys[i]);
        if(v && *v) DELSTRING(*v);
        char *key = (char *)keys[i];
        DELSTRING(key);
    }
    translations.clear();
}

void reloadtranslation(const char *code)
{
    cleartranslations();
    if(!code || !*code || !strcmp(code, "en")) return; // English is the built-in source language

    defformatstring(file)("config" PATHDIVS "lang" PATHDIVS "%s.cfg", code);
    if(!execfile(file))
        conoutf("\f3no translation pack for language \"%s\" (expected %s)", code, file);
}

// persisted automatically, so a player's choice survives restarts
SVARFP(uilang, "en", reloadtranslation(uilang));
