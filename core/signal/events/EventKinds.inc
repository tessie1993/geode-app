// Every event the engine raises, one row each: GEODE_EVENT(Name, "wire_id").
// Append only: the ordinal is the wire value, and saved event routes carry it.
//
// Musical events come from the sound frame (SoundEvents.cpp says which field or rule raises each);
// engine events are pushed by whoever owns the moment (the renderer for a style switch).
//
// To add one: a row here, and either a row in SoundEvents.cpp's trigger table (a sound-frame trigger)
// or a rule in SoundEvents::detect (a derived event), or a push from the engine. Nothing else changes.

// Drums and onsets
GEODE_EVENT(Kick, "kick")
GEODE_EVENT(Snare, "snare")
GEODE_EVENT(Hat, "hat")
GEODE_EVENT(Onset, "onset")
// The grid
GEODE_EVENT(Beat, "beat")
GEODE_EVENT(Downbeat, "downbeat")
GEODE_EVENT(Phrase, "phrase")
// Loudness
GEODE_EVENT(HardCut, "hard_cut")
GEODE_EVENT(Surge, "surge")
GEODE_EVENT(Silence, "silence")
GEODE_EVENT(SoundBack, "sound_back")
// Melody and harmony
GEODE_EVENT(NoteChange, "note_change")
GEODE_EVENT(Reattack, "reattack")
GEODE_EVENT(ChordChange, "chord_change")
GEODE_EVENT(KeyChange, "key_change")
// Form
GEODE_EVENT(SectionChange, "section_change")
GEODE_EVENT(BuildStart, "build_start")
GEODE_EVENT(BuildEnd, "build_end")
GEODE_EVENT(Drop, "drop")
// Engine
GEODE_EVENT(StyleSwitched, "style_switched")
