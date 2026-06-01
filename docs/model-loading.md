# Model loading: referenced, not copied

## Short answer

When you pick a model from the file picker, the app **references the file in
place** (e.g. `/storage/emulated/0/Documents/ggml-base.bin`). It does **not**
copy the model into the app's own storage. Copying would not make transcription
any faster — once a model is loaded its weights live in RAM either way.

## How it works

1. **Pick** — `MainActivity.onActivityResult` receives the picked URI.
2. **Resolve path** — `resolveRealPath()` turns the URI into a real filesystem
   path when it can (Documents, Downloads, external storage). That path is saved
   to `SharedPreferences` (`PREF_MODEL_PATH`) and handed to native via
   `nativeOnModelPicked()`.
3. **Load** — native `loadModelFromPath()` →
   `whisper_init_from_file_with_params()` reads the file from that path once and
   builds an in-memory `whisper_context` (`wa->whisperCtx`). That context stays
   resident for the whole session; transcription never re-reads the file.
4. **Next launch** — on cold start `getSavedModelPath()` returns the saved path
   and the same load happens, which is why a restart shows the model "instantly"
   (it is just loading from the remembered path).

### The copy fallback

`copyToInternal()` copies the model into the app-private `files/models/`
directory, but it **only runs when `resolveRealPath()` returns null** — e.g. a
cloud / content URI that has no real on-disk path. For a normal Documents file,
the direct path is used and nothing is copied.

## Reference vs. copy — trade-offs

| | Reference Documents (current) | Copy into app storage |
|---|---|---|
| Speed once loaded | identical (model in RAM) | identical |
| First-load speed | one read | one read + one-time copy |
| File moved/deleted later | reload fails → "FAILED TO LOAD" | keeps working |
| Needs "All files access" | yes (reads external storage) | no (private dir) |
| Disk usage | single copy | duplicated (~model size) |

**Takeaway:** copying buys robustness (survives the source file being moved or
deleted, and drops the all-files-access dependency), not speed. Transcription
performance is the same because the weights are loaded into memory at model-load
time regardless of where the file sits.
