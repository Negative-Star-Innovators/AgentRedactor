# Release and product improvements

This is a planning list, not authorization to change the release process or application. **Ask the user before implementing any item on this list.** No item should be implemented merely because it appears here.

## macOS: promote tested candidates

Status: approved and implemented.

Signed candidate builds on `main` retain the package, installation, update, Keychain, and LocalAuthentication tests. Once the exact candidate has been tested and approved, it is published or submitted without a tag-time rebuild.

For self-distribution, the protected publish-only job verifies the candidate run and version; on tags it also verifies that the candidate and tag identify the same commit. It rechecks the Developer ID signature and notarization, uploads the existing DMG and Velopack files to R2, and verifies that the live DMG is identical to the candidate.

For the App Store, manually submit the existing Apple Distribution-signed ZIP from the tested `main` candidate. Version tags do not trigger another Store build. Record the candidate run ID and commit used for each submission so the submitted binary remains traceable.

The candidate must already contain the final version. Changing the version after testing requires a new candidate. If the candidate artifact has expired, rebuild and test a new candidate instead of bypassing the checks.

## Linux self-release: reuse the main candidate

Status: approved and implemented; cross-platform CI validation is required before publication.

`build-linux.yml` builds final-version x64/ARM64 candidates on `main`. Tags resolve the successful candidate at the same commit; manual publication accepts its run ID. The publish-only path verifies the run, versions, and packages, then uploads the existing artifacts. Pull-request artifacts cannot be promoted, and both architectures must have succeeded in the candidate run.

## Windows self-release: final-candidate reuse

Status: approved and implemented; cross-platform CI validation is required before publication.

`release-selfrelease.yml` now produces final-version, fully tested x64/ARM64 candidates on `main`. Tags and manual publication promote those exact packages. The candidate run includes the existing installation, upgrade, migration, locked-save, and GUI-test gates. PR-only synthetic versions cannot be promoted.

## Coordinated cross-platform release: selective CI and publication

Status: approved and implemented; full cross-platform validation is required before staged publication.

`scripts/ci_platform_changes.py` selects affected platforms for pull requests and merge candidates. Platform-only changes skip the other platforms; shared C++, models, translations, common tests, version files, and workflow changes select every affected platform. Build-step gates preserve the existing required per-architecture check names.

Merges build final-version candidates only for affected platforms and never publish self-release updates automatically. Each platform's publication promotes only its own tested candidate, allowing the coordinated code to be released separately to macOS, Windows, and Linux. Full automated installation, upgrade, GUI, and self-release tests remain required, with manual smoke-testing before publication.

## Windows MSIX: retain artifact reuse

No change is currently recommended. The MSIX workflow already publishes the exact packages built and tested on `main` to the rolling `latest` prerelease. Use this as a reference for artifact reuse rather than adding another build stage.

## Cross-platform locked-settings preservation

Status: approved and implemented; cross-platform CI validation is required before publication.

The shared C++ settings manager encrypts in-memory sensitive fields before locking and refuses to save plaintext sensitive fields while locked. A shared `--selftest-locked-settings` regression hook covers global saves while locked, unlock, and restart on all three platforms.

Previous-live upgrade tests on macOS, Windows, and Linux seed and verify API-key, keyword, and regex content and entry flags, profile identity, port, and statistics. Each runs unprotected and password-enabled cases. The protected case unlocks, locks, saves a global setting, unlocks again, and verifies every field after the upgrade.

## Post-unlock GUI profile refresh

Status: approved and implemented; cross-platform GUI validation is required before publication.

All three GUIs reload the complete profile after a locked-to-unlocked transition, including external control-API unlocks. This prevents empty locked-session snapshots from being shown or saved over the decrypted API key, keywords, and regexes. GUI regression tests restore populated profiles after a full restart, verify the actual controls contain the values, save an unrelated profile setting, and confirm that no sensitive field is lost.

## macOS localization completeness

Status: deferred; not implemented.

An audit found complete runtime coverage of all 215 localization keys across all 53 supported languages, with no empty values, missing referenced keys, or numbered-placeholder mismatches. The following gaps remain:

