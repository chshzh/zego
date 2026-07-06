# nrf_security patches

Vendored-file patches applied on top of the checked-out `nrf` module. `west
update` re-checks out `nrf` from the manifest revision and will silently
drop these edits, so **reapply after every `west update`**.

CI (`validation.yml` / `release.yml`) applies these automatically in an
"Apply vendored nrf_security patches" step right after its own `west
update`, so this only needs to be done by hand for local/manual workspaces.

## 0001-forward-mbedtls-memory-debug.patch

**Files:**
- `nrf/subsys/nrf_security/cmake/psa_crypto_want_config.cmake`
- `nrf/subsys/nrf_security/configs/psa_crypto_want_config.h.template`

**Why:** `CONFIG_MBEDTLS_MEMORY_DEBUG` is a real Zephyr Kconfig symbol, but
NCS v3.4.0's PSA-only crypto config generator never forwarded it into the
generated `psa/crypto_config.h`, unlike the neighbouring
`MBEDTLS_MEMORY_BUFFER_ALLOC_C`. Without the macro defined,
`mbedtls_memory_buffer_alloc_cur_get()`/`_max_get()` don't exist in the built
library, so `bricks/memonitor` can't report mbedTLS heap usage (used by
`CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR` and the `zview_mbedtls_stats` bridge
that ZView reads over SWD).

**Fix:** forward the symbol the same way its neighbour already is — one line
in the `.cmake` file *and* one matching `#cmakedefine` line in the
`.h.template` it fills in (both are required; the `.cmake` line alone is a
silent no-op without the template placeholder).

**Apply:**

```bash
cd /opt/nordic/ncs/v3.4.0/nrf
git apply /opt/nordic/ncs/v3.4.0/zego/patches/nrf_security/0001-forward-mbedtls-memory-debug.patch
```
