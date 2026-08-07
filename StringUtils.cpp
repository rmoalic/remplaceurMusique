#include "StringUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <sstream>

// ===========================================================================
// Time Conversion Functions
// ===========================================================================

std::wstring StringUtils::SecondsToHMS(double seconds)
{
    if (seconds < 0) seconds = 0;
    int totalSeconds = static_cast<int>(seconds);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;
    
    wchar_t buffer[32];
    swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, secs);
    return buffer;
}

double StringUtils::HMSToSeconds(const std::wstring& time)
{
    int hours = 0, minutes = 0;
    double seconds = 0;
    
    // Try to parse HH:MM:SS format
    if (swscanf_s(time.c_str(), L"%d:%d:%lf", &hours, &minutes, &seconds) >= 2) {
        return hours * 3600.0 + minutes * 60.0 + seconds;
    }
    
    // Try to parse MM:SS format
    if (swscanf_s(time.c_str(), L"%d:%lf", &minutes, &seconds) >= 1) {
        return minutes * 60.0 + seconds;
    }
    
    // Try to parse just seconds
    if (swscanf_s(time.c_str(), L"%lf", &seconds) == 1) {
        return seconds;
    }
    
    return -1.0;
}

long long StringUtils::HMSToMilliseconds(const std::wstring& time)
{
    double seconds = HMSToSeconds(time);
    if (seconds < 0) return -1;
    return static_cast<long long>(seconds * 1000.0);
}

std::wstring StringUtils::FormatDuration(double seconds)
{
    if (seconds < 0) seconds = 0;
    
    if (seconds < 60) {
        // Less than a minute - show just seconds
        return SecondsToHMS(seconds);
    } else if (seconds < 3600) {
        // Less than an hour - show MM:SS
        int totalSeconds = static_cast<int>(seconds);
        int minutes = totalSeconds / 60;
        int secs = totalSeconds % 60;
        wchar_t buffer[16];
        swprintf_s(buffer, L"%02d:%02d", minutes, secs);
        return buffer;
    } else {
        // Hour or more - show HH:MM:SS
        return SecondsToHMS(seconds);
    }
}

// ===========================================================================
// File Size Formatting
// ===========================================================================

std::wstring StringUtils::FormatFileSize(uint64_t bytes)
{
    const wchar_t* units[] = { L"o", L"Ko", L"Mo", L"Go", L"To" };
    const int numUnits = sizeof(units) / sizeof(units[0]);
    
    double size = static_cast<double>(bytes);
    int unitIndex = 0;
    
    while (size >= 1024 && unitIndex < numUnits - 1) {
        size /= 1024.0;
        unitIndex++;
    }
    
    wchar_t buffer[64];
    if (unitIndex == 0) {
        swprintf_s(buffer, L"%llu %s", bytes, units[unitIndex]);
    } else {
        swprintf_s(buffer, L"%.2f %s", size, units[unitIndex]);
    }
    
    return buffer;
}

// ===========================================================================
// String Manipulation Functions
// ===========================================================================

std::wstring StringUtils::ReplaceAll(std::wstring str, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return str;
    
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::wstring::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length();
    }
    return str;
}

std::wstring StringUtils::ToLower(const std::wstring& str)
{
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](wchar_t c) { return towlower(c); });
    return result;
}

std::wstring StringUtils::ToUpper(const std::wstring& str)
{
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](wchar_t c) { return towupper(c); });
    return result;
}

std::wstring StringUtils::Trim(const std::wstring& str)
{
    if (str.empty()) return str;
    
    size_t start = str.find_first_not_of(L" \t\n\r\f\v");
    size_t end = str.find_last_not_of(L" \t\n\r\f\v");
    
    if (start == std::wstring::npos || end == std::wstring::npos) {
        return L"";
    }
    
    return str.substr(start, end - start + 1);
}

// ===========================================================================
// String Comparison Functions
// ===========================================================================

bool StringUtils::StartsWith(const std::wstring& str, const std::wstring& prefix, bool caseSensitive)
{
    if (prefix.empty()) return true;
    if (str.length() < prefix.length()) return false;
    
    if (caseSensitive) {
        return str.compare(0, prefix.length(), prefix) == 0;
    } else {
        return ToLower(str.substr(0, prefix.length())) == ToLower(prefix);
    }
}

bool StringUtils::EndsWith(const std::wstring& str, const std::wstring& suffix, bool caseSensitive)
{
    if (suffix.empty()) return true;
    if (str.length() < suffix.length()) return false;
    
    if (caseSensitive) {
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    } else {
        return ToLower(str.substr(str.length() - suffix.length())) == ToLower(suffix);
    }
}

// ===========================================================================
// String Splitting and Joining
// ===========================================================================

std::vector<std::wstring> StringUtils::Split(const std::wstring& str, const std::wstring& delimiter)
{
    std::vector<std::wstring> tokens;
    
    if (delimiter.empty()) {
        tokens.push_back(str);
        return tokens;
    }
    
    size_t start = 0;
    size_t end = str.find(delimiter);
    
    while (end != std::wstring::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    
    tokens.push_back(str.substr(start));
    return tokens;
}

std::wstring StringUtils::Join(const std::vector<std::wstring>& tokens, const std::wstring& delimiter)
{
    if (tokens.empty()) return L"";
    if (tokens.size() == 1) return tokens[0];
    
    std::wstringstream result;
    result << tokens[0];
    
    for (size_t i = 1; i < tokens.size(); ++i) {
        result << delimiter << tokens[i];
    }
    
    return result.str();
}

// ===========================================================================
// String Validation
// ===========================================================================

bool StringUtils::IsEmptyOrWhitespace(const std::wstring& str)
{
    if (str.empty()) return true;
    
    for (wchar_t c : str) {
        if (!iswspace(c)) {
            return false;
        }
    }
    return true;
}

// ===========================================================================
// File Path Utilities
// ===========================================================================

std::wstring StringUtils::GetFileExtension(const std::wstring& filePath)
{
    size_t dotPos = filePath.rfind(L'.');
    size_t slashPos = filePath.find_last_of(L"\\/");
    
    if (dotPos == std::wstring::npos) {
        return L"";
    }
    
    // If there's a slash after the last dot, it's not a file extension
    if (slashPos != std::wstring::npos && dotPos < slashPos) {
        return L"";
    }
    
    return filePath.substr(dotPos + 1);
}

std::wstring StringUtils::GetFileName(const std::wstring& filePath)
{
    size_t slashPos = filePath.find_last_of(L"\\/");
    
    if (slashPos == std::wstring::npos) {
        return filePath;
    }
    
    return filePath.substr(slashPos + 1);
}

std::wstring StringUtils::GetFileNameWithoutExtension(const std::wstring& filePath)
{
    std::wstring fileName = GetFileName(filePath);
    size_t dotPos = fileName.rfind(L'.');
    
    if (dotPos == std::wstring::npos) {
        return fileName;
    }
    
    return fileName.substr(0, dotPos);
}
