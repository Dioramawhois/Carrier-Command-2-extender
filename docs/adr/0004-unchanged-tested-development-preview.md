# ADR 0004: publish the unchanged tested development runtime

Date: 2026-11-06. Status: accepted for the 0.9.0-dev prerelease.

Island Airfields 2.2 needs the optional equipment interface absent from the
published 0.8.1 binary. The owner tested the exact 0.9.0-dev runtime, including
equipment changes and three-arrival save/reload. Changing its embedded version
would change the accepted binary hash without changing gameplay behavior.

Publish the unchanged native bytes under the immutable `0.9.0-dev` tag, explicitly
as a development prerelease. The package tool accepts a `-dev` version only with
`--allow-development-version`; it always sets `prerelease` and records
`development_preview`. Clean-source, exact input hashes, matching native tests,
source archive and pinned provider-source checks remain mandatory.

Documentation/packaging changes may produce a newer source commit while the
compiled inputs and DLL remain byte-identical. Rebuild from that clean commit
and assert the tested hash before staging. Include the corresponding complete
source and provider sources. Never relabel this preview as a stable release or
infer remote-client, dedicated-server, full Brake or CPU-preview acceptance.

Future native changes receive a new version and their own exact-binary checks;
do not replace the published tag or reuse its acceptance for changed bytes.
