#include "FilterConfig.h"
#include <json/json.h>
#include <fstream>
#include <algorithm>

static uint32_t colFromStr(const std::string& s)
{
    if (s.size() == 9 && s[0] == '#')
    {
        unsigned long v = std::stoul(s.substr(1), nullptr, 16);
        return (uint32_t)v;
    }
    return 0xFFFFFFFF;
}

static std::string strFromCol(uint32_t c)
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%08X", c);
    return buf;
}

bool FilterConfig::LoadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;

    Json::Value  root;
    Json::Reader reader;
    if (!reader.parse(f, root)) return false;

    m_rules.clear();
    const Json::Value& arr = root["rules"];
    for (const auto& v : arr)
    {
        FilterRule r;
        r.name         = v.get("name",         "").asString();
        r.enabled      = v.get("enabled",       true).asBool();
        r.payloadRegex = v.get("payloadRegex",  "").asString();
        r.fixedPayload = v.get("fixedPayload",  "").asString();
        r.application  = v.get("application",   "").asString();
        r.tsFrom       = v.get("tsFrom",        "").asString();
        r.tsTo         = v.get("tsTo",          "").asString();
        r.bgColour     = colFromStr(v.get("bgColour", "#FFFFFFFF").asString());
        r.fgColour     = colFromStr(v.get("fgColour", "#FF000000").asString());
        m_rules.push_back(r);
    }
    return true;
}

bool FilterConfig::SaveToFile(const std::string& path) const
{
    Json::Value arr(Json::arrayValue);
    for (const auto& r : m_rules)
    {
        Json::Value v;
        v["name"]         = r.name;
        v["enabled"]      = r.enabled;
        v["payloadRegex"] = r.payloadRegex;
        v["fixedPayload"] = r.fixedPayload;
        v["application"]  = r.application;
        v["tsFrom"]       = r.tsFrom;
        v["tsTo"]         = r.tsTo;
        v["bgColour"]     = strFromCol(r.bgColour);
        v["fgColour"]     = strFromCol(r.fgColour);
        arr.append(v);
    }

    Json::Value root;
    root["rules"] = arr;

    Json::StyledWriter writer;
    std::ofstream f(path);
    if (!f) return false;
    f << writer.write(root);
    return true;
}

void FilterConfig::RemoveRule(std::size_t idx)
{
    if (idx < m_rules.size())
        m_rules.erase(m_rules.begin() + (std::ptrdiff_t)idx);
}

void FilterConfig::MoveUp(std::size_t idx)
{
    if (idx > 0 && idx < m_rules.size())
        std::swap(m_rules[idx], m_rules[idx-1]);
}

void FilterConfig::MoveDown(std::size_t idx)
{
    if (idx + 1 < m_rules.size())
        std::swap(m_rules[idx], m_rules[idx+1]);
}
