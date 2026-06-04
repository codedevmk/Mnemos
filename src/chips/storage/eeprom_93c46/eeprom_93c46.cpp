#include "eeprom_93c46.hpp"

namespace mnemos::chips::storage {

    void eeprom_93c46::reset() noexcept {
        stage_ = stage::wait_start;
        cs_ = false;
        clk_ = false;
        data_out_ = true;
        write_enable_ = false;
        cycles_ = 0;
        opcode_ = 0;
        buffer_ = 0;
    }

    void eeprom_93c46::decode_opcode() noexcept {
        // Top 2 bits select the command; for the "special" group (00) the next 2
        // bits sub-select. The low 6 bits are the word address.
        switch ((opcode_ >> 6U) & 0x3U) {
        case 1U: // WRITE (single word): clock in 16 data bits next
            buffer_ = 0;
            cycles_ = 0;
            stage_ = stage::write_word;
            break;
        case 2U: // READ: load the addressed word and stream it out
            buffer_ = word_at(opcode_);
            cycles_ = 0;
            stage_ = stage::read_word;
            data_out_ = false; // leading dummy 0 before the 16 data bits
            break;
        case 3U: // ERASE one word
            if (write_enable_) {
                set_word(opcode_, 0xFFFFU);
            }
            stage_ = stage::standby;
            break;
        default: // 00: special command, sub-selected by bits 5-4
            switch ((opcode_ >> 4U) & 0x3U) {
            case 1U: // WRITE ALL: 16 data bits next, broadcast to every word
                buffer_ = 0;
                cycles_ = 0;
                stage_ = stage::write_word;
                break;
            case 2U: // ERASE ALL
                if (write_enable_) {
                    store_.fill(0xFFU);
                }
                stage_ = stage::standby;
                break;
            default: // EWEN (11) / EWDS (00): write-enable latch = bit 4
                write_enable_ = ((opcode_ >> 4U) & 1U) != 0U;
                stage_ = stage::standby;
                break;
            }
            break;
        }
    }

    void eeprom_93c46::update(bool cs, bool clk, bool di) noexcept {
        if (cs) {
            if (clk && !clk_) { // DI sampled on the CLK low->high edge
                switch (stage_) {
                case stage::wait_start:
                    if (di) { // START bit: begin clocking the opcode
                        opcode_ = 0;
                        cycles_ = 0;
                        stage_ = stage::get_opcode;
                    }
                    break;
                case stage::get_opcode:
                    opcode_ =
                        static_cast<std::uint8_t>(opcode_ | ((di ? 1U : 0U) << (7U - cycles_)));
                    if (++cycles_ == 8U) {
                        decode_opcode();
                    }
                    break;
                case stage::write_word:
                    buffer_ =
                        static_cast<std::uint16_t>(buffer_ | ((di ? 1U : 0U) << (15U - cycles_)));
                    if (++cycles_ == 16U) {
                        if (write_enable_) {
                            if ((opcode_ & 0x40U) != 0U) {
                                set_word(opcode_, buffer_); // WRITE single word
                            } else {
                                for (std::uint8_t w = 0; w < 64U; ++w) { // WRITE ALL
                                    set_word(w, buffer_);
                                }
                            }
                        }
                        stage_ = stage::standby;
                    }
                    break;
                case stage::read_word:
                    data_out_ = ((buffer_ >> (15U - cycles_)) & 1U) != 0U;
                    if (++cycles_ == 16U) {
                        // Sequential read (93C46B): advance to the next word and continue.
                        ++opcode_;
                        cycles_ = 0;
                        buffer_ = word_at(opcode_);
                    }
                    break;
                case stage::standby:
                    break;
                }
            }
        } else if (cs_) { // CS high->low: return to standby, release DO
            data_out_ = true;
            stage_ = stage::wait_start;
        }

        cs_ = cs;
        clk_ = clk;
    }

} // namespace mnemos::chips::storage