- Move user-facing hardcoded English errors into the shared translation catalogs. This includes engine-connection errors in `macos/gui/AgentRedactor/Core/EngineClient.swift`, authentication and Keychain errors in `LocalAuthGate.swift`, updater errors in `UI/Settings/UpdateProvider.swift` and `VelopackUpdateProvider.swift`, and the unlock fallback in `Core/AppState.swift`.
- Localize the supplemental model-download failure and retry status supplied by the C++ engine. The normal first-install download title, progress, failure heading, and Retry button are already localized; the additional engine-provided status can still be English.
- Review translations that exactly match English where this is not clearly intentional. Product names and technical terms such as Agent Redactor, PII, Regex, URL, CPU, and OK are expected to remain unchanged. Candidates for native-language review include `Agent Redactor - Crash` in eight languages and `AI Detection` in Filipino.
- Repair the `macos/tools/resw_to_xcstrings.py --check` consistency failure. Xcode has changed the String Catalog format/version and normalized `az-Latn` and `ha-Latn` to `az` and `ha`. The runtime `strings-manifest.json` is complete and matches the `.resw` sources, so this is a generated-catalog/tooling issue rather than missing runtime translations.

Preserve the existing verbatim-copy localization workflow: do not regenerate or retranslate existing valid translations. Reuse existing translations and translate only genuinely new or missing strings.

## Cross-platform tokenizer replacement and optimization

Status: Hugging Face integration and exact Anthropic-compatible opaque-field preservation are implemented in unmerged PR #82 on `perf/huggingface-tokenizer`. Windows x64 evaluation is complete; cross-platform integration is implemented for CI validation. Merging and production release are not approved yet. Provider-observed round-trip caching remains under discussion and is not implemented.

Long Anthropic `thinking.signature` and `redacted_thinking.data` values exposed a tokenizer bottleneck before chunked inference. `PIIDetector::DetectPII` tokenizes the complete value before creating bounded ONNX windows. `BPETokenizer::BpeEncodeWord` repeatedly scans every remaining pair and erases from a vector for each merge, producing approximately quadratic behaviour on long unbroken strings. A captured 100,277-character signature took about 396.9 seconds; Hugging Face `tokenizers` 0.22.2 processed 100,000 random Base64 characters through the same `tokenizer.json` in about 0.061 seconds on the investigation machine. Chunked ONNX inference already bounds model memory and is not the cause of this preprocessing stall.

The custom tokenizer has no recorded model-specific justification in its Git history. It also does not implement the regex pre-tokenizer declared by the bundled `tokenizer.json`; differential checks found token-ID differences for inputs including `JohnSmith1234` and `a+++++b`. These differences are not requirements to preserve. The reference tokenizer is the appropriate compatibility target, but PII accuracy must be regression-tested because changed token boundaries can change detections.

The completed Windows x64 A/B evaluation used the real engine, model, chunking, Viterbi decoder, proxy, and 804 ground-truth entities. Hugging Face matched the reference token IDs and UTF-8 byte offsets exactly, including Unicode cases where the custom tokenizer differed. Exact-span recall improved from 76.62% to 81.59% and exact-span precision from 86.15% to 88.89%. Overlap-based recall improved from 80.85% to 84.70% and precision from 90.91% to 92.28%; raw-leaked ground-truth strings decreased from 68 to 65. Both versions passed the existing chunked-inference regression test. These are corpus-specific evaluation results, not a universal model-accuracy guarantee.

For a 100,000-character random signature, tokenization fell from 311.62 seconds to 0.075 seconds and total request time from 346.54 seconds to 38.82 seconds. Peak request RSS was similar (about 4.08 GiB custom versus 4.04 GiB Hugging Face), while dense-corpus peak RSS was about 2.52 GiB versus 2.50 GiB. The engine executable grew from 0.98 MiB to 4.48 MiB. Hugging Face startup peak RSS was higher in this run (298 MiB versus 196 MiB), so initialization memory needs review during production integration.

The evaluation also exposed a separate integrity issue: both tokenizers caused the model to replace entire random Anthropic signatures with redaction labels, and a replay of ten distinct real signatures from the Demo account altered every one. Faster or reference-correct tokenization does not make opaque provider fields safe to alter. PR #82 therefore also preserves the exact approved schema-qualified opaque fields byte-for-byte; the request-stage timings above describe the pre-exemption evaluation, not repeated inference under the new policy.

For the real 100,277-character Demo signature, the Hugging Face engine took 0.076 seconds tokenizing, 33.25 seconds in ONNX `Run`, 4.15 seconds in Viterbi/probability decoding, 0.006 seconds assembling entities, and less than 0.001 seconds replacing labels and serializing JSON. Total request time was 37.54 seconds; the identical cached request took 0.040 seconds. The custom engine took 294.89 seconds tokenizing, 30.28 seconds in ONNX `Run`, 3.95 seconds in Viterbi/probability decoding, and 329.19 seconds overall; its identical cached request took 0.046 seconds. The upstream timing in this harness is a local mock-provider round trip, not an external Anthropic/OpenRouter call.

