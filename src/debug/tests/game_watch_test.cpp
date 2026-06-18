#include "game_watch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

    using mnemos::debug::achievement_rule;
    using mnemos::debug::diagnostic;
    using mnemos::debug::evaluate_predicate;
    using mnemos::debug::frame_event_observation;
    using mnemos::debug::game_watch_pack;
    using mnemos::debug::memory_observation;
    using mnemos::debug::predicate_eval_state;
    using mnemos::debug::register_observation;
    using mnemos::debug::sample_watches;
    using mnemos::debug::score_definition;
    using mnemos::debug::score_reset_policy;
    using mnemos::debug::score_submit_policy;
    using mnemos::debug::validate_game_watch_pack;
    using mnemos::debug::watch_definition;
    using mnemos::debug::watch_endian;
    using mnemos::debug::watch_observation_frame;
    using mnemos::debug::watch_predicate;
    using mnemos::debug::watch_predicate_kind;
    using mnemos::debug::watch_result_type;
    using mnemos::debug::watch_sample;
    using mnemos::debug::watch_sample_point;
    using mnemos::debug::watch_source_kind;
    using mnemos::debug::watch_value_encoding;

    [[nodiscard]] watch_definition memory_watch(std::string_view id, std::string_view view,
                                                const std::uint64_t offset,
                                                const std::uint8_t byte_count,
                                                const watch_endian endian = watch_endian::little) {
        watch_definition watch{};
        watch.id = id;
        watch.source.kind = watch_source_kind::memory_view;
        watch.source.sample_point = watch_sample_point::frame_end;
        watch.source.memory_view = view;
        watch.source.offset = offset;
        watch.source.byte_count = byte_count;
        watch.decoder.endian = endian;
        return watch;
    }

    [[nodiscard]] watch_definition register_watch(std::string_view id, std::string_view chip,
                                                  std::string_view name) {
        watch_definition watch{};
        watch.id = id;
        watch.source.kind = watch_source_kind::register_field;
        watch.source.sample_point = watch_sample_point::frame_end;
        watch.source.chip = chip;
        watch.source.register_name = name;
        return watch;
    }

    [[nodiscard]] watch_definition event_watch(std::string_view id, std::string_view event) {
        watch_definition watch{};
        watch.id = id;
        watch.source.kind = watch_source_kind::frame_event;
        watch.source.sample_point = watch_sample_point::frame_end;
        watch.source.event_name = event;
        return watch;
    }

    [[nodiscard]] const watch_sample* find_sample(std::span<const watch_sample> samples,
                                                  std::string_view id) {
        for (const auto& sample : samples) {
            if (sample.watch_id == id) {
                return &sample;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool has_diagnostic(std::span<const diagnostic> diagnostics,
                                      std::string_view code) {
        for (const auto& diag : diagnostics) {
            if (diag.code == code) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("game watches sample memory registers and frame events", "[game_watch]") {
    const std::array<std::uint8_t, 2> little_score{0x34U, 0x12U};
    const std::array<std::uint8_t, 2> big_bcd{0x12U, 0x34U};
    const std::array<std::uint8_t, 1> signed_flag{0xFEU};
    const std::array<std::uint8_t, 1> scaled_timer{10U};

    std::vector<watch_definition> watches;
    watches.push_back(memory_watch("score_raw", "ram", 0U, 2U));

    watch_definition bcd = memory_watch("score_bcd", "bcd", 0U, 2U, watch_endian::big);
    bcd.decoder.encoding = watch_value_encoding::bcd;
    watches.push_back(bcd);

    watch_definition signed_value = memory_watch("signed_delta", "signed", 0U, 1U);
    signed_value.decoder.encoding = watch_value_encoding::signed_binary;
    watches.push_back(signed_value);

    watch_definition scaled = memory_watch("scaled_timer", "timer", 0U, 1U);
    scaled.decoder.scale_num = 3;
    scaled.decoder.scale_den = 2;
    watches.push_back(scaled);

    watch_definition reg = register_watch("score_nibble", "vdp", "score_hi");
    reg.decoder.mask = 0xF0U;
    reg.decoder.shift = 4U;
    watches.push_back(reg);

    watches.push_back(event_watch("bonus_event", "bonus"));

    watch_observation_frame frame{};
    frame.sample_point = watch_sample_point::frame_end;
    frame.memory = {
        memory_observation{.view = "ram", .bytes = little_score},
        memory_observation{.view = "bcd", .bytes = big_bcd},
        memory_observation{.view = "signed", .bytes = signed_flag},
        memory_observation{.view = "timer", .bytes = scaled_timer},
    };
    frame.registers = {
        register_observation{.chip = "vdp", .name = "score_hi", .value = 0xC0U, .bit_width = 8U},
    };
    frame.events = {
        frame_event_observation{.name = "bonus", .occurred = true},
    };

    const auto samples = sample_watches(watches, frame);

    CHECK(samples.diagnostics.empty());
    REQUIRE(samples.samples.size() == 6U);
    REQUIRE(find_sample(samples.samples, "score_raw") != nullptr);
    CHECK(find_sample(samples.samples, "score_raw")->value == 0x1234);
    REQUIRE(find_sample(samples.samples, "score_bcd") != nullptr);
    CHECK(find_sample(samples.samples, "score_bcd")->value == 1234);
    REQUIRE(find_sample(samples.samples, "signed_delta") != nullptr);
    CHECK(find_sample(samples.samples, "signed_delta")->value == -2);
    REQUIRE(find_sample(samples.samples, "scaled_timer") != nullptr);
    CHECK(find_sample(samples.samples, "scaled_timer")->value == 15);
    REQUIRE(find_sample(samples.samples, "score_nibble") != nullptr);
    CHECK(find_sample(samples.samples, "score_nibble")->value == 12);
    REQUIRE(find_sample(samples.samples, "bonus_event") != nullptr);
    CHECK(find_sample(samples.samples, "bonus_event")->value == 1);
}

TEST_CASE("game watch predicates compare change and latch on watch ids", "[game_watch]") {
    const std::array current{watch_sample{.watch_id = "score", .value = 1000},
                             watch_sample{.watch_id = "flag", .value = 1}};
    const std::array previous{watch_sample{.watch_id = "score", .value = 900},
                              watch_sample{.watch_id = "flag", .value = 1}};

    predicate_eval_state state;
    std::vector<diagnostic> diagnostics;

    CHECK(evaluate_predicate(watch_predicate{.kind = watch_predicate_kind::greater_equal,
                                             .watch_id = "score",
                                             .value = 1000},
                             current, previous, state, diagnostics));
    CHECK(evaluate_predicate(
        watch_predicate{
            .kind = watch_predicate_kind::in_range, .watch_id = "score", .min = 900, .max = 1000},
        current, previous, state, diagnostics));
    CHECK(evaluate_predicate(
        watch_predicate{.kind = watch_predicate_kind::changed, .watch_id = "score"}, current,
        previous, state, diagnostics));

    const watch_predicate all{
        .kind = watch_predicate_kind::all,
        .children = {
            watch_predicate{
                .kind = watch_predicate_kind::greater_than, .watch_id = "score", .value = 999},
            watch_predicate{.kind = watch_predicate_kind::equals, .watch_id = "flag", .value = 1}}};
    CHECK(evaluate_predicate(all, current, previous, state, diagnostics));

    const watch_predicate any{
        .kind = watch_predicate_kind::any,
        .children = {
            watch_predicate{.kind = watch_predicate_kind::equals, .watch_id = "flag", .value = 0},
            watch_predicate{.kind = watch_predicate_kind::equals, .watch_id = "flag", .value = 1}}};
    CHECK(evaluate_predicate(any, current, previous, state, diagnostics));

    const watch_predicate latched{
        .id = "first_bonus",
        .kind = watch_predicate_kind::latched,
        .children = {
            watch_predicate{.kind = watch_predicate_kind::equals, .watch_id = "flag", .value = 1}}};
    CHECK(evaluate_predicate(latched, current, previous, state, diagnostics));

    const std::array after_clear{watch_sample{.watch_id = "score", .value = 1001},
                                 watch_sample{.watch_id = "flag", .value = 0}};
    CHECK(evaluate_predicate(latched, after_clear, current, state, diagnostics));
    CHECK(diagnostics.empty());
}

TEST_CASE("game watch validation diagnoses malformed definitions", "[game_watch]") {
    game_watch_pack pack{};
    pack.id = "bad_pack";

    watch_definition bad_watch = memory_watch("bad_watch", "", 0U, 0U);
    bad_watch.source.sample_point = watch_sample_point::unspecified;
    bad_watch.decoder.scale_den = 0;
    pack.watches.push_back(bad_watch);

    pack.scores.push_back(score_definition{.id = "score_1", .watch_id = "missing_score"});
    pack.achievements.push_back(achievement_rule{
        .id = "ach_1",
        .predicate =
            watch_predicate{.kind = watch_predicate_kind::equals, .watch_id = "0xC000", .value = 1},
        .watch_ids = {"missing_watch"}});

    const auto diagnostics = validate_game_watch_pack(pack);

    CHECK(has_diagnostic(diagnostics, "game_watch.sample_point.unspecified"));
    CHECK(has_diagnostic(diagnostics, "game_watch.memory.view_empty"));
    CHECK(has_diagnostic(diagnostics, "game_watch.memory.byte_count"));
    CHECK(has_diagnostic(diagnostics, "game_watch.decoder.scale_den"));
    CHECK(has_diagnostic(diagnostics, "game_watch.score.watch_missing"));
    CHECK(has_diagnostic(diagnostics, "game_watch.achievement.watch_missing"));
    CHECK(has_diagnostic(diagnostics, "game_watch.predicate.raw_address"));
}

TEST_CASE("game watch validation accepts score and achievement packs by watch id", "[game_watch]") {
    game_watch_pack pack{};
    pack.id = "valid_pack";

    watch_definition score = memory_watch("p1_score", "work_ram", 0x10U, 3U);
    score.decoder.encoding = watch_value_encoding::bcd;
    score.result_type = watch_result_type::score;
    pack.watches.push_back(score);

    pack.scores.push_back(score_definition{.id = "p1_scoreboard",
                                           .watch_id = "p1_score",
                                           .player_slot = 1U,
                                           .reset_policy = score_reset_policy::on_game_reset,
                                           .submit_policy = score_submit_policy::on_game_over});

    pack.achievements.push_back(
        achievement_rule{.id = "score_1000",
                         .title = "Score 1000",
                         .predicate = watch_predicate{.kind = watch_predicate_kind::greater_equal,
                                                      .watch_id = "p1_score",
                                                      .value = 1000},
                         .watch_ids = {"p1_score"},
                         .progress_watch_ids = {"p1_score"}});

    CHECK(validate_game_watch_pack(pack).empty());
}
