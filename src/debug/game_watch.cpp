#include "game_watch.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <utility>

namespace mnemos::debug {
    namespace {

        [[nodiscard]] diagnostic
        watch_diagnostic(std::string capability_id, std::string code, std::string detail,
                         const diagnostic_severity severity = diagnostic_severity::error) {
            return make_diagnostic(code, severity, std::move(capability_id), std::move(code),
                                   std::move(detail));
        }

        [[nodiscard]] bool is_sample_point_match(const watch_source& source,
                                                 const watch_observation_frame& frame) noexcept {
            if (source.sample_point == watch_sample_point::unspecified ||
                frame.sample_point == watch_sample_point::unspecified) {
                return false;
            }
            if (source.sample_point != frame.sample_point) {
                return false;
            }
            if (source.sample_point == watch_sample_point::named_event &&
                !source.event_name.empty() && !frame.event_name.empty()) {
                return source.event_name == frame.event_name;
            }
            return true;
        }

        [[nodiscard]] std::uint64_t low_mask(const std::uint8_t bits) noexcept {
            if (bits == 0U) {
                return 0U;
            }
            if (bits >= 64U) {
                return UINT64_MAX;
            }
            return (UINT64_C(1) << bits) - 1U;
        }

        [[nodiscard]] std::uint8_t significant_bits(std::uint64_t value) noexcept {
            std::uint8_t bits = 0U;
            while (value != 0U) {
                ++bits;
                value >>= 1U;
            }
            return bits;
        }

        [[nodiscard]] std::uint8_t clamped_source_bits(const std::uint8_t bits) noexcept {
            if (bits == 0U || bits > 64U) {
                return 64U;
            }
            return bits;
        }

        [[nodiscard]] std::uint8_t effective_bits(const watch_decoder& decoder,
                                                  const std::uint8_t source_bits) noexcept {
            if (decoder.shift >= 64U) {
                return 0U;
            }
            const std::uint64_t scoped_mask = decoder.mask & low_mask(source_bits);
            return significant_bits(scoped_mask >> decoder.shift);
        }

        [[nodiscard]] std::int64_t sign_extend(const std::uint64_t value,
                                               const std::uint8_t bits) noexcept {
            if (bits == 0U) {
                return 0;
            }
            if (bits >= 64U) {
                return std::bit_cast<std::int64_t>(value);
            }

            const std::uint64_t mask = low_mask(bits);
            const std::uint64_t sign_bit = UINT64_C(1) << (bits - 1U);
            const std::uint64_t scoped = value & mask;
            if ((scoped & sign_bit) == 0U) {
                return static_cast<std::int64_t>(scoped);
            }
            return std::bit_cast<std::int64_t>(scoped | ~mask);
        }

        [[nodiscard]] std::uint64_t magnitude(const std::int64_t value) noexcept {
            if (value >= 0) {
                return static_cast<std::uint64_t>(value);
            }
            return static_cast<std::uint64_t>(-(value + 1)) + 1U;
        }

        [[nodiscard]] bool multiply_overflows(const std::int64_t lhs,
                                              const std::int64_t rhs) noexcept {
            if (lhs == 0 || rhs == 0) {
                return false;
            }
            if ((lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) ||
                (rhs == std::numeric_limits<std::int64_t>::min() && lhs == -1)) {
                return true;
            }

            const bool negative = (lhs < 0) != (rhs < 0);
            const std::uint64_t limit =
                negative ? magnitude(std::numeric_limits<std::int64_t>::min())
                         : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            return magnitude(lhs) > limit / magnitude(rhs);
        }