After schema preservation, replaying that same real signature through the updated engine preserved all 100,277 characters with matching SHA-256 hashes and zero redaction labels. The first request took 0.382 seconds and tokenized only the 47 characters of plaintext sibling content; the repeat took 0.025 seconds. No tokenizer or ONNX inference was applied to the opaque signature.

The implemented policy is schema-aware structural preservation rather than generic encryption detection or a hardcoded Anthropic hostname/model check. Preserve only a direct string `signature` inside an object with `type: "thinking"` and a direct string `data` inside an object with `type: "redacted_thinking"`. Continue scanning plaintext thinking, sibling fields, non-string values, and every unknown field. Add other providers only through explicitly reviewed schemas; do not bypass arbitrary Base64, high-entropy, or spaceless strings. Schema matching is primary so OpenRouter and compatible gateways keep working; trusted-profile and `/v1/messages` restrictions could provide defense-in-depth but are not part of this implementation.

### Security review before preserving opaque fields

This exemption is not risk-free. A crafted request could place a plaintext secret or encoded exfiltration payload in an exempt field, and Agent Redactor cannot verify Anthropic's cryptographic signature locally. Parent-type matching narrows the exemption but does not prove provider origin. If malicious request construction is in scope, consider restricting exemptions to trusted upstream profiles and `/v1/messages`, and optionally reject obvious user-configured keyword/regex matches rather than modifying the opaque value. Do not use neural redaction as an integrity check: every real signature tested was falsely redacted, so that approach breaks legitimate traffic.

Prompt injection is a different threat from PII exfiltration. Preserving opaque provider-issued data does not add natural-language instructions to a request, and an attacker cannot normally choose the encrypted reasoning without the provider's key. Plaintext thinking remains scanned, but Agent Redactor is a PII redactor, not a prompt-injection filter; malicious instructions in plaintext are outside its detection guarantee. Returning encrypted reasoning to the same provider can preserve earlier malicious context, but altering its signature is corruption, not a safe prompt-injection defense.

### Configurable opaque-field policy

There is no reliable universal encryption detector. Ciphertext, random data, hashes, API keys, and encoded secrets can be indistinguishable, and Base64/whitespace/entropy heuristics cannot provide an all-provider guarantee. A profile setting should not be a single ambiguous "scan thinking" checkbox. If configurability is desired, use an explicit policy such as `preserve`, `scan`, or `block` for known opaque fields, with plaintext thinking controlled separately. `preserve` favors provider continuity, `scan` favors attempted content inspection but can corrupt signatures, and `block` rejects suspected opaque content rather than modifying it. Unknown new provider schemas fail safely only by explicit policy: preserving them risks exfiltration, redacting them risks corruption, and blocking them risks availability. A compatibility default would preserve exact known opaque fields; a high-security default could preserve only provider-issued values and block unknown ones.

Model-side alternatives are probabilistic, not guarantees. Fine-tuning the secret classifier with valid signature/redacted-thinking negatives or adding an opaque-vs-secret classifier could reduce false positives but may reduce secret recall and must be revalidated as providers change. Decoding Base64 before scanning handles encoding only, not encryption, and requires strict size/depth limits; never rewrite the original opaque value. Do not rely on a generic high-entropy/no-space exception for real credentials.

### Provider-issued signature allowlist

For stronger protection against crafted opaque fields, evaluate learning provider-issued values from actual upstream responses and preserving only matching values in later requests. Store bounded hashes rather than raw signatures where practical. Unknown values could be rejected or handled by an explicit bootstrap policy. This can prevent arbitrary forged exemptions but requires careful handling of engine restarts, pre-existing conversation history, streaming responses, and provider session migration. Do not implement this complexity unless the trusted-schema exemption's residual exfiltration risk is unacceptable.

This is different from the existing exact-fragment redaction cache. The current cache stores a processed redaction result after the first scan, so it can reuse a corrupted signature. A provider-issued allowlist would instead record that a complete opaque value arrived from a trusted upstream response, allowing that exact value to bypass the first request-side scan later. Do not reuse the fragment cache as proof of provider origin. Namespace entries by profile/upstream, cache only complete values (including correctly reconstructed streamed values), use bounded LRU expiry, and use a collision-resistant hash rather than the existing `size_t` fragment hash. Never treat a partial streamed signature as trusted.

