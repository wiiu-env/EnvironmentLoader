#include "RelocationData.h"
#include "utils/StringTools.h"

std::string RelocationData::toString() const {
    return StringTools::strfmt("%s destination: %08X offset: %08X type: %02X addend: %d rplName: %s isData: %d \n", name.c_str(), destination, offset, type, addend, rplInfo->getName().c_str(),
                               rplInfo->isData());
}