        [[nodiscard]] std::optional<std::int64_t>
        apply_scale_and_clamp(const watch_definition& watch, std::int64_t value,
                              std::vector<diagnostic>& diagnostics) {
            if (watch.decoder.scale_den == 0) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.decoder.scale_den",
                                     "watch decoder scale denominator must not be zero"));
                return std::nullopt;
            }
            if (multiply_overflows(value, watch.decoder.scale_num)) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.decoder.scale_overflow",
                                     "watch decoder scale multiplication exceeded int64 range"));
                return std::nullopt;
            }

            const std::int64_t product = value * watch.decoder.scale_num;
            if (product == std::numeric_limits<std::int64_t>::min() &&
                watch.decoder.scale_den == -1) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.decoder.scale_overflow",
                                     "watch decoder scale division exceeded int64 range"));
                return std::nullopt;
            }

            value = product / watch.decoder.scale_den;
            if (watch.decoder.clamp_min_enabled && value < watch.decoder.clamp_min) {
                value = watch.decoder.clamp_min;
            }
            if (watch.decoder.clamp_max_enabled && value > watch.decoder.clamp_max) {
                value = watch.decoder.clamp_max;
            }
            return value;
        }

        [[nodiscard]] std::optional<std::int64_t> decode_bcd(const watch_definition& watch,
                                                             const std::uint64_t value,
                                                             const std::uint8_t bits,
                                                             std::vector<diagnostic>& diagnostics) {
            if (bits == 0U) {
                return 0;
            }

            std::int64_t decoded = 0;
            const std::uint8_t nibble_count = static_cast<std::uint8_t>((bits + 3U) / 4U);
            for (std::uint8_t index = nibble_count; index > 0U; --index) {
                const std::uint8_t shift = static_cast<std::uint8_t>((index - 1U) * 4U);
                const std::uint8_t digit =
                    static_cast<std::uint8_t>((value >> shift) & UINT64_C(0x0F));
                if (digit > 9U) {
                    diagnostics.push_back(watch_diagnostic(
                        watch.id, "game_watch.decoder.invalid_bcd",
                        "watch decoder encountered a BCD digit greater than nine"));
                    return std::nullopt;
                }
                if (decoded > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.decoder.value_overflow",
                                         "watch decoder BCD value exceeded int64 range"));
                    return std::nullopt;
                }
                decoded = (decoded * 10) + digit;
            }
            return decoded;
        }

        [[nodiscard]] std::optional<std::int64_t>
        decode_value(const watch_definition& watch, const std::uint64_t raw,
                     const std::uint8_t source_bits, std::vector<diagnostic>& diagnostics) {
            if (watch.decoder.shift >= 64U) {
                diagnostics.push_back(watch_diagnostic(watch.id, "game_watch.decoder.shift",
                                                       "watch decoder shift must be less than 64"));
                return std::nullopt;
            }

            const std::uint8_t bits = clamped_source_bits(source_bits);
            const std::uint64_t scoped_mask = watch.decoder.mask & low_mask(bits);
            const std::uint64_t filtered = (raw & scoped_mask) >> watch.decoder.shift;
            const std::uint8_t filtered_bits = effective_bits(watch.decoder, bits);

            std::optional<std::int64_t> decoded;
            switch (watch.decoder.encoding) {
            case watch_value_encoding::unsigned_binary:
                if (filtered >
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.decoder.value_overflow",
                                         "watch decoder unsigned value exceeded int64 range"));
                    return std::nullopt;
                }
                decoded = static_cast<std::int64_t>(filtered);
                break;
            case watch_value_encoding::signed_binary:
                decoded = sign_extend(filtered, filtered_bits);
                break;
            case watch_value_encoding::bcd:
                decoded = decode_bcd(watch, filtered, filtered_bits, diagnostics);
                break;
            }

            if (!decoded.has_value()) {
                return std::nullopt;
            }
            return apply_scale_and_clamp(watch, *decoded, diagnostics);
        }

        [[nodiscard]] std::optional<std::uint64_t>
        read_memory_value(const watch_definition& watch, const watch_observation_frame& frame,
                          std::vector<diagnostic>& diagnostics) {
            if (watch.source.byte_count == 0U || watch.source.byte_count > 8U) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.memory.byte_count",
                                     "memory watch byte_count must be between one and eight"));
                return std::nullopt;
            }

            const auto found = std::find_if(frame.memory.begin(), frame.memory.end(),
                                            [&watch](const memory_observation& observation) {
                                                return observation.view == watch.source.memory_view;
                                            });
            if (found == frame.memory.end()) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.memory.view_missing",
                                     "observation frame did not include memory view '" +
                                         watch.source.memory_view + "'"));
                return std::nullopt;
            }

            const auto offset = static_cast<std::size_t>(watch.source.offset);
            const std::size_t count = watch.source.byte_count;
            if (watch.source.offset > static_cast<std::uint64_t>(found->bytes.size()) ||
                count > found->bytes.size() - offset) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.memory.range",
                                     "memory watch range exceeded the observed view"));
                return std::nullopt;
            }

            std::uint64_t value = 0U;
            if (watch.decoder.endian == watch_endian::little) {
                for (std::size_t index = 0U; index < count; ++index) {
                    value |= static_cast<std::uint64_t>(found->bytes[offset + index])
                             << (index * 8U);
                }
            } else {
                for (std::size_t index = 0U; index < count; ++index) {
                    value <<= 8U;
                    value |= found->bytes[offset + index];
                }
            }
            return value;
        }

        [[nodiscard]] std::optional<register_observation>
        read_register_value(const watch_definition& watch, const watch_observation_frame& frame,
                            std::vector<diagnostic>& diagnostics) {
            const auto found =
                std::find_if(frame.registers.begin(), frame.registers.end(),
                             [&watch](const register_observation& observation) {
                                 return observation.chip == watch.source.chip &&
                                        observation.name == watch.source.register_name;
                             });
            if (found == frame.registers.end()) {
                diagnostics.push_back(watch_diagnostic(
                    watch.id, "game_watch.register.missing",
                    "observation frame did not include register '" + watch.source.chip + "." +
                        watch.source.register_name + "'"));
                return std::nullopt;
            }
            return *found;
        }

        [[nodiscard]] bool read_frame_event(const watch_definition& watch,
                                            const watch_observation_frame& frame) {
            const auto found = std::find_if(frame.events.begin(), frame.events.end(),
                                            [&watch](const frame_event_observation& observation) {
                                                return observation.name == watch.source.event_name;
                                            });
            return found != frame.events.end() && found->occurred;
        }

        [[nodiscard]] const watch_sample* find_sample(std::span<const watch_sample> samples,
                                                      std::string_view watch_id) noexcept {
            const auto found =
                std::find_if(samples.begin(), samples.end(), [watch_id](const watch_sample& item) {
                    return item.watch_id == watch_id;
                });
            return found != samples.end() ? &*found : nullptr;
        }

        [[nodiscard]] bool contains_string(std::span<const std::string> values,
                                           std::string_view value) noexcept {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        [[nodiscard]] bool contains_watch_id(std::span<const watch_definition> watches,
                                             std::string_view watch_id) noexcept {
            return std::find_if(watches.begin(), watches.end(),
                                [watch_id](const watch_definition& watch) {
                                    return watch.id == watch_id;
                                }) != watches.end();
        }

        [[nodiscard]] const watch_definition* find_watch(std::span<const watch_definition> watches,
                                                         std::string_view watch_id) noexcept {
            const auto found = std::find_if(
                watches.begin(), watches.end(),
                [watch_id](const watch_definition& watch) { return watch.id == watch_id; });
            return found != watches.end() ? &*found : nullptr;
        }

        [[nodiscard]] bool looks_like_raw_address(std::string_view value) noexcept {
            if (value.size() > 2U && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                return true;
            }
            return !value.empty() && value[0] == '$';
        }

        void validate_unique_id(std::vector<diagnostic>& diagnostics, std::string_view kind,
                                std::string_view id, std::span<const std::string> previous_ids) {
            if (id.empty()) {
                diagnostics.push_back(
                    watch_diagnostic({}, "game_watch." + std::string{kind} + ".id_empty",
                                     std::string{kind} + " definition id must not be empty"));
                return;
            }
            if (contains_string(previous_ids, id)) {
                diagnostics.push_back(watch_diagnostic(
                    std::string{id}, "game_watch." + std::string{kind} + ".duplicate",
                    std::string{kind} + " definition id must be unique"));
            }
        }

        void validate_watch_source(const watch_definition& watch,
                                   std::vector<diagnostic>& diagnostics) {
            if (watch.source.sample_point == watch_sample_point::unspecified) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.sample_point.unspecified",
                                     "watch source must declare a deterministic sample point"));
            }
            switch (watch.source.kind) {
            case watch_source_kind::memory_view:
                if (watch.source.memory_view.empty()) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.memory.view_empty",
                                         "memory watch source must name a memory view"));
                }
                if (watch.source.byte_count == 0U || watch.source.byte_count > 8U) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.memory.byte_count",
                                         "memory watch byte_count must be between one and eight"));
                }
                break;
            case watch_source_kind::register_field:
                if (watch.source.chip.empty() || watch.source.register_name.empty()) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.register.field_empty",
                                         "register watch source must name a chip and register"));
                }
                break;
            case watch_source_kind::frame_event:
                if (watch.source.event_name.empty()) {
                    diagnostics.push_back(
                        watch_diagnostic(watch.id, "game_watch.event.name_empty",
                                         "frame event watch source must name an event"));
                }
                break;
            }
        }

        void validate_decoder(const watch_definition& watch, std::vector<diagnostic>& diagnostics) {
            if (watch.decoder.scale_den == 0) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.decoder.scale_den",
                                     "watch decoder scale denominator must not be zero"));
            }
            if (watch.decoder.shift >= 64U) {
                diagnostics.push_back(watch_diagnostic(watch.id, "game_watch.decoder.shift",
                                                       "watch decoder shift must be less than 64"));
            }
            if (watch.decoder.clamp_min_enabled && watch.decoder.clamp_max_enabled &&
                watch.decoder.clamp_min > watch.decoder.clamp_max) {
                diagnostics.push_back(
                    watch_diagnostic(watch.id, "game_watch.decoder.clamp_range",
                                     "watch decoder clamp_min must not exceed clamp_max"));
            }
        }

        void validate_watch_ref(std::vector<diagnostic>& diagnostics,
                                std::span<const watch_definition> watches,
                                std::string_view owner_id, std::string_view watch_id,
                                std::string_view code_prefix) {
            if (watch_id.empty()) {
                diagnostics.push_back(watch_diagnostic(std::string{owner_id},
                                                       std::string{code_prefix} + ".watch_empty",
                                                       "watch reference must name a watch id"));
                return;
            }
            if (contains_watch_id(watches, watch_id)) {
                return;
            }
            if (looks_like_raw_address(watch_id)) {
                diagnostics.push_back(watch_diagnostic(
                    std::string{owner_id}, std::string{code_prefix} + ".raw_address",
                    "rule references a raw address; define and reference a watch id instead"));
                return;
            }
            diagnostics.push_back(watch_diagnostic(
                std::string{owner_id}, std::string{code_prefix} + ".watch_missing",
                "watch reference '" + std::string{watch_id} + "' does not match a defined watch"));
        }

        void validate_predicate(const watch_predicate& predicate,
                                std::span<const watch_definition> watches,
                                std::string_view owner_id, std::vector<diagnostic>& diagnostics) {
            switch (predicate.kind) {
            case watch_predicate_kind::always:
                break;
            case watch_predicate_kind::equals:
            case watch_predicate_kind::not_equals:
            case watch_predicate_kind::greater_than:
            case watch_predicate_kind::greater_equal:
            case watch_predicate_kind::less_than:
            case watch_predicate_kind::less_equal:
            case watch_predicate_kind::changed:
                validate_watch_ref(diagnostics, watches, owner_id, predicate.watch_id,
                                   "game_watch.predicate");
                break;
            case watch_predicate_kind::in_range:
                validate_watch_ref(diagnostics, watches, owner_id, predicate.watch_id,
                                   "game_watch.predicate");
                if (predicate.min > predicate.max) {
                    diagnostics.push_back(
                        watch_diagnostic(std::string{owner_id}, "game_watch.predicate.range",
                                         "range predicate min must not exceed max"));
                }
                break;
            case watch_predicate_kind::latched:
                if (predicate.id.empty()) {
                    diagnostics.push_back(
                        watch_diagnostic(std::string{owner_id}, "game_watch.predicate.latch_id",
                                         "latched predicate must declare a stable id"));
                }
                if (predicate.children.size() != 1U) {
                    diagnostics.push_back(watch_diagnostic(
                        std::string{owner_id}, "game_watch.predicate.latch_child",
                        "latched predicate must contain exactly one child predicate"));
                }
                break;
            case watch_predicate_kind::all:
            case watch_predicate_kind::any:
                if (predicate.children.empty()) {
                    diagnostics.push_back(watch_diagnostic(
                        std::string{owner_id}, "game_watch.predicate.children_empty",
                        "all/any predicates must contain at least one child predicate"));
                }
                break;
            }

            for (const auto& child : predicate.children) {
                validate_predicate(child, watches, owner_id, diagnostics);
            }
        }

        [[nodiscard]] bool compare_value(const std::int64_t lhs,
                                         const watch_predicate& predicate) noexcept {
            switch (predicate.kind) {
            case watch_predicate_kind::equals:
                return lhs == predicate.value;
            case watch_predicate_kind::not_equals:
                return lhs != predicate.value;
            case watch_predicate_kind::greater_than:
                return lhs > predicate.value;
            case watch_predicate_kind::greater_equal:
                return lhs >= predicate.value;
            case watch_predicate_kind::less_than:
                return lhs < predicate.value;
            case watch_predicate_kind::less_equal:
                return lhs <= predicate.value;
            case watch_predicate_kind::in_range:
                return lhs >= predicate.min && lhs <= predicate.max;
            default:
                return false;
            }
        }

        [[nodiscard]] bool evaluate_watch_comparison(const watch_predicate& predicate,
                                                     std::span<const watch_sample> current,
                                                     std::vector<diagnostic>& diagnostics) {
            const watch_sample* sample = find_sample(current, predicate.watch_id);
            if (sample == nullptr) {
                diagnostics.push_back(
                    watch_diagnostic(predicate.watch_id, "game_watch.predicate.sample_missing",
                                     "current samples did not include predicate watch '" +
                                         predicate.watch_id + "'"));
                return false;
            }
            return compare_value(sample->value, predicate);
        }

        [[nodiscard]] bool evaluate_changed(const watch_predicate& predicate,
                                            std::span<const watch_sample> current,
                                            std::span<const watch_sample> previous,
                                            std::vector<diagnostic>& diagnostics) {
            const watch_sample* current_sample = find_sample(current, predicate.watch_id);
            if (current_sample == nullptr) {
                diagnostics.push_back(
                    watch_diagnostic(predicate.watch_id, "game_watch.predicate.sample_missing",
                                     "current samples did not include predicate watch '" +
                                         predicate.watch_id + "'"));
                return false;
            }
            const watch_sample* previous_sample = find_sample(previous, predicate.watch_id);
            if (previous_sample == nullptr) {
                return false;
            }
            return current_sample->value != previous_sample->value;
        }

    } // namespace

    std::vector<diagnostic> validate_game_watch_pack(const game_watch_pack& pack) {
        std::vector<diagnostic> diagnostics;

        std::vector<std::string> watch_ids;
        watch_ids.reserve(pack.watches.size());
        for (const auto& watch : pack.watches) {
            validate_unique_id(diagnostics, "watch", watch.id, watch_ids);
            watch_ids.push_back(watch.id);
            validate_watch_source(watch, diagnostics);
            validate_decoder(watch, diagnostics);
        }

        std::vector<std::string> score_ids;
        score_ids.reserve(pack.scores.size());
        for (const auto& score : pack.scores) {
            validate_unique_id(diagnostics, "score", score.id, score_ids);
            score_ids.push_back(score.id);
            validate_watch_ref(diagnostics, pack.watches, score.id, score.watch_id,
                               "game_watch.score");
            if (const watch_definition* watch = find_watch(pack.watches, score.watch_id);
                watch != nullptr && watch->result_type != watch_result_type::score) {
                diagnostics.push_back(watch_diagnostic(
                    score.id, "game_watch.score.watch_type",
                    "score definition must reference a watch with result_type score"));
            }
        }

        std::vector<std::string> achievement_ids;
        achievement_ids.reserve(pack.achievements.size());
        for (const auto& achievement : pack.achievements) {
            validate_unique_id(diagnostics, "achievement", achievement.id, achievement_ids);
            achievement_ids.push_back(achievement.id);
            for (const auto& watch_id : achievement.watch_ids) {
                validate_watch_ref(diagnostics, pack.watches, achievement.id, watch_id,
                                   "game_watch.achievement");
            }
            for (const auto& watch_id : achievement.progress_watch_ids) {
                validate_watch_ref(diagnostics, pack.watches, achievement.id, watch_id,
                                   "game_watch.achievement_progress");
            }
            validate_predicate(achievement.predicate, pack.watches, achievement.id, diagnostics);
        }

        return diagnostics;
    }

    std::optional<watch_sample> sample_watch(const watch_definition& watch,
                                             const watch_observation_frame& frame,
                                             std::vector<diagnostic>& diagnostics) {
        if (!is_sample_point_match(watch.source, frame)) {
            return std::nullopt;
        }

        std::uint64_t raw = 0U;
        std::uint8_t source_bits = 0U;
        switch (watch.source.kind) {
        case watch_source_kind::memory_view: {
            std::optional<std::uint64_t> memory = read_memory_value(watch, frame, diagnostics);
            if (!memory.has_value()) {
                return std::nullopt;
            }
            raw = *memory;
            source_bits = static_cast<std::uint8_t>(watch.source.byte_count * 8U);
            break;
        }
        case watch_source_kind::register_field: {
            std::optional<register_observation> reg =
                read_register_value(watch, frame, diagnostics);
            if (!reg.has_value()) {
                return std::nullopt;
            }
            raw = reg->value;
            source_bits = clamped_source_bits(reg->bit_width);
            break;
        }
        case watch_source_kind::frame_event:
            raw = read_frame_event(watch, frame) ? 1U : 0U;
            source_bits = 1U;
            break;
        }

        std::optional<std::int64_t> decoded = decode_value(watch, raw, source_bits, diagnostics);
        if (!decoded.has_value()) {
            return std::nullopt;
        }
        return watch_sample{.watch_id = watch.id, .value = *decoded};
    }

    watch_sample_set sample_watches(std::span<const watch_definition> watches,
                                    const watch_observation_frame& frame) {
        watch_sample_set result;
        result.samples.reserve(watches.size());
        for (const auto& watch : watches) {
            std::optional<watch_sample> sample = sample_watch(watch, frame, result.diagnostics);
            if (sample.has_value()) {
                result.samples.push_back(std::move(*sample));
            }
        }
        return result;
    }

    bool evaluate_predicate(const watch_predicate& predicate, std::span<const watch_sample> current,
                            std::span<const watch_sample> previous, predicate_eval_state& state,
                            std::vector<diagnostic>& diagnostics) {
        switch (predicate.kind) {
        case watch_predicate_kind::always:
            return true;
        case watch_predicate_kind::equals:
        case watch_predicate_kind::not_equals:
        case watch_predicate_kind::greater_than:
        case watch_predicate_kind::greater_equal:
        case watch_predicate_kind::less_than:
        case watch_predicate_kind::less_equal:
        case watch_predicate_kind::in_range:
            return evaluate_watch_comparison(predicate, current, diagnostics);
        case watch_predicate_kind::changed:
            return evaluate_changed(predicate, current, previous, diagnostics);
        case watch_predicate_kind::latched:
            if (predicate.id.empty()) {
                diagnostics.push_back(watch_diagnostic({}, "game_watch.predicate.latch_id",
                                                       "latched predicate requires a stable id"));
                return false;
            }
            if (contains_string(state.latched_predicates, predicate.id)) {
                return true;
            }
            if (predicate.children.size() != 1U) {
                diagnostics.push_back(
                    watch_diagnostic(predicate.id, "game_watch.predicate.latch_child",
                                     "latched predicate requires exactly one child"));
                return false;
            }
            if (evaluate_predicate(predicate.children.front(), current, previous, state,
                                   diagnostics)) {
                state.latched_predicates.push_back(predicate.id);
                return true;
            }
            return false;
        case watch_predicate_kind::all: {
            if (predicate.children.empty()) {
                diagnostics.push_back(watch_diagnostic(predicate.id,
                                                       "game_watch.predicate.children_empty",
                                                       "all predicate requires child predicates"));
                return false;
            }
            bool all_true = true;
            for (const auto& child : predicate.children) {
                all_true =
                    evaluate_predicate(child, current, previous, state, diagnostics) && all_true;
            }
            return all_true;
        }
        case watch_predicate_kind::any: {
            if (predicate.children.empty()) {
                diagnostics.push_back(watch_diagnostic(predicate.id,
                                                       "game_watch.predicate.children_empty",
                                                       "any predicate requires child predicates"));
                return false;
            }
            bool any_true = false;
            for (const auto& child : predicate.children) {
                any_true =
                    evaluate_predicate(child, current, previous, state, diagnostics) || any_true;
            }
            return any_true;
        }
        }
        return false;
    }

} // namespace mnemos::debug
