#pragma once
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <cassert>

class responseWrapper
{
public:
    using stringContainer = std::vector<std::string>;
    using objectContainer = std::vector<responseWrapper>;

    enum index
    {
        strings,
        objects
    };

    //An element is either a text value or a collection of text values
    //using element = std::variant<std::string, responseWrapper>;
    using elementContainer = std::variant<stringContainer, objectContainer>;
private:
    //Elements are only arrays of strings OR objects, not mixed
    std::vector<std::pair<std::string, elementContainer>> elements;
    //Elements may be forced to use array representation ([]'s), otherwise they will only use it when multiple items are present
    std::vector<bool> forcedArrays;

    decltype(elements)::iterator find(std::string_view key)
    {
        for (auto it = elements.begin(); it != elements.end(); ++it)
        {
            if (it->first == key)
                return it;
        }
        return elements.end();
    }
    decltype(elements)::const_iterator find(std::string_view key) const
    {
        for (auto it = elements.cbegin(); it != elements.cend(); ++it)
        {
            if (it->first == key)
                return it;
        }
        return elements.cend();
    }

    //Single-value overload for appending
    static void elementToData(std::string& src, size_t depth, const std::string& value, bool prespace, bool pad)
    {
        if (pad && prespace)
            src.append(depth, '\t');
        if (pad)
        {
            src += "\"" + value + "\"";
        }
        else
        {
            src += value;
        }
    }
    //Multi-value overload for appending
    static void elementToData(std::string& src, size_t depth, const responseWrapper& value, const bool prespace, const bool pad)
    {
        value.toData(src, depth, prespace, pad);
    }

    static size_t size(const elementContainer& cont)
    {
        if (cont.index() == 0)
            return std::get<std::vector<std::string>>(cont).size();
        else
            return std::get<std::vector<responseWrapper>>(cont).size();
    }

    template <class T>
    static void containerToData(std::string& src, size_t depth, const std::vector<T>& values, bool forceArray, bool pad)
    {
        auto addPadding = [&](char val, size_t count = 1)
        {
            if (pad)
                src.append(count, val);
        };

        assert(!values.empty());
        if (values.size() > 1 || forceArray)
        {
            src += '[';
            addPadding('\n');
            for (const auto& i : values)
            {
                elementToData(src, depth + 1, i, true, pad);
                src += ',';
                addPadding('\n');
            }
            src.erase(src.end() - (size_t(1) + pad));
            addPadding('\t', depth);
            src += ']';
        }
        else
        {
            elementToData(src, depth, values.front(), false, pad);
        }

    }

    //Adds a single or multi-value element
    template <class T>
    void genericAdd(std::string_view key, T&& value, bool isForced)
    {
        auto it = find(key);
        constexpr bool isWrapper = std::is_same<T, responseWrapper>::value;
        if (it != elements.end() && it->second.index() != static_cast<size_t>(isWrapper))
        {
            assert(false);
        }
        if (it == elements.end())
        {
            elements.emplace_back(key, std::vector<T>());
            forcedArrays.emplace_back(isForced);
            it = elements.end() - 1;
        }
        std::get<std::vector<T>>(it->second).emplace_back(std::forward<T>(value));
    }


    //Converts the current element into a text form, accounting for tab depths and array/single layout
    //Optional padding includes
    //  -Tabs
    //  -Newlines
    //  -Quotations
    void toData(std::string& src, size_t depth, const bool prespace, const bool pad) const
    {
        auto addPadding = [&](char val, size_t count = 1)
        {
            if (pad)
                src.append(count, val);
        };

        if (prespace)
        {
            addPadding('\t', depth);
        }
        src += "{";
        addPadding('\n');
        depth += 1;
        for (size_t i = 0; i < elements.size(); i++)
        {
            const auto& [key, value] = elements[i];
            addPadding('\t', depth);
            elementToData(src, 0, key, false, false);
            src += ':';
            addPadding(' ');

            if (value.index() == 0)
                containerToData(src, depth, std::get<std::vector<std::string>>(value), forcedArrays[i], pad);
            else
                containerToData(src, depth, std::get<std::vector<responseWrapper>>(value), forcedArrays[i], pad);

            //if (size(value) > 1 || forcedArrays[i])
            //{
            //    src += '[';
            //    addPadding('\n');
            //    for (const auto& i : value)
            //    {
            //        std::visit([&](const auto& obj) {elementToData(src, depth + 1, obj, true, pad); }, i);
            //        src += ',';
            //        addPadding('\n');
            //    }
            //    src.erase(src.end() - (size_t(1) + pad));
            //    addPadding('\t', depth);
            //    src += ']';
            //}
            //else if (value.size() == 1)
            //{
            //    std::visit([&](const auto& obj) {elementToData(src, depth, obj, false, pad); }, value[0]);
            //}
            //else
            //{
            //    addPadding('\"', 2);
            //}

            src += ",";
            addPadding('\n');
        }
        if (!elements.empty())
        {
            src.erase(src.end() - (size_t(1) + pad));
        }
        addPadding('\t', depth - 1);
        src += "}";
    }

public:

    void add(std::string_view key, std::string_view value, bool forceArray = false)
    {
        genericAdd(key, std::string(value), forceArray);
    }

    void add(std::string_view key, responseWrapper&& value, bool forceArray = false)
    {
        genericAdd(key, std::move(value), forceArray);
    }

