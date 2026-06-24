#include "TextUtil.h"
#include <cstring>

fmx::TextUniquePtr TextFromString(const std::string& s)
{
    fmx::TextUniquePtr text;
    text->AssignWithLength(s.c_str(), static_cast<fmx::uint32>(s.size()), fmx::Text::kEncoding_UTF8);
    return text;
}

std::string StringFromData(const fmx::Data& data)
{
    const fmx::Text& text = data.GetAsText();
    fmx::uint32 charCount = text.GetSize();
    if (charCount == 0) return {};

    // GetBytes signature: (buffer, buffersize, position, size, encoding)
    // size = kSize_End means "all characters from position onwards"
    std::string buf(static_cast<size_t>(charCount) * 4 + 1, '\0');
    text.GetBytes(
        buf.data(),
        static_cast<fmx::uint32>(buf.size()),
        0,                          // position: start at character 0
        fmx::Text::kSize_End,       // size: read all characters
        fmx::Text::kEncoding_UTF8
    );
    buf.resize(std::strlen(buf.c_str()));
    return buf;
}

void SetResultString(const std::string& s, fmx::Data& result)
{
    fmx::TextUniquePtr text = TextFromString(s);
    result.SetAsText(*text, result.GetLocale());
}

void SetResultOK(fmx::Data& result)
{
    SetResultString("OK", result);
}

void SetResultError(const std::string& message, fmx::Data& result)
{
    SetResultString("ERROR: " + message, result);
}