This approach is stronger than schema-only preservation against a client that crafts a new `signature` or `data` value, and it avoids scanning legitimate provider-generated ciphertext that the PII model cannot understand. It does not solve first-run/restored-history bootstrap, provider migration, or a malicious upstream. Choices for unknown values are schema-preserving (better availability, more exfiltration risk), scanning (corruption risk), or blocking (availability risk). Provider-issued caching is therefore a possible strict mode and defense-in-depth layer, not a complete replacement for a documented bootstrap policy.

### Potentially expanded scanning: bounded Base64 decoding

Evaluate Base64 decoding as a way to increase detection coverage for secrets that users have merely encoded rather than encrypted. This is an expansion of scanning scope, not an encryption bypass. Candidates may be decoded and the resulting text scanned for PII, with strict input/output size limits, shallow recursion limits, time budgets, and binary/text sanity checks. Cap CPU and memory use and never treat Base64 syntax as proof of encryption or safety.

Do not modify a provider-issued opaque field based on decoded content; if the configured policy treats a finding as sensitive, reject the request rather than corrupting the signature. Ordinary user-authored encoded content may be redacted according to the existing output policy after tests establish the correct representation and mappings. Document false-positive risks from arbitrary binary data, nested encoding, compression, and encryption, none of which Base64 decoding alone can interpret.

Anthropic documents `thinking.signature` as an encrypted copy of reasoning that clients pass back unchanged. The PR regression suite exercises Anthropic Messages non-streaming and streaming requests and an OpenAI-compatible streaming path, using deterministic keyword/regex findings to prove byte-exact opaque fields and continued redaction of plaintext PII in sibling and similarly named non-exempt fields. Live direct-Anthropic and OpenRouter provider validation remains a release check rather than something claimed by the local mock tests.

### Optimization priorities from measured impact

Use the stage measurements to prioritize future work rather than optimizing every stage equally:

1. **Finish the Hugging Face tokenizer integration (agreed direction).** It removed approximately 294.89 seconds from the real 100,277-character custom-tokenizer case and improved the evaluation corpus's overall detection metrics. Production work still needs Windows/macOS/Linux packaging, startup-memory review, multilingual/chunk-seam tests, and release validation.
2. **Opaque structural fields are resolved in PR #82; validate before release.** Preserving legitimate signatures avoids both provider corruption and all 37.54 seconds of pre-exemption Hugging Face processing for the real 100,277-character signature. This is larger than any remaining tokenizer or decoder optimization. The accepted residual risk is that exact schema matching does not prove provider origin; provider-observed caching or a configurable strict policy remains optional future work.
3. **Profile and optimize ONNX inference next.** ONNX `Run` consumed 33.25 seconds (about 89% of the Hugging Face request). Measure execution providers, thread budgets, graph/quantization choices, and chunk sizing before changing them; do not assume GPU, more threads, or smaller windows will help. Preserve the existing accuracy and memory gates.
4. **Optimize Viterbi only after the larger costs are addressed.** Viterbi/probability decoding consumed 4.15 seconds (about 11%). The current C++ rebuilds label information and performs string comparisons and map lookups inside the inner transition loop. Precompute the 33x33 transition/start/end score tables, use two dynamic-programming rows plus backpointers, reduce allocations, and consider computing assigned probabilities in the same pass. Benchmark exact label/probability parity and entity metrics before adopting changes.
5. **Do not spend optimization time on negligible stages yet.** Entity assembly took about 6 ms, replacement/JSON serialization was below 1 ms, and local upstream round-trip was below 10 ms. Exact-fragment caching already reduced repeat requests to 40–46 ms. Network time with a real provider is a separate measurement.

### Viterbi implementation alternatives

Hugging Face `tokenizers` has no Viterbi/CRF decoder; it only handles tokenization. OpenAI's official `privacy-filter` repository provides `opf/_core/decoding.py`, a Python/PyTorch `ViterbiCRFDecoder` with precomputed transition tables, CPU decoding, and batched CUDA decoding. It is a useful correctness reference, not a native drop-in replacement and not evidence of faster CPU performance. Differences such as log-probability inputs and start/end scoring must be checked before comparing or adopting it.

