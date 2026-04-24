#include "json_parser.h"
#include "logger.h"

std::string ExtractFirstJsonObject(const std::string& json) {
    size_t start = json.find('{');
    if (start == std::string::npos) return "";

    int depth = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = start; i < json.length(); i++) {
        char c = json[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && inString) {
            escape = true;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            continue;
        }

        if (!inString) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    return json.substr(start, i - start + 1);
                }
            }
        }
    }

    return "";
}

std::string ParseJsonString(const std::string& json, const std::string& key) {
    LogDebug("Parsing JSON for key: " + key);

    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) {
        LogDebug("Key not found in JSON: " + key);
        LogJsonParse(json, key, "");
        return "";
    }
    LogDebug("Key found at position: " + std::to_string(keyPos));

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) {
        LogDebug("Colon not found after key: " + key);
        LogJsonParse(json, key, "");
        return "";
    }

    size_t quoteStart = json.find('"', colonPos);
    if (quoteStart == std::string::npos) {
        LogDebug("Quote not found after colon for key: " + key);
        LogJsonParse(json, key, "");
        return "";
    }

    std::string result;
    bool escape = false;
    for (size_t i = quoteStart + 1; i < json.length(); i++) {
        char c = json[i];
        if (escape) {
            switch (c) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (i + 4 < json.length()) {
                        unsigned int codepoint = 0;
                        bool validHex = true;
                        for (int j = 0; j < 4; j++) {
                            codepoint <<= 4;
                            char hex = json[i + 1 + j];
                            if (hex >= '0' && hex <= '9') codepoint |= (hex - '0');
                            else if (hex >= 'a' && hex <= 'f') codepoint |= (hex - 'a' + 10);
                            else if (hex >= 'A' && hex <= 'F') codepoint |= (hex - 'A' + 10);
                            else { validHex = false; break; }
                        }
                        if (validHex) {
                            if (codepoint < 0x80) {
                                result += static_cast<char>(codepoint);
                            } else if (codepoint < 0x800) {
                                result += static_cast<char>(0xC0 | (codepoint >> 6));
                                result += static_cast<char>(0x80 | (codepoint & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (codepoint >> 12));
                                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (codepoint & 0x3F));
                            }
                            i += 4;
                        } else {
                            result += '?';
                        }
                    } else {
                        result += '?';
                    }
                    break;
                }
                default: result += c; break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            LogJsonParse(json, key, result);
            LogDebug("Parsed value for key '" + key + "': " + result);
            return result;
        } else {
            result += c;
        }
    }

    LogJsonParse(json, key, result);
    LogDebug("Parsed value for key '" + key + "': " + result);
    return result;
}
