# Cache and decoding semantics

Committed Chinese history is produced by the unconditional pass 1. Generation
uses pass 2 with pinyin cross-attention. These are different self-attention
states even for the same token at the same position: cross-attention in an
earlier decoder layer changes the input to later layers.

The last committed token is reused as the first conditional query. Its pass-2
self-attention KV remains visible throughout the current beam search, matching
the Python decoder's `prefix[:-1]` prefill followed by conditional decoding of
`prefix[-1]`. On every exit, including cancellation and model errors, the runtime
restores this boundary to its pass-1 state in every beam lane. The remaining
generation suffix is temporary; it is hidden or overwritten on the next request.
Selected text is committed through `fill`, which runs pass 1. It is never committed
by relabeling a conditional beam cache as history.

The pinyin encoder's projected cross-attention KV is a separate per-request
object. Invalidating that object alone cannot remove conditional information
already written into self-attention KV.

## Sliding history

Eviction retains the BOS attention sink and shifts pre-RoPE K/V for retained
tokens. This is not a simple stale-RoPE rotation bug. However, deep cached states
can still encode information from evicted text. Shifting them is an approximation
and is not generally equivalent to recomputing only the visible retained text.
The runtime currently preserves this policy. Tests or benchmarks requiring exact
fresh-history semantics should reset and prefill the retained text explicitly.

## Beam scores

Phono normalizes logits over each position's legal Chinese-character subset.
Scores accumulate these log probabilities. A baseline that normalizes over the
full vocabulary before masking can rank parent-beam expansions differently.
Preserving a published baseline's scoring rule is appropriate for an artifact
comparison; an architecture ablation should additionally control this rule.

## Verification builds

Set `PHONO_TEST_MODEL_DIR` to a compatible model package to exercise the model
tests; otherwise those tests skip inference. Their static link retains the
registration-only custom operators required to load the PTE methods.

`PHONO_RUNTIME_OUTPUT_DIR` may be overridden at CMake configure time to keep a
verification DLL separate from an existing application or frozen experiment.
Its default remains this repository's `results/`. Use this project's pixi CMake
and CTest environment. On Windows, put the chosen runtime directory on PATH when
running the C ABI test executable.