Generic alternatives include CRFsuite/python-crfsuite and PyTorch CRF implementations, but they add runtime dependencies and still need our BIOES constraints, six calibration biases, offsets, and confidence semantics. Prefer optimizing the small existing C++ implementation first. If a replacement is ever evaluated, require exact path/probability comparisons on the existing 804-entity corpus and real long-signature cases.

### Preferred direction: Hugging Face tokenizers with a native Rust FFI adapter

Hugging Face `tokenizers` is Apache-2.0-licensed open source. Its reference implementation is Rust, with official Python, Node.js, and WebAssembly bindings; official C/C++ bindings are not yet available. The production engine should link the Rust implementation directly and must not depend on a Python runtime.

Keep the existing C++ `BPETokenizer` interface as the integration boundary and implement it with a small project-owned C ABI around a pinned Hugging Face Rust crate. Expose tokenizer creation/destruction, encoding to token IDs and UTF-8 byte offsets, decoding/token lookup needed by diagnostics, and explicit buffer destruction. Convert between UTF-8 offsets and the detector's UTF-16 character offsets in C++. Build the Rust static library for each required Windows, macOS, and Linux architecture, link it into `agentredactor-core`, pin the Rust toolchain and crate versions with a lockfile, and ship the required license and attribution notices. Start with stable version 0.22.2, which was used for the reference benchmark, rather than tracking the changing development branch.

The dependency audit covers 85 desktop packages including `tokenizers`: all declare permissive license options and supply license/notice files. `third_party/hf_tokenizer/generate_notices.py` reproducibly combines the direct Hugging Face license and all 84 transitive desktop package notices into the already staged `LICENSE.tokenizers.txt`; regenerate it whenever `Cargo.lock` changes.

The Apache-2.0 `mlc-ai/tokenizers-cpp` project is a possible build reference, but its public C++ API currently exposes IDs without token offsets. It is therefore not a drop-in replacement; using it would require extending its API or accessing lower-level bindings. Do not add it merely to avoid a small project-owned adapter without first resolving offset support.

Evaluation and implementation sequence:

1. Build a proof of concept behind the existing tokenizer interface. Evaluate tokenization separately from the explicitly approved schema-qualified opaque-field exemption so tokenizer measurements do not conceal the policy change.
2. Use the Python reference binding as a test oracle for token IDs and offsets across ordinary PII text, multilingual text, Unicode, whitespace/newlines, punctuation, long random Base64, repeated characters, and adversarial unbroken strings.
3. Run the existing PII precision/recall and chunked-inference tests, plus real proxy request/response regression scenarios. Review any detection differences rather than accepting token parity alone as proof of safety.
4. Benchmark initialization, steady-state encoding, memory, and end-to-end proxy latency separately from ONNX inference on all three platforms and both supported architectures. Include repeated content and previously unseen long signatures.
5. Verify static linking, native runtime dependencies, Rust toolchain setup, package contents, licenses, and release builds on Windows, macOS, and Linux before merging. Because this is shared-core functionality, run every affected platform's full validation and coordinate the eventual release.
6. Keep the existing tokenizer available during evaluation so the change can be abandoned or reverted without affecting release builds. Do not publish a release from the proof of concept.

### Fallback: optimize the custom BPE without changing its output

If native Rust integration is rejected or blocked, replace full-vector rescans with linked token pieces and a merge-rank priority queue. Use numeric token-ID pairs, lazy queue invalidation, and deterministic leftmost tie-breaking. This should give approximately O(n log n) merging while preserving the current token IDs, offsets, and detection behaviour. Prove output equivalence against the current implementation and benchmark it against the Hugging Face adapter; no performance ranking between Hugging Face and a hypothetical optimized C++ implementation should be assumed without that native comparison.

Other supporting options include bounded word/tokenization caches, direct whole-word vocabulary lookup, fewer UTF conversions and string copies, separate tokenizer/inference timing, cooperative cancellation, and truncation of oversized sensitive diagnostic log values. These are useful additions but do not fix a unique long-signature cache miss by themselves.

Do not use generic no-space, Base64, or entropy detection to bypass scanning: secrets often have exactly those properties, and encoding is not proof of encryption. The narrowly schema-qualified Anthropic exemption is approved and implemented in PR #82, with its residual crafted-field risk documented above. Arbitrary pre-tokenizer chunking, truncation, sampling, heuristic neural-model prefilters, additional ONNX threads, GPU inference, and smaller inference windows do not safely address the original tokenizer bottleneck; any later inference optimization needs its own measurements and correctness gates.