    std::optional<std::reference_wrapper<const elementContainer>> search(std::string_view key) const
    {
        const auto it = find(key);
        if (it == elements.cend())
        {
            return {};
        }
        else
        {
            return std::ref(it->second);
        }
    }

    std::string toData(const bool pad) const
    {
        std::string ret;
        toData(ret, 0, false, pad);
        return ret;
    }

    static std::optional<responseWrapper> fromData(std::string_view data)
    {
        if (data.empty())
            return responseWrapper();
        if (data.front() != '{' || data.back() != '}')
            return std::nullopt;

        auto match = [](std::string_view::const_iterator begin, std::string_view::const_iterator end, char open, char close)
        {
            assert(*begin == open);
            //Note the immediate rollover to 0 as begin is the open char
            size_t depth = -1;
            for (auto it = begin; it != end; ++it)
            {
                if (*it == open)
                    depth++;
                if (*it == close)
                {
                    if (depth == 0)
                    {
                        return it;
                    }
                    depth--;
                }
            }
            return end;
        };

        responseWrapper ret;

        auto left = data.cbegin() + 1;
        const auto end = data.cend() - 1;
        while (left != end)
        {
            const auto div = std::find(left, end, ':');
            if (div == end)
            {
                //No divisor found, return what was found and ignore
                return ret;
            }
            const std::string_view key{ &*left, size_t(div - left) };
            const auto next = std::next(div);

            ret.forcedArrays.push_back(*next == '[');

            if (*next == '{')
            {
                const auto blockEnd = match(next, end, '{', '}');
                auto val = fromData(std::string_view(&*next, size_t(blockEnd - div)));
                if (!val.has_value())
                    return std::nullopt;
                ret.elements.emplace_back(key, elementContainer{ std::vector<responseWrapper>{std::move(val.value())} });
                left = std::next(blockEnd);
                if (left != end)
                    ++left;
                continue;
            }
            else if (*next == '[')
            {
                const auto blockEnd = match(std::next(div), end, '[', ']');
                auto subleft = div + 2;
                if (*subleft == '{')
                {
                    //Array of objects
                    std::vector<responseWrapper> elements;

                    enum
                    {
                        ADDED,
                        ADDED_AT_END,
                        FAILED
                    };

                    auto add = [&]()
                    {
                        auto subdiv = match(subleft, blockEnd, '{', '}');
                        auto val = fromData({ &*subleft, size_t(subdiv - subleft) + 1 });
                        if (!val.has_value())
                            return FAILED;
                        elements.emplace_back(std::move(val.value()));

                        subleft = subdiv;
                        if (subdiv == blockEnd - 1)
                            return ADDED_AT_END;
                        subleft += 2;
                        return ADDED;
                    };


                    //Loop until no more can be added
                    auto result = ADDED;
                    do
                    {
                        result = add();
                        if (result == FAILED)
                            return std::nullopt;
                    } while (result != ADDED_AT_END);

                    ret.elements.emplace_back(key, std::move(elements));
                }
                else
                {
                    //Array of values
                    std::vector<std::string> elements;


                    auto add = [&]()
                    {
                        auto subdiv = std::find(subleft, blockEnd, ',');
                        elements.emplace_back(std::string(subleft, subdiv));
                        
                        subleft = subdiv;
                        if (subdiv == blockEnd)
                            return false;
                        ++subleft;
                        return true;
                    };

                    while (add()) {}

                    ret.elements.emplace_back(key, std::move(elements));
                }
                left = std::next(blockEnd);
                if (left != end)
                    ++left;
                continue;

                //std::vector<element> elements;



                //auto add = [&]()
                //{
                //    std::string_view::const_iterator subdiv;
                //    if (*subleft == '{')
                //    {
                //        //##
                //        subdiv = match(subleft, blockEnd, '{', '}');
                //        auto val = fromData({ &*subleft, size_t(subdiv - subleft) });
                //        if (!val.has_value())
                //            return FAILED;
                //        elements.emplace_back(std::move(val.value()));
                //    }
                //    else
                //    {
                //        subdiv = std::find(subleft, blockEnd, ',');
                //        elements.emplace_back(std::string(subleft, subdiv));
                //    }
                //    subleft = subdiv;
                //    if (subdiv == blockEnd)
                //        return ADDED_AT_END;
                //    ++subleft;
                //    return ADDED;
                //};

                ////Loop until no more can be added
                //auto result = ADDED;
                //do
                //{
                //    result = add();
                //    if (result == FAILED)
                //        return std::nullopt;
                //}
                //while (result != ADDED_AT_END);

                //ret.elements.emplace_back(key, std::move(elements));
                //left = std::next(blockEnd);
                //if (left != end)
                //    ++left;
                //continue;
            }
            else
            {
                const auto blockEnd = std::find(div, end, ',');
                ret.elements.emplace_back(key, elementContainer{ std::vector<std::string>{ std::string(std::next(div), blockEnd) } });
                left = blockEnd;
                if (left != end)
                    ++left;
                continue;
            }
        }

        return ret;
    }

    static std::optional<responseWrapper> fromUnformattedData(std::string data)
    {

        bool isQuote = false;
        data.erase(std::remove_if(data.begin(), data.end(),
            [&](const char v)
            {
                if (v == '\"')
                    isQuote = !isQuote;
                return (!isQuote && std::isspace(v));
            }), data.end());

        data.erase(std::remove_if(data.begin(), data.end(),
            [&](const char v)
            {
                return v == '\"';
            }), data.end());

        return fromData(data);
    }
};