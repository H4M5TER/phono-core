// Model-based session tests. These exercise the real InferenceSession against
// a v2 model package. They require the PHONO_TEST_MODEL_DIR environment
// variable; when it is unset the suite is skipped so CTest still passes on
// machines without a downloaded model.
//
// Covered here:
//   * stateless session driving a context slot through fill + generate
//   * cancellation via a callback + generation-cursor rollback
//   * max_pinyin_length enforcement
//   * max_history_length windowing (eviction keeps BOS as the attention sink)

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "context/context.hpp"
#include "core/config.hpp"
#include "core/tokenizer.hpp"
#include "engine/inference_engine.hpp"

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct CancellationTrigger {
    int max_steps = 0;   // allow this many generation steps, then cancel
    int steps = 0;
};

bool cancel_after_n_steps(void* user_data) {
    auto* trigger = static_cast<CancellationTrigger*>(user_data);
    return trigger->steps++ >= trigger->max_steps;
}

const char* env_or_null(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

std::vector<int32_t> pinyin_ids(
    const phono::core::Tokenizer& tokenizer,
    std::initializer_list<const char*> tokens) {
    std::vector<int32_t> output;
    for (const char* token : tokens) {
        const auto id = tokenizer.find_pinyin_id_exact(token);
        if (!id) throw std::runtime_error(std::string("missing pinyin token: ") + token);
        output.push_back(*id);
    }
    return output;
}

void test_explicit_syllables_without_segmenter(const std::string& package_root) {
    namespace fs = std::filesystem;
    struct TemporaryPackage {
        fs::path directory = fs::temp_directory_path() /
            ("phono_no_segmenter_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryPackage() {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }
    } package;
    fs::create_directories(package.directory);
    nlohmann::json config;
    std::ifstream(fs::path(package_root) / "config.json") >> config;
    const auto original = phono::core::ModelPackageConfig::load(fs::absolute(package_root).string());
    config["runtime"]["pre_model_path"] = original.resolve(original.runtime.pre_model_path);
    config["runtime"]["post_model_path"] = original.resolve(original.runtime.post_model_path);
    config["vocabs"]["chinese_vocab"] = original.resolve(original.vocabs.chinese_vocab);
    config["vocabs"]["context_vocab"] = original.resolve(original.vocabs.context_vocab);
    config["vocabs"]["pinyin_vocab"] = original.resolve(original.vocabs.pinyin_vocab);
    check(config.contains("segmenter"), "test package must advertise its segmenter");
    config["segmenter"]["model_path"] = "deliberately_missing_segmenter.pte";
    config["segmenter"]["char_vocab"] = "deliberately_missing_char_vocab.txt";
    std::ofstream(package.directory / "config.json") << config;

    {
        phono::core::EngineConfig explicit_syllables;
        explicit_syllables.tokenizer.use_segmenter = false;
        explicit_syllables.tokenizer.max_pinyin_chars =
            original.segmenter->max_input_chars + 1;
        phono::engine::InferenceEngine engine(package.directory.string(), explicit_syllables);
        check(!engine.smart_segmenter_available(),
              "disabled segmentation must not load the missing scorer assets");
        check(engine.diagnostics().empty(),
              "intentionally disabled segmenter must not report missing-scorer fallback");
        const auto core = phono::core::default_core_config(engine.config());
        phono::context::ContextManager manager(engine.config(), 1, core);
        phono::engine::InferenceSession session(engine, core);
        const auto ids = pinyin_ids(engine.tokenizer(), {"ni", "hao"});
        const auto result = session.generate(manager.get_context_by_id(0), ids);
        check(result.ok() && !result.beams.empty() && result.beams.front().decoded == "你好",
              "exact syllable generation must work without segmenter assets");
    }
    bool missing_assets_rejected = false;
    try {
        phono::engine::InferenceEngine enabled(package.directory.string());
    } catch (const std::exception&) {
        missing_assets_rejected = true;
    }
    check(missing_assets_rejected,
          "default enabled policy must still require advertised segmenter assets");
}

void run(const std::string& package_root) {
    phono::engine::InferenceEngine engine(package_root);
    const auto& cfg = engine.config();
    const auto& tokenizer = engine.tokenizer();

    check(engine.smart_segmenter_available(), "v2.2 test package should load its gap scorer");
    const auto segmented = engine.segment_pinyin("nihaoma");
    check(segmented.ok() && segmented.segments ==
              std::vector<std::string>({"ni", "hao", "ma"}),
          "smart segmenter should decode nihaoma as ni hao ma");
    const auto short_input = engine.segment_pinyin("pb");
    check(short_input.ok() && short_input.strategy == "checked_fmm" &&
              short_input.segments == std::vector<std::string>({"p", "b"}),
          "two-character input should bypass the scorer and use checked FMM");
    const auto normalized = engine.segment_pinyin("jvan");
    check(normalized.ok() && normalized.normalized_input == "juan" &&
              normalized.segments == std::vector<std::string>({"juan"}),
          "j/q/x/y + v normalization should produce a legal token");
    const auto invalid = engine.segment_pinyin("pv");
    check(invalid.ok() && invalid.normalized_input == "p" &&
              invalid.invalid_ranges.size() == 1 &&
              invalid.invalid_ranges.front().begin == 1 &&
              invalid.invalid_ranges.front().end == 2,
          "safe mode should delete and locate unavoidable invalid characters");
    check(engine.segment_pinyin("iiiiii").error ==
              phono::engine::InferenceError::InvalidPinyin,
          "safe mode should reject a result that becomes empty");

    // A core_config that is valid for any v2 model (pre_max >= 128, post_max
    // >= 32, batch 3) and small enough to exercise windowing quickly.
    phono::core::CoreConfig core;
    core.beam_size = cfg.runtime.batch_size;
    core.max_context_length = cfg.pre_model.max_seqlen - 1;
    core.max_pinyin_length = std::min<int32_t>(4, cfg.post_model.max_seqlen);
    core.max_history_length =
        std::max<int32_t>(1, core.max_context_length - core.max_pinyin_length - 1);
    core.slack_interval = std::max<int32_t>(1, core.max_history_length / 4);

    phono::context::ContextManager manager(cfg, 1, core);
    phono::context::Context* slot = &manager.get_context_by_id(0);
    phono::engine::InferenceSession session(engine, core);
    check(session.beam_size() == core.beam_size,
          "a new session should report its configured beam width before first use");

    // --- stateless fill + generate ---
    const std::vector<int32_t> pinyin = pinyin_ids(tokenizer, {"ni", "hao"});
    check(!pinyin.empty(), "ni hao should encode to pinyin ids");
    check(slot->context_ids_len() == 0, "a fresh slot must start empty");
    phono::engine::GenerateResult first = session.generate(*slot, pinyin);
    check(first.ok(), "first generate should succeed");
    check(!first.beams.empty(), "first generate should return beams");
    check(static_cast<int32_t>(first.beams.size()) == session.beam_size(),
          "beam count should match the configured beam width");
    check(first.beams[0].decoded == "你好", "expected 'ni hao' to decode to 你好");

    // Sparse syllables may expose fewer candidates than the fixed pass-2 batch.
    const auto shei = pinyin_ids(tokenizer, {"shei"});
    const auto sparse = session.generate(*slot, shei);
    check(sparse.ok(), "a non-empty sparse candidate set should succeed");
    check(sparse.beams.size() == 1 && sparse.beams[0].decoded == "谁",
          "shei should return its sole finite candidate");

    const auto shei_me = pinyin_ids(tokenizer, {"shei", "me"});
    const auto expanded = session.generate(*slot, shei_me);
    check(expanded.ok(), "a sparse first step should continue recursively");
    check(static_cast<int32_t>(expanded.beams.size()) == session.beam_size(),
          "later steps should be able to refill inactive beam lanes");
    for (const auto& beam : expanded.beams) {
        check(beam.decoded.rfind("谁", 0) == 0,
              "expanded candidates must preserve the sole valid first token");
    }

    // Reset only invalidates the cursors. Dirty KV data from the previous run
    // must be safely overwritten or masked and produce the same result.
    check(session.reset(*slot) == phono::engine::InferenceError::Ok,
          "reset after generation should succeed");
    first = session.generate(*slot, pinyin);
    check(first.ok() && first.beams[0].decoded == "你好",
          "generation after a dirty-cache reset should remain deterministic");

    const std::vector<int32_t> committed =
        tokenizer.encode_context(first.beams[0].decoded);
    check(session.fill(*slot, committed) == phono::engine::InferenceError::Ok,
          "filling the committed candidate should succeed");
    check(slot->context_ids_len() == static_cast<int32_t>(committed.size()),
          "committed ids should be stored in the context slot");
    check(slot->history_seqlen() == 1 + slot->context_ids_len(),
          "history_seqlen should count BOS + committed ids");

    // Independent two-step reference: retain the last prefix token's pass-2
    // state, as in the Python decoder, instead of restoring it between steps.
    const auto clean = slot->self_kv.snapshot_batch_slice(0, 0, slot->history_seqlen());
    auto reference_cache = phono::context::make_zero_persistent_tensor(
        {cfg.pre_model.mhsa_layers, 2, core.beam_size, cfg.pre_model.max_seqlen,
         cfg.pre_model.mhsa_heads, cfg.pre_model.self_head_dim()});
    for (int b = 0; b < core.beam_size; ++b)
        reference_cache.restore_batch_slice(b, 0, slot->history_seqlen(), clean);
    phono::engine::PostModelOutput post;
    phono::engine::CrossKvOutput cross;
    check(engine.run_post_model(pinyin, post) == phono::engine::InferenceError::Ok,
          "reference pinyin encoding failed");
    check(engine.run_pre_cross_kv(post, cross) == phono::engine::InferenceError::Ok,
          "reference cross projection failed");
    phono::engine::DecoderModelOutput decoder;
    check(engine.run_pre_pass2(std::vector<int32_t>(core.beam_size, committed.back()),
              reference_cache, std::vector<int32_t>(core.beam_size, slot->history_seqlen() - 1),
              post, cross, 0, decoder) == phono::engine::InferenceError::Ok,
          "reference first step failed");
    const auto log_probs = [](const auto& values, size_t offset, int size) {
        const double maximum = *std::max_element(values.begin() + offset, values.begin() + offset + size);
        double sum = 0;
        for (int i = 0; i < size; ++i) sum += std::exp(values[offset + i] - maximum);
        std::vector<double> result(size);
        for (int i = 0; i < size; ++i) result[i] = values[offset + i] - maximum - std::log(sum);
        return result;
    };
    auto first_lp = log_probs(decoder.logits, 0, decoder.projection_size);
    std::vector<int> order(first_lp.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return first_lp[a] > first_lp[b]; });
    std::vector<int32_t> inputs(core.beam_size), first_ids(core.beam_size);
    for (int b = 0; b < core.beam_size; ++b) {
        first_ids[b] = decoder.candidate_ids[order[b]];
        inputs[b] = tokenizer.chinese_id_to_context_id(first_ids[b]);
    }
    check(engine.run_pre_pass2(inputs, reference_cache,
              std::vector<int32_t>(core.beam_size, slot->history_seqlen()),
              post, cross, 1, decoder) == phono::engine::InferenceError::Ok,
          "reference second step failed");
    struct Expected { double score; std::vector<int32_t> ids; };
    std::vector<Expected> expected;
    for (int b = 0; b < core.beam_size; ++b) {
        auto lp = log_probs(decoder.logits, b * decoder.projection_size, decoder.projection_size);
        for (int i = 0; i < decoder.projection_size; ++i)
            expected.push_back({first_lp[order[b]] + lp[i], {first_ids[b], decoder.candidate_ids[i]}});
    }
    std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    const auto aligned = session.generate(*slot, pinyin);
    check(aligned.ok(), "contextual reference comparison failed");
    for (size_t b = 0; b < aligned.beams.size(); ++b) {
        check(aligned.beams[b].pred_ids == expected[b].ids, "conditional cache candidate mismatch");
        check(std::abs(aligned.beams[b].score - expected[b].score) < 1e-5,
              "conditional cache score mismatch");
    }
    for (int cancel_poll : {-1, 1, 3}) {
        CancellationTrigger boundary_trigger{cancel_poll, 0};
        const auto result = session.generate(*slot, pinyin, {},
            cancel_poll < 0 ? nullptr : cancel_after_n_steps, &boundary_trigger);
        check(cancel_poll < 0 ? result.ok() : result.error == phono::engine::InferenceError::Cancelled,
              "boundary cancellation status mismatch");
        for (int b = 0; b < core.beam_size; ++b)
            check(slot->self_kv.snapshot_batch_slice(b, 0, slot->history_seqlen()) == clean,
                  "generation must restore all committed causal history");
    }
    // Committing a non-leading candidate after cancellation must agree with a
    // fresh unconditional prefill of the same selected text.
    const auto selected = tokenizer.encode_context(aligned.beams.back().decoded);
    check(session.fill(*slot, selected) == phono::engine::InferenceError::Ok,
          "commit after conditional generation failed");
    phono::context::ContextManager fresh_manager(cfg, 1, core);
    auto& fresh_slot = fresh_manager.get_context_by_id(0);
    std::vector<int32_t> combined = committed;
    combined.insert(combined.end(), selected.begin(), selected.end());
    check(session.fill(fresh_slot, combined) == phono::engine::InferenceError::Ok,
          "fresh reference fill failed");
    const auto continued = session.generate(*slot, pinyin);
    const auto fresh = session.generate(fresh_slot, pinyin);
    check(continued.ok() && fresh.ok() && continued.beams.size() == fresh.beams.size(),
          "continuation comparison failed");
    for (size_t b = 0; b < fresh.beams.size(); ++b) {
        check(continued.beams[b].pred_ids == fresh.beams[b].pred_ids,
              "committed candidate changed subsequent predictions");
        check(std::abs(continued.beams[b].score - fresh.beams[b].score) < 1e-3,
              "committed candidate retained conditional cache state");
    }

    // A second window with explicit full context: the committed history is
    // reused and only the new suffix is generated against.
    const std::vector<int32_t> context2 = tokenizer.encode_context("你好世界");
    const std::vector<int32_t> pinyin2 = pinyin_ids(tokenizer, {"shi", "jie"});
    phono::engine::GenerateResult second =
        session.generate(*slot, pinyin2, context2);
    check(second.ok(), "generate with explicit context should succeed");
    check(second.beams[0].decoded == "世界",
          "expected 'shi jie' after 你好 to decode to 世界");

    // --- cancellation rolls back the generation cursor ---
    const int32_t committed_history_seqlen = slot->history_seqlen();
    const std::vector<int32_t> long_pinyin =
        pinyin_ids(tokenizer, {"ni", "hao", "shi", "jie"});
    CancellationTrigger trigger;
    trigger.max_steps = 0;  // cancel on the very first poll (before any token)
    phono::engine::GenerateResult cancelled =
        session.generate(*slot, long_pinyin, {}, cancel_after_n_steps, &trigger);
    check(cancelled.error == phono::engine::InferenceError::Cancelled,
          "cancellation should surface as Cancelled");
    check(cancelled.current_seqlen == cancelled.history_seqlen,
          "cancelled generate must roll back current_seqlen to the committed history");
    check(slot->current_seqlen() == slot->history_seqlen(),
          "context cursors must be rolled back in the slot");
    check(slot->history_seqlen() == committed_history_seqlen,
          "history_seqlen must be unchanged by the cancelled generate");

    // Cancelling mid-generation also rolls the cursor back.
    trigger.steps = 0;
    trigger.max_steps = 1;  // cancel after the first expansion step
    cancelled = session.generate(*slot, long_pinyin, {}, cancel_after_n_steps, &trigger);
    check(cancelled.error == phono::engine::InferenceError::Cancelled,
          "mid-generation cancellation should surface as Cancelled");
    check(cancelled.current_seqlen == cancelled.history_seqlen,
          "mid-generation cancellation must roll back current_seqlen");
    check(slot->current_seqlen() == slot->history_seqlen(),
          "slot cursors must be rolled back after mid-generation cancellation");
    check(slot->history_seqlen() == committed_history_seqlen,
          "history_seqlen must be unchanged by mid-generation cancellation");

    // A retry after cancellation still works from the committed history.
    phono::engine::GenerateResult retry = session.generate(*slot, pinyin);
    check(retry.ok(), "generate after cancellation should succeed");
    check(!retry.beams.empty(), "retry should return beams");
    check(retry.current_seqlen >= retry.history_seqlen,
          "retry cursors should advance consistently from the committed history");

    // --- max_pinyin_length enforcement ---
    const std::vector<int32_t> too_long(core.max_pinyin_length + 1, pinyin.front());
    phono::engine::GenerateResult limited = session.generate(*slot, too_long);
    check(limited.error == phono::engine::InferenceError::PinyinLimitExceeded,
          "pinyin window beyond max_pinyin_length must be rejected");

    // --- max_history_length windowing ---
    session.reset(*slot);
    const int32_t target = core.max_history_length - core.slack_interval;
    std::vector<int32_t> ids;
    for (int32_t i = 0; i < core.max_history_length + 5; ++i) {
        ids.push_back(tokenizer.encode_context("你").front());
    }
    check(session.fill(*slot, ids) == phono::engine::InferenceError::Ok,
          "oversized fill should be windowed instead of failing");
    check(slot->context_ids_len() <= core.max_history_length,
          "fill must keep history within max_history_length");
    check(slot->context_ids_len() >= target,
          "fill windowing must keep at least max_history_length - slack_interval ids");
    check(slot->history_seqlen() == 1 + slot->context_ids_len(),
          "windowed history seqlen must count BOS + retained ids");
}

}  // namespace

int main() {
    const char* model_dir = env_or_null("PHONO_TEST_MODEL_DIR");
    if (model_dir == nullptr) {
        std::printf("engine_tests: PHONO_TEST_MODEL_DIR unset, skipping model-based tests\n");
        return 0;
    }
    try {
        test_explicit_syllables_without_segmenter(model_dir);
        run(model_dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "engine_tests: FAILED: %s\n", e.what());
        return 1;
    }
    std::printf("engine_tests: all model-based tests passed\n");
    return 0;
}
