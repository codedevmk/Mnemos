#include "amiga_ipf_caps.hpp"

#include "amiga_system.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mnemos::apps::player {
namespace {

#if defined(_MSC_VER)
#define MNEMOS_CAPS_GETENV(name_) mnemos_caps_getenv_msvc(name_)
    [[nodiscard]] const char* mnemos_caps_getenv_msvc(const char* name) {
#pragma warning(push)
#pragma warning(disable : 4996)
        const char* value = std::getenv(name);
#pragma warning(pop)
        return value;
    }
#else
#define MNEMOS_CAPS_GETENV(name_) std::getenv(name_)
#endif

#if defined(_WIN32)
#define MNEMOS_CAPS_CALL __cdecl
    using native_library_handle = HMODULE;
#else
#define MNEMOS_CAPS_CALL
    using native_library_handle = void*;
#endif

    using caps_byte = std::uint8_t;
    using caps_long = std::int32_t;
    using caps_ulong = std::uint32_t;

    constexpr caps_long caps_ok = 0;
    constexpr caps_long caps_out_of_range = 3;
    constexpr caps_ulong caps_image_type_floppy = 1U;
    constexpr caps_ulong caps_lock_align = 1U << 1U;
    constexpr caps_ulong caps_lock_noise = 1U << 5U;
    constexpr caps_ulong caps_lock_noise_revolutions = 1U << 6U;
    constexpr caps_ulong caps_max_platform = 4U;
    constexpr caps_ulong caps_max_track_revolutions = 5U;
    constexpr std::size_t amiga_caps_cylinder_count =
        mnemos::manifests::amiga::amiga_system::floppy_cylinders;
    constexpr std::size_t amiga_caps_head_count =
        mnemos::manifests::amiga::amiga_system::floppy_heads;
    constexpr std::size_t amiga_caps_track_count =
        mnemos::manifests::amiga::amiga_system::floppy_track_count;

#pragma pack(push, 1)
    struct caps_date_time_ext final {
        caps_ulong year{};
        caps_ulong month{};
        caps_ulong day{};
        caps_ulong hour{};
        caps_ulong min{};
        caps_ulong sec{};
        caps_ulong tick{};
    };

    struct caps_image_info final {
        caps_ulong type{};
        caps_ulong release{};
        caps_ulong revision{};
        caps_ulong mincylinder{};
        caps_ulong maxcylinder{};
        caps_ulong minhead{};
        caps_ulong maxhead{};
        caps_date_time_ext crdt{};
        std::array<caps_ulong, caps_max_platform> platform{};
    };

    struct caps_track_info final {
        caps_ulong type{};
        caps_ulong cylinder{};
        caps_ulong head{};
        caps_ulong sectorcnt{};
        caps_ulong sectorsize{};
        caps_ulong trackcnt{};
        caps_byte* trackbuf{};
        caps_ulong tracklen{};
        std::array<caps_byte*, caps_max_track_revolutions> trackdata{};
        std::array<caps_ulong, caps_max_track_revolutions> tracksize{};
        caps_ulong timelen{};
        caps_ulong* timebuf{};
    };
#pragma pack(pop)

    using caps_init_fn = caps_long(MNEMOS_CAPS_CALL*)();
    using caps_exit_fn = caps_long(MNEMOS_CAPS_CALL*)();
    using caps_add_image_fn = caps_long(MNEMOS_CAPS_CALL*)();
    using caps_remove_image_fn = caps_long(MNEMOS_CAPS_CALL*)(caps_long);
    using caps_lock_image_memory_fn =
        caps_long(MNEMOS_CAPS_CALL*)(caps_long, caps_byte*, caps_ulong, caps_ulong);
    using caps_unlock_image_fn = caps_long(MNEMOS_CAPS_CALL*)(caps_long);
    using caps_load_image_fn = caps_long(MNEMOS_CAPS_CALL*)(caps_long, caps_ulong);
    using caps_get_image_info_fn = caps_long(MNEMOS_CAPS_CALL*)(caps_image_info*, caps_long);
    using caps_lock_track_fn =
        caps_long(MNEMOS_CAPS_CALL*)(caps_track_info*, caps_long, caps_ulong, caps_ulong,
                                     caps_ulong);
    using caps_unlock_track_fn =
        caps_long(MNEMOS_CAPS_CALL*)(caps_long, caps_ulong, caps_ulong);
    using caps_unlock_all_tracks_fn = caps_long(MNEMOS_CAPS_CALL*)(caps_long);

