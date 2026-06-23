#pragma once

// Data-only watch model for high scores and achievements.
//
// Game-specific packs describe what to observe through stable watch ids; they
// do not reach into emulator internals or frontend state directly.

#include "capability_discovery.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::debug {

    enum class watch_sample_point : std::uint8_t {
        unspecified,
        frame_end,
        scanline,
        instruction,
        named_event
    };

    enum class watch_source_kind : std::uint8_t { memory_view, register_field, frame_event };

    enum class watch_value_encoding : std::uint8_t { unsigned_binary, signed_binary, bcd };

    enum class watch_endian : std::uint8_t { little, big };

    enum class watch_result_type : std::uint8_t {
        counter,
        score,
        flag,
        timer,
        enumeration,
        opaque
    };

    enum class watch_predicate_kind : std::uint8_t {
        always,
        equals,
        not_equals,
        greater_than,
        greater_equal,
        less_than,
        less_equal,
        in_range,
        changed,
        latched,
        all,
        any
    };

    enum class score_reset_policy : std::uint8_t {
        never,
        on_power_on,
        on_game_reset,
        on_life_lost
    };

    enum class score_submit_policy : std::uint8_t { manual, on_game_over, on_threshold };

    enum class achievement_persistence : std::uint8_t { session, save_state, profile };

    struct watch_source {
        watch_source_kind kind{watch_source_kind::memory_view};
        watch_sample_point sample_point{watch_sample_point::unspecified};
        std::string memory_view{};
        std::uint64_t offset{0U};
        std::uint8_t byte_count{0U};
        std::string chip{};
        std::string register_name{};
        std::string event_name{};
    };

    struct watch_decoder {
        watch_value_encoding encoding{watch_value_encoding::unsigned_binary};
        watch_endian endian{watch_endian::little};
        std::uint64_t mask{UINT64_MAX};
        std::uint8_t shift{0U};
        std::int64_t scale_num{1};
        std::int64_t scale_den{1};
        bool clamp_min_enabled{false};
        std::int64_t clamp_min{0};
        bool clamp_max_enabled{false};
        std::int64_t clamp_max{0};
    };

    struct watch_definition {
        std::string id{};
        watch_source source{};
        watch_decoder decoder{};
        watch_result_type result_type{watch_result_type::opaque};
    };

    struct score_definition {
        std::string id{};
        std::string watch_id{};
        std::uint8_t player_slot{0U};
        score_reset_policy reset_policy{score_reset_policy::on_power_on};
        score_submit_policy submit_policy{score_submit_policy::manual};
    };

    struct watch_predicate {
        std::string id{};
        watch_predicate_kind kind{watch_predicate_kind::always};
        std::string watch_id{};
        std::int64_t value{0};
        std::int64_t min{0};
        std::int64_t max{0};
        std::vector<watch_predicate> children{};
    };

    struct achievement_rule {
        std::string id{};
        std::string title{};
        watch_predicate predicate{};
        std::vector<std::string> watch_ids{};
        std::vector<std::string> progress_watch_ids{};
        achievement_persistence persistence{achievement_persistence::session};
    };

    struct game_watch_pack {
        std::string id{};
        std::vector<watch_definition> watches{};
        std::vector<score_definition> scores{};
        std::vector<achievement_rule> achievements{};
    };

    struct memory_observation {
        std::string view{};
        std::span<const std::uint8_t> bytes{};
    };

    struct register_observation {
        std::string chip{};
        std::string name{};
        std::uint64_t value{0U};
        std::uint8_t bit_width{64U};
    };

    struct frame_event_observation {
        std::string name{};
        bool occurred{false};
    };

    struct watch_observation_frame {
        watch_sample_point sample_point{watch_sample_point::unspecified};
        std::string event_name{};
        std::vector<memory_observation> memory{};
        std::vector<register_observation> registers{};
        std::vector<frame_event_observation> events{};
    };

    struct watch_sample {
        std::string watch_id{};
        std::int64_t value{0};
    };

    struct watch_sample_set {
        std::vector<watch_sample> samples{};
        std::vector<diagnostic> diagnostics{};
    };

    struct predicate_eval_state {
        std::vector<std::string> latched_predicates{};
    };

    [[nodiscard]] std::vector<diagnostic> validate_game_watch_pack(const game_watch_pack& pack);

    [[nodiscard]] std::optional<watch_sample> sample_watch(const watch_definition& watch,
                                                           const watch_observation_frame& frame,
                                                           std::vector<diagnostic>& diagnostics);

    [[nodiscard]] watch_sample_set sample_watches(std::span<const watch_definition> watches,
                                                  const watch_observation_frame& frame);

    [[nodiscard]] bool evaluate_predicate(const watch_predicate& predicate,
                                          std::span<const watch_sample> current,
                                          std::span<const watch_sample> previous,
                                          predicate_eval_state& state,
                                          std::vector<diagnostic>& diagnostics);

} // namespace mnemos::debug
