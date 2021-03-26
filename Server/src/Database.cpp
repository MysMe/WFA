#include "Database.h"
#include "Network.h"

std::string generateLIKEArgument(std::string_view val)
{
    std::string ret = "'%";
    ret.append(val.data(), val.size());
    ret += "%'";
    return ret;
}

std::string generateUpdateStatement(const body& b, const std::unordered_map<std::string, std::string>& relations)
{
    std::string ret;
    for (const auto& [bodyElementName, tableElementName] : relations)
    {
        if (!b.hasElement(bodyElementName))
            continue;

        if (!ret.empty())
            ret += ",";
        const auto& val = b.getElement(bodyElementName);
        ret += tableElementName;
        ret += " = \"";
        ret.append(val.data(), val.size());
        ret += "\"";
    }
    return ret;
}