    [[nodiscard]] const char* getenv_nonempty(const char* name) noexcept {
        const char* value = MNEMOS_CAPS_GETENV(name);
        return value != nullptr && value[0] != '\0' ? value : nullptr;
    }

    [[nodiscard]] const char* caps_error_name(caps_long error) noexcept {
        switch (error) {
        case 0:
            return "imgeOk";
        case 1:
            return "imgeUnsupported";
        case 2:
            return "imgeGeneric";
        case 3:
            return "imgeOutOfRange";
        case 4:
            return "imgeReadOnly";
        case 5:
            return "imgeOpen";
        case 6:
            return "imgeType";
        case 7:
            return "imgeShort";
        case 8:
            return "imgeTrackHeader";
        case 9:
            return "imgeTrackStream";
        case 10:
            return "imgeTrackData";
        case 11:
            return "imgeDensityHeader";
        case 12:
            return "imgeDensityStream";
        case 13:
            return "imgeDensityData";
        case 14:
            return "imgeIncompatible";
        case 15:
            return "imgeUnsupportedType";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::string caps_error_message(caps_long error) {
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "%s (%d)", caps_error_name(error),
                      static_cast<int>(error));
        return std::string{buffer};
    }

    void append_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value >> 8U));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
        append_be16(out, static_cast<std::uint16_t>(value >> 16U));
        append_be16(out, static_cast<std::uint16_t>(value));
    }

    [[nodiscard]] native_library_handle load_native_library(const std::string& path) noexcept {
#if defined(_WIN32)
        return LoadLibraryA(path.c_str());
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }

    void close_native_library(native_library_handle handle) noexcept {
        if (handle == nullptr) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }

    [[nodiscard]] void* native_symbol(native_library_handle handle, const char* name) noexcept {
        if (handle == nullptr) {
            return nullptr;
        }
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle, name));
#else
        return dlsym(handle, name);
