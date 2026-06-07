#pragma once
#include <string>
#include <vector>
#include <cstdint>

// A single filter rule with colour and match criteria
struct FilterRule
{
    std::string name;           // display name
    bool        enabled{true};

    // Match criteria (all non-empty fields must match, AND logic)
    std::string payloadRegex;   // regex on full raw line
    std::string fixedPayload;   // fixed substring in raw line
    std::string application;    // exact or partial match on service field
    std::string tsFrom;         // ISO timestamp >= (empty = no bound)
    std::string tsTo;           // ISO timestamp <= (empty = no bound)

    // Display colour (ARGB stored as 0xAARRGGBB)
    uint32_t    bgColour{0xFFFFFFFF};  // row background
    uint32_t    fgColour{0xFF000000};  // row foreground
};

class FilterConfig
{
public:
    std::vector<FilterRule>& Rules()             { return m_rules; }
    const std::vector<FilterRule>& Rules() const { return m_rules; }

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void AddRule(const FilterRule& r)   { m_rules.push_back(r); }
    void RemoveRule(std::size_t idx);
    void MoveUp(std::size_t idx);
    void MoveDown(std::size_t idx);

private:
    std::vector<FilterRule> m_rules;
};
