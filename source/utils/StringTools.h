/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * for WiiXplorer 2010
 ***************************************************************************/
#ifndef __STRING_TOOLS_H
#define __STRING_TOOLS_H

#include <vector>
#include <string>
#include <wut_types.h>

class StringTools {
public:
    static BOOL EndsWith(const std::string &a, const std::string &b);

    static const char *byte_to_binary(int32_t x);

    static std::string removeCharFromString(std::string &input, char toBeRemoved);

    static const char *fmt(const char *format, ...);

    static const wchar_t *wfmt(const char *format, ...);

    static int32_t strprintf(std::string &str, const char *format, ...);

    static std::string strfmt(const char *format, ...);

    static BOOL char2wchar_t(const char *src, wchar_t *dest);

    static int32_t strtokcmp(const char *string, const char *compare, const char *separator);

    static int32_t strextcmp(const char *string, const char *extension, char seperator);

    static char *str_replace(char *orig, char *rep, char *with);

    static const char *FullpathToFilename(const char *path);

    static void RemoveDoubleSlashs(std::string &str);

    static std::vector<std::string> stringSplit(const std::string &value, const std::string &splitter);
};

#endif /* __STRING_TOOLS_H */

