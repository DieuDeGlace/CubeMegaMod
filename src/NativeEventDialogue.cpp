#include "NativeEventDialogue.h"
#include "cwsdk.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace {
constexpr size_t DialogSize = 0x1b0;
using DialogConstructor = void *(*)(void *);
using DialogDestructor = void (*)(void *);
using AddPhrase = void (*)(void *, const std::wstring *, const FloatRGBA *, int);
using ClearDialogs = void (*)(void *);

// a3170 only reads the native vector's begin/end pointers, advancing by 1b0.
// Native constructors/destructors own every string, phrase and action list.
// The SDK's partial Dialog/Phrase declarations do not describe those lists.
struct DialogRange {
    unsigned char *begin;
    unsigned char *end;
    unsigned char *capacity;
};
using PresentDialogue = void (*)(cube::Game *, cube::Creature *, cube::Creature *,
                                 const DialogRange *);

static_assert(sizeof(std::wstring) == 0x20, "Native speech requires release MSVC strings");
static_assert(sizeof(DialogRange) == 0x18, "Native vector header changed");
static_assert(offsetof(cube::Game, gui) == 0x118, "Native game GUI layout changed");
static_assert(offsetof(cube::GUI, speech_widgets) == 0x2e8, "Native speech vector layout changed");
static_assert(offsetof(cube::SpeechWidget, dialogs) == 0x1e8,
              "Native speech dialogs layout changed");
static_assert(offsetof(cube::SpeechWidget, read_time) == 0x200,
              "Native speech timer layout changed");
static_assert(offsetof(cube::SpeechWidget, dialog_index) == 0x204,
              "Native speech index layout changed");
static_assert(offsetof(cube::SpeechWidget, text_uncovered) == 0x208,
              "Native speech reveal layout changed");
static_assert(offsetof(cube::SpeechWidget, field_210) == 0x210,
              "Native speech speaker layout changed");

bool NativeEntriesAvailable() {
    const unsigned char presenter[16] = {0x40, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41,
                                         0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44};
    const unsigned char constructor[16] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                           0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe};
    const unsigned char phrase[16] = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x57, 0x41, 0x54,
                                      0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d};
    const unsigned char destructor[16] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                          0xec, 0x20, 0x48, 0x8b, 0x81, 0xa0, 0x01, 0x00};
    const unsigned char clear[16] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                     0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48};
    return std::memcmp(CWOffset(0xa3170), presenter, sizeof(presenter)) == 0 &&
           std::memcmp(CWOffset(0x15c380), constructor, sizeof(constructor)) == 0 &&
           std::memcmp(CWOffset(0x16c880), phrase, sizeof(phrase)) == 0 &&
           std::memcmp(CWOffset(0x82630), destructor, sizeof(destructor)) == 0 &&
           std::memcmp(CWOffset(0x8adf0), clear, sizeof(clear)) == 0;
}

struct NativeDialog {
    alignas(8) unsigned char bytes[DialogSize];

    NativeDialog() {
        reinterpret_cast<DialogConstructor>(CWOffset(0x15c380))(bytes);
    }
    ~NativeDialog() {
        reinterpret_cast<DialogDestructor>(CWOffset(0x82630))(bytes);
    }
    NativeDialog(const NativeDialog &) = delete;
    NativeDialog &operator=(const NativeDialog &) = delete;
};

cube::SpeechWidget *SpeakerWidget(cube::Game *game, cube::Creature *speaker) {
    for (auto widget : game->gui.speech_widgets)
        if (widget && widget->field_210 == speaker->id)
            return widget;
    return nullptr;
}

void ResetReading(cube::SpeechWidget *widget) {
    widget->read_time = 0.f;
    widget->dialog_index = 0;
    widget->text_uncovered = 0.f;
}
} // namespace

bool overworldBridge::ShowNativeEventDialogue(cube::Game *game, cube::Creature *speaker,
                                              const wchar_t *text) {
    if (!game || !game->world || !speaker || !text || !*text || !game->gui.speech_widget ||
        !game->gui.speech_node || !game->gui.interface_elements_node || !NativeEntriesAvailable())
        return false;
    auto player = game->GetPlayer();
    if (!player)
        return false;

    NativeDialog dialog;
    std::wstring words(text);
    // Native default phrase color is white (Steam rdata RVA 42e8c0).
    FloatRGBA color(1.f, 1.f, 1.f, 1.f);
    reinterpret_cast<AddPhrase>(CWOffset(0x16c880))(dialog.bytes, &words, &color, 0);

    // Repeated event interactions replace this speaker's current bubble. Native
    // Dialog destruction clears action lists without changing event callbacks.
    if (auto existing = SpeakerWidget(game, speaker)) {
        reinterpret_cast<ClearDialogs>(CWOffset(0x8adf0))(&existing->dialogs);
        ResetReading(existing);
    }
    DialogRange range{dialog.bytes, dialog.bytes + DialogSize, dialog.bytes + DialogSize};
    // The constructor leaves the string at +10 empty. Its length at +20 keeps
    // a3170 on the NPC's world-space widget instead of the central speech panel.
    reinterpret_cast<PresentDialogue>(CWOffset(0xa3170))(game, speaker, player, &range);
    auto widget = SpeakerWidget(game, speaker);
    if (!widget)
        return false;
    ResetReading(widget);
    return true;
}
