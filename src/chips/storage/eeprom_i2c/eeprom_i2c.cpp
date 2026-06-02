#include "eeprom_i2c.hpp"

namespace mnemos::chips::storage {

    namespace {
        // High address bits the control byte carries for the 512 B - 2 KiB parts:
        // 24C04 = 1, 24C08 = 2, 24C16 = 3.
        int derive_block_bits(std::size_t size) noexcept {
            int bits = 0;
            for (std::size_t s = 512U; s <= 2048U && size >= s; s *= 2U) {
                ++bits;
            }
            return bits;
        }
    } // namespace

    eeprom_i2c::eeprom_i2c(std::size_t size_bytes) : store_(size_bytes, 0xFFU) {
        addr_mask_ = size_bytes ? static_cast<std::uint32_t>(size_bytes - 1U) : 0U;
        if (size_bytes <= 256U) {
            word_addr_bytes_ = 1;
            block_bits_ = 0;
        } else if (size_bytes <= 2048U) {
            word_addr_bytes_ = 1;
            block_bits_ = derive_block_bits(size_bytes);
        } else {
            word_addr_bytes_ = 2;
            block_bits_ = 0;
        }
    }

    bool eeprom_i2c::sda() const noexcept { return sda_out_; }

    void eeprom_i2c::reset() noexcept {
        stage_ = stage::idle;
        bit_count_ = 0;
        shift_in_ = 0;
        transmitting_ = false;
        sda_out_ = true;
        prev_scl_ = true;
        prev_sda_ = true;
        addr_ = 0;
    }

    void eeprom_i2c::update(bool scl, bool sda) noexcept {
        // START / STOP: an SDA edge while SCL stays high.
        if (scl && prev_scl_) {
            if (prev_sda_ && !sda) { // START (or repeated START); the pointer survives
                stage_ = stage::control;
                bit_count_ = 0;
                shift_in_ = 0;
                transmitting_ = false;
                sda_out_ = true;
                prev_sda_ = sda;
                return;
            }
            if (!prev_sda_ && sda) { // STOP
                stage_ = stage::idle;
                transmitting_ = false;
                sda_out_ = true;
                prev_sda_ = sda;
                return;
            }
        }

        if (scl && !prev_scl_) { // rising edge: a bit (or the ACK) is clocked
            if (stage_ != stage::idle) {
                if (bit_count_ < 8) {
                    if (!transmitting_) { // receiving a bit from the master
                        shift_in_ = static_cast<std::uint8_t>((shift_in_ << 1U) | (sda ? 1U : 0U));
                    }
                    ++bit_count_;
                    if (bit_count_ == 8 && !transmitting_) {
                        on_byte(shift_in_);
                        sda_out_ = false; // drive ACK low for the 9th clock
                    }
                } else { // 9th clock high (ACK bit)
                    if (transmitting_) {
                        master_ack_ = !sda; // master pulled SDA low -> wants more
                    }
                    bit_count_ = 9;
                }
            }
        } else if (!scl && prev_scl_) { // falling edge: advance / drive next bit
            if (stage_ != stage::idle) {
                if (bit_count_ == 9) {
                    bit_count_ = 0;
                    shift_in_ = 0;
                    if (transmitting_) {   // a read byte just finished
                        if (master_ack_) { // sequential read: hand over the next cell
                            addr_ = (addr_ + 1U) & addr_mask_;
                            shift_out_ = store_[addr_ & addr_mask_];
                            sda_out_ = (shift_out_ & 0x80U) != 0U;
                        } else { // NAK: master ends the read
                            transmitting_ = false;
                            stage_ = stage::idle;
                            sda_out_ = true;
                        }
                    } else {
                        sda_out_ = true;                  // release the received-byte ACK
                        if (stage_ == stage::read_data) { // control(read) -> begin output
                            transmitting_ = true;
                            sda_out_ = (shift_out_ & 0x80U) != 0U;
                        }
                    }
                } else if (transmitting_ && bit_count_ < 8) {
                    sda_out_ = ((shift_out_ >> (7 - bit_count_)) & 1U) != 0U; // next bit, MSB-first
                } else if (transmitting_ && bit_count_ == 8) {
                    sda_out_ = true; // release SDA so the master can drive ACK/NAK
                }
            }
        }

        prev_scl_ = scl;
        prev_sda_ = sda;
    }

    void eeprom_i2c::on_byte(std::uint8_t byte) noexcept {
        switch (stage_) {
        case stage::control:
            reading_ = (byte & 1U) != 0U;
            block_high_ = block_bits_ != 0 ? ((static_cast<std::uint32_t>(byte) >> 1U) &
                                              ((1U << static_cast<unsigned>(block_bits_)) - 1U))
                                           : 0U;
            if (reading_) {
                stage_ = stage::read_data;
                shift_out_ = store_[addr_ & addr_mask_]; // current-address read source
            } else {
                stage_ = word_addr_bytes_ == 2 ? stage::word_hi : stage::word_lo;
            }
            break;
        case stage::word_hi:
            addr_ = static_cast<std::uint32_t>(byte) << 8U;
            stage_ = stage::word_lo;
            break;
        case stage::word_lo:
            addr_ =
                word_addr_bytes_ == 2 ? ((addr_ & 0xFF00U) | byte) : ((block_high_ << 8U) | byte);
            addr_ &= addr_mask_;
            stage_ = stage::write_data;
            break;
        case stage::write_data:
            store_[addr_ & addr_mask_] = byte;
            addr_ = (addr_ + 1U) & addr_mask_; // auto-increment
            break;
        case stage::idle:
        case stage::read_data:
            break;
        }
    }

} // namespace mnemos::chips::storage
