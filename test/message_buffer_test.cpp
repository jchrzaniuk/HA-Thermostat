#include "TextMessageBuffer.h"
#include <cassert>
int main() {
    using R=TextMessageBuffer::Result;
    TextMessageBuffer b(8);
    assert(b.feed(1,"abc",3,0,6,true)==R::incomplete);
    assert(b.feed(1,"def",3,3,6,true)==R::complete);
    assert(b.text()=="abcdef");
    assert(b.feed(1,"ab",2,0,2,false)==R::incomplete);
    assert(b.feed(9,"ping",4,0,4,true)==R::ignored);
    assert(b.feed(0,"c",1,0,2,true)==R::incomplete);
    assert(b.feed(0,"d",1,1,2,true)==R::complete);
    assert(b.text()=="abcd");
    assert(b.feed(0,"orphan",6,0,6,true)==R::invalid);
    assert(b.feed(1,"abcdef",6,0,6,false)==R::incomplete);
    assert(b.feed(0,"ghi",3,0,3,true)==R::invalid);
    assert(b.feed(1,"a",1,0,3,true)==R::incomplete);
    assert(b.feed(1,"c",1,2,3,true)==R::invalid);
    b.clear();
    assert(b.feed(1,"ok",2,0,2,true)==R::complete);
    assert(b.text()=="ok");
}