#endif
    }

    class caps_library final {
      public:
        caps_library() = default;
        caps_library(const caps_library&) = delete;
        caps_library& operator=(const caps_library&) = delete;

        caps_library(caps_library&& other) noexcept { move_from(other); }

        caps_library& operator=(caps_library&& other) noexcept {
            if (this != &other) {
                close();
                move_from(other);
            }
            return *this;
        }

        ~caps_library() { close(); }

        [[nodiscard]] bool open(const std::string& path, std::string& error) {
            close();
            handle_ = load_native_library(path);
            if (handle_ == nullptr) {
                return false;
            }
            loaded_path_ = path;
            if (!bind(init_, "CAPSInit", error) || !bind(exit_, "CAPSExit", error) ||
                !bind(add_image_, "CAPSAddImage", error) ||
                !bind(remove_image_, "CAPSRemImage", error) ||
                !bind(lock_image_memory_, "CAPSLockImageMemory", error) ||
                !bind(unlock_image_, "CAPSUnlockImage", error) ||
                !bind(load_image_, "CAPSLoadImage", error) ||
                !bind(get_image_info_, "CAPSGetImageInfo", error) ||
                !bind(lock_track_, "CAPSLockTrack", error) ||
                !bind(unlock_track_, "CAPSUnlockTrack", error) ||
                !bind(unlock_all_tracks_, "CAPSUnlockAllTracks", error)) {
                close();
                return false;
            }
            return true;
        }

        void close() noexcept {
            close_native_library(handle_);
            handle_ = nullptr;
            loaded_path_.clear();
            init_ = nullptr;
            exit_ = nullptr;
            add_image_ = nullptr;
            remove_image_ = nullptr;
            lock_image_memory_ = nullptr;
            unlock_image_ = nullptr;
            load_image_ = nullptr;
            get_image_info_ = nullptr;
            lock_track_ = nullptr;
            unlock_track_ = nullptr;
            unlock_all_tracks_ = nullptr;
        }

        [[nodiscard]] const std::string& loaded_path() const noexcept { return loaded_path_; }

        caps_init_fn init() const noexcept { return init_; }
        caps_exit_fn exit() const noexcept { return exit_; }
        caps_add_image_fn add_image() const noexcept { return add_image_; }
        caps_remove_image_fn remove_image() const noexcept { return remove_image_; }
        caps_lock_image_memory_fn lock_image_memory() const noexcept {
            return lock_image_memory_;
        }
        caps_unlock_image_fn unlock_image() const noexcept { return unlock_image_; }
        caps_load_image_fn load_image() const noexcept { return load_image_; }
        caps_get_image_info_fn get_image_info() const noexcept { return get_image_info_; }
        caps_lock_track_fn lock_track() const noexcept { return lock_track_; }
        caps_unlock_track_fn unlock_track() const noexcept { return unlock_track_; }
        caps_unlock_all_tracks_fn unlock_all_tracks() const noexcept {
            return unlock_all_tracks_;
        }

      private:
        template <typename Function>
        [[nodiscard]] bool bind(Function& out, const char* name, std::string& error) noexcept {
            void* symbol = native_symbol(handle_, name);
            if (symbol == nullptr) {
                error = "CAPSImg library is missing export ";
                error += name;
                return false;
            }
            out = reinterpret_cast<Function>(symbol);
            return true;
        }

        void move_from(caps_library& other) noexcept {
            handle_ = std::exchange(other.handle_, nullptr);
            loaded_path_ = std::move(other.loaded_path_);
            init_ = std::exchange(other.init_, nullptr);
            exit_ = std::exchange(other.exit_, nullptr);
            add_image_ = std::exchange(other.add_image_, nullptr);
            remove_image_ = std::exchange(other.remove_image_, nullptr);
            lock_image_memory_ = std::exchange(other.lock_image_memory_, nullptr);
            unlock_image_ = std::exchange(other.unlock_image_, nullptr);
            load_image_ = std::exchange(other.load_image_, nullptr);
            get_image_info_ = std::exchange(other.get_image_info_, nullptr);
            lock_track_ = std::exchange(other.lock_track_, nullptr);
            unlock_track_ = std::exchange(other.unlock_track_, nullptr);
            unlock_all_tracks_ = std::exchange(other.unlock_all_tracks_, nullptr);
        }

        native_library_handle handle_{};
        std::string loaded_path_{};
        caps_init_fn init_{};
        caps_exit_fn exit_{};
        caps_add_image_fn add_image_{};
        caps_remove_image_fn remove_image_{};
        caps_lock_image_memory_fn lock_image_memory_{};
        caps_unlock_image_fn unlock_image_{};
        caps_load_image_fn load_image_{};
        caps_get_image_info_fn get_image_info_{};
        caps_lock_track_fn lock_track_{};
        caps_unlock_track_fn unlock_track_{};
        caps_unlock_all_tracks_fn unlock_all_tracks_{};
    };

    [[nodiscard]] bool is_directory_path(const std::string& path) {
        std::error_code ec;
        return std::filesystem::is_directory(std::filesystem::path{path}, ec);
    }

    void push_unique(std::vector<std::string>& out, std::string value) {
        if (value.empty()) {
            return;
        }
        if (std::find(out.begin(), out.end(), value) == out.end()) {
            out.push_back(std::move(value));
        }
    }

    void push_env_path(std::vector<std::string>& out, const char* name) {
        if (const char* value = getenv_nonempty(name); value != nullptr) {
            push_unique(out, std::string{value});
        }
    }

    [[nodiscard]] constexpr std::array<std::string_view, 5> caps_library_names() noexcept {
#if defined(_WIN32)
        return {"CAPSImg.dll", "CAPSImg_x64.dll", "capsimg.dll", "capsimg_x64.dll",
                "CAPSIMAGE.DLL"};
#else
        return {"libcapsimage.so", "libcapsimg.so", "CAPSImg.so", "capsimg.so",
                "CAPSImage.framework/CAPSImage"};
#endif
    }

    void push_env_directory_candidates(std::vector<std::string>& out, const char* name) {
        const char* value = getenv_nonempty(name);
        if (value == nullptr || !is_directory_path(value)) {
            return;
        }
        const std::filesystem::path dir{value};
        for (std::string_view library_name : caps_library_names()) {
            push_unique(out, (dir / std::filesystem::path{library_name}).string());
        }
    }

    [[nodiscard]] std::vector<std::string> caps_candidate_paths() {
        std::vector<std::string> out;
        for (const char* env_name :
             {"MNEMOS_AMIGA_CAPSIMG_DLL", "MNEMOS_CAPSIMG_DLL",
              "MNEMOS_IPF_CAPSIMG_DLL", "MNEMOS_AMIGA_CAPSIMG", "MNEMOS_CAPSIMG"}) {
            push_env_path(out, env_name);
        }
        for (const char* env_name :
             {"MNEMOS_AMIGA_CAPSIMG_DIR", "MNEMOS_CAPSIMG_DIR",
              "MNEMOS_AMIGA_BIOS_DIR", "MNEMOS_AMIGA_KICKSTART_DIR"}) {
            push_env_directory_candidates(out, env_name);
        }
        for (std::string_view library_name : caps_library_names()) {
            push_unique(out, std::string{library_name});
        }
        return out;
    }

    [[nodiscard]] std::optional<caps_library> load_caps_library(std::string& error) {
        std::string last_error;
        for (const std::string& path : caps_candidate_paths()) {
            caps_library lib;
            std::string bind_error;
            if (lib.open(path, bind_error)) {
                return lib;
            }
            if (!bind_error.empty()) {
                last_error = path;
                last_error += ": ";
                last_error += bind_error;
            }
        }

        error =
            "CAPSImg library not found. Set MNEMOS_AMIGA_CAPSIMG_DLL or "
            "MNEMOS_CAPSIMG_DLL to the SPS CAPSImg library, place CAPSImg.dll "
            "or CAPSImg_x64.dll in MNEMOS_AMIGA_BIOS_DIR, or put the library "
            "beside mnemos_player. The SPS IPF User Library must be downloaded "
            "separately.";
        if (!last_error.empty()) {
            error += " Last load attempt failed: ";
            error += last_error;
        }
        return std::nullopt;
    }

    struct caps_session final {
        caps_library* lib{};
        bool initialized{};

        ~caps_session() {
            if (initialized && lib != nullptr && lib->exit() != nullptr) {
                static_cast<void>(lib->exit()());
            }
        }
    };

    struct caps_image_guard final {
        caps_library* lib{};
        caps_long id{-1};
        bool locked{};

        ~caps_image_guard() {
            if (lib == nullptr || id < 0) {
                return;
            }
            if (locked && lib->unlock_image() != nullptr) {
                static_cast<void>(lib->unlock_image()(id));
            }
            if (lib->remove_image() != nullptr) {
                static_cast<void>(lib->remove_image()(id));
            }
        }
    };

    [[nodiscard]] std::size_t caps_track_index(caps_ulong cylinder, caps_ulong head) noexcept {
        return static_cast<std::size_t>(cylinder) * amiga_caps_head_count +
               static_cast<std::size_t>(head);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_extended_adf_from_raw_tracks(
        const std::array<std::vector<std::uint8_t>, amiga_caps_track_count>& raw_tracks) {
        std::size_t payload_size = 0U;
        for (const auto& track : raw_tracks) {
            payload_size += track.size();
        }

        std::vector<std::uint8_t> image;
        image.reserve(12U + amiga_caps_track_count * 12U + payload_size);
        image.insert(image.end(), {'U', 'A', 'E', '-', '1', 'A', 'D', 'F'});
        append_be16(image, 0U);
        append_be16(image, static_cast<std::uint16_t>(amiga_caps_track_count));

        for (const auto& track : raw_tracks) {
            append_be16(image, 0U);
            append_be16(image, 1U);
            append_be32(image, static_cast<std::uint32_t>(track.size()));
            append_be32(image, static_cast<std::uint32_t>(track.size() * 8U));
        }
        for (const auto& track : raw_tracks) {
            image.insert(image.end(), track.begin(), track.end());
        }
        return image;
    }

    [[nodiscard]] amiga_ipf_decode_result make_error(std::string error) {
        amiga_ipf_decode_result result;
        result.error = std::move(error);
        return result;
    }

} // namespace

    amiga_ipf_decode_result
    decode_amiga_ipf_to_extended_adf(std::span<const std::uint8_t> ipf_image,
                                     std::string_view display_name) {
        if (ipf_image.empty()) {
            return make_error("empty IPF image");
        }
        if (ipf_image.size() > std::numeric_limits<caps_ulong>::max()) {
            return make_error("IPF image is too large for the CAPS Access API");
        }

        std::string load_error;
        auto lib_opt = load_caps_library(load_error);
        if (!lib_opt.has_value()) {
            return make_error(std::move(load_error));
        }
        caps_library lib = std::move(*lib_opt);

        const caps_long init_result = lib.init()();
        caps_session session{.lib = &lib, .initialized = init_result == caps_ok};
        if (!session.initialized) {
            return make_error("CAPSInit failed for '" + std::string{lib.loaded_path()} +
                              "': " + caps_error_message(init_result));
        }

        const caps_long image_id = lib.add_image()();
        if (image_id < 0) {
            return make_error("CAPSAddImage failed for '" + std::string{display_name} + "'");
        }
        caps_image_guard image{.lib = &lib, .id = image_id};

        std::vector<caps_byte> mutable_ipf(ipf_image.begin(), ipf_image.end());
        const caps_long lock_result = lib.lock_image_memory()(
            image_id, mutable_ipf.data(), static_cast<caps_ulong>(mutable_ipf.size()), 0U);
        if (lock_result != caps_ok) {
            return make_error("CAPSLockImageMemory failed for '" + std::string{display_name} +
                              "': " + caps_error_message(lock_result));
        }
        image.locked = true;

        caps_image_info info{};
        const caps_long info_result = lib.get_image_info()(&info, image_id);
        if (info_result != caps_ok) {
            return make_error("CAPSGetImageInfo failed for '" + std::string{display_name} +
                              "': " + caps_error_message(info_result));
        }
        if (info.type != caps_image_type_floppy) {
            return make_error("IPF image '" + std::string{display_name} +
                              "' is not a floppy-disk image");
        }
        if (info.minhead >= amiga_caps_head_count) {
            return make_error("IPF image '" + std::string{display_name} +
                              "' does not contain Amiga floppy heads 0/1");
        }

        constexpr caps_ulong decode_flags =
            caps_lock_align | caps_lock_noise | caps_lock_noise_revolutions;
        static_cast<void>(lib.load_image()(image_id, decode_flags));

        std::array<std::vector<std::uint8_t>, amiga_caps_track_count> raw_tracks{};
        std::size_t decoded_tracks = 0U;
        const caps_ulong first_cylinder = std::min<caps_ulong>(
            info.mincylinder, static_cast<caps_ulong>(amiga_caps_cylinder_count));
        const caps_ulong last_cylinder =
            std::min<caps_ulong>(info.maxcylinder,
                                 static_cast<caps_ulong>(amiga_caps_cylinder_count - 1U));
        const caps_ulong first_head =
            std::min<caps_ulong>(info.minhead, static_cast<caps_ulong>(amiga_caps_head_count));
        const caps_ulong last_head =
            std::min<caps_ulong>(info.maxhead, static_cast<caps_ulong>(amiga_caps_head_count - 1U));

        for (caps_ulong cylinder = first_cylinder; cylinder <= last_cylinder; ++cylinder) {
            for (caps_ulong head = first_head; head <= last_head; ++head) {
                caps_track_info track{};
                const caps_long track_result =
                    lib.lock_track()(&track, image_id, cylinder, head, decode_flags);
                if (track_result == caps_out_of_range) {
                    continue;
                }
                if (track_result != caps_ok) {
                    return make_error("CAPSLockTrack failed for '" + std::string{display_name} +
                                      "' cylinder " + std::to_string(cylinder) + " head " +
                                      std::to_string(head) + ": " +
                                      caps_error_message(track_result));
                }
                const caps_byte* const data = track.trackdata[0];
                caps_ulong size = track.tracksize[0];
                if (data == nullptr || size == 0U) {
                    static_cast<void>(lib.unlock_track()(image_id, cylinder, head));
                    continue;
                }
                if ((size & 1U) != 0U) {
                    ++size;
                }
                if (size > std::numeric_limits<std::uint32_t>::max() / 8U) {
                    static_cast<void>(lib.unlock_track()(image_id, cylinder, head));
                    return make_error("CAPS track is too large for the extended ADF carrier in '" +
                                      std::string{display_name} + "'");
                }
                std::vector<std::uint8_t> raw(size, 0U);
                std::copy_n(data, static_cast<std::size_t>(track.tracksize[0]), raw.begin());
                raw_tracks[caps_track_index(cylinder, head)] = std::move(raw);
                ++decoded_tracks;
                static_cast<void>(lib.unlock_track()(image_id, cylinder, head));
            }
        }

        if (decoded_tracks == 0U) {
            return make_error("CAPS decoded no Amiga-readable tracks for '" +
                              std::string{display_name} + "'");
        }

        amiga_ipf_decode_result result;
        result.image = make_extended_adf_from_raw_tracks(raw_tracks);
        return result;
    }

} // namespace mnemos::apps::player
