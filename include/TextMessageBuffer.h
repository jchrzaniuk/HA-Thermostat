#pragma once
#include <cstddef>
#include <string>

// Reassembles both WebSocket fragmentation and ESP-IDF's receive-buffer chunks.
class TextMessageBuffer {
public:
    enum class Result { ignored, incomplete, complete, invalid };
    explicit TextMessageBuffer(size_t limit) : limit_(limit) {}
    void clear() { data_.clear(); active_=false; expectedOffset_=0; }
    const std::string &text() const { return data_; }
    Result feed(int opcode, const char *bytes, size_t length, size_t offset,
                size_t frameLength, bool final) {
        if(opcode!=0 && opcode!=1) return Result::ignored; // Control frames can interleave.
        if(opcode==1 && offset==0) { clear(); active_=true; }
        if(!active_ || offset!=expectedOffset_ || offset>frameLength ||
           length>frameLength-offset || data_.size()>limit_ || length>limit_-data_.size()) {
            clear(); return Result::invalid;
        }
        if(length) data_.append(bytes,length);
        expectedOffset_=offset+length;
        if(expectedOffset_!=frameLength) return Result::incomplete;
        expectedOffset_=0;
        if(!final) return Result::incomplete;
        active_=false; return Result::complete;
    }
private:
    size_t limit_,expectedOffset_=0;
    bool active_=false;
    std::string data_;
};
