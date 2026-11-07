# SDK updates independent of the ModLoader executable

CC2 ModLoader beta 12 and later can verify an official SDK release from a signed
descriptor. Future compatible SDK DLLs no longer need a hash added to each
ModLoader EXE. The SDK stays a shared framework, with ordinary numbered releases.

The Workshop SDK package must include **content/cc2x/runtime_update.json** from
the release's **runtime-update.json** asset. The DLL stays at content/dinput8.dll.
The descriptor authenticates its exact byte hash/length, target game, API levels,
capabilities, increasing release sequence and source revision. Keep the same
publisher key across ordinary updates. A package-supplied public key cannot grant
trust; the matching public key is bundled with the client application.

`tools/package_release.py` signs this metadata automatically. Release maintainers
need the pinned `cryptography==50.0.2` developer dependency and an Ed25519 private
key outside every repository/source/release folder. The tool uses
CC2X_RUNTIME_SIGNING_KEY or the local publisher-key location by default; an
explicit --runtime-signing-key is also supported. Missing keys stop packaging
rather than shipping a future SDK that needs another application update.

The default sequence is the source commit's UTC timestamp. Ensure it increases
relative to the previously published descriptor; use --runtime-sequence for an
explicit newer release based on historical source. The target game and reviewed
predecessor hashes are maintained in sdk/runtime-update-policy.json. A signing
key rotation or a genuinely incompatible application protocol may still need an
application update. Compatible SDK updates do not.

For an already released, unchanged runtime, tools/sign_runtime_release.py can
produce just the signed metadata from its verified RELEASE.json and DLL. No
version-only native rebuild or false gameplay acceptance is needed. Publish the
descriptor alongside that existing runtime and include it in Workshop payloads.
Never overwrite frozen native assets or change their original build records.

Players still use the GUI checkbox/apply/Launch workflow, with a closed-game
transaction, backup and Undo. Signature verification authenticates the publisher;
it does not certify every gameplay case or grant trust to arbitrary DLL plugins.
