.section ".text"
    .global textStart
textStart:
    mflr 4;
    bl a
    mflr 3;
    mtlr 4;
    subi 3,3,0x8
a:   blr
