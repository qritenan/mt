#pragma once
// UI translations. English is the source text and the key; T() returns the translation for the
// chosen language, or the English text when there is none. The --list / --dump output stays English.
#include <string>

// True when the Windows display language is Turkish: the default until the user picks a language.
bool WindowsIsTurkish();

void SetUiTurkish(bool on);
bool UiTurkish();

const wchar_t* T(const wchar_t* en);
inline std::wstring T(const std::wstring& en) { return T(en.c_str()); }
