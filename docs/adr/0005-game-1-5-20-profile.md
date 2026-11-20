# ADR 0005: exact-build compatibility for the 1.5.20 security hotfix

Date: 2026-11-20. Status: compatibility candidate.

The owner requested an urgent SDK update and publication after the game update.
Steam build 26732430 changes native code, data addresses and game-owned Lua
initialization. Treating it as the previous profile would pass the wrong CPU
register to the Lua bridge and would not preserve exact signature guards.

Release 0.11.1-dev targets only the authenticated new executable. Previous
immutable SDK tags remain available for older games. Use the new build name,
SHA-256, timestamp, image size, native tables and relocation operands together;
do not alias the new executable to the old profile or relax unknown-build gates.

The game now removes four Lua loading globals before the insertion point.
Read the Lua state from ESI and hook a complete twelve-byte instruction window
after that removal. The SDK must not restore those globals. Its independent,
sandboxed module VM remains a separate runtime with the existing contract.

Keep all public ABI layouts unchanged. Signed distribution metadata binds the
runtime to the new executable hash; it does not authorize ModLoader to accept
an unknown game or replace an unrelated DLL. Keep static executable checks,
native fixtures, isolated game loading and ordinary-host gameplay as distinct
evidence. Include exact source, compiler, artifact and test hashes in packages.
