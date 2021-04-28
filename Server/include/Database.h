#pragma once
#include "sqlite3.h"
#include <vector>
#include <string>
#include <cassert>
#include <optional>
#include <unordered_map>


//A simple wrapper around SQLite error codes
//Is either good or not
struct SQLCode final
{
    int errorCode = SQLITE_OK;

    SQLCode(int val) : errorCode(val) {}

    bool isOK() const { return errorCode == SQLITE_OK; }
    operator bool() const { return isOK(); }
};

//The result of a database query, only includes requested rows
class SQLResult final
{
    size_t colCount;
    //Store elements continuously to save some space and provide more cache friendly linear access (despite double deref from strings)
    std::vector<std::string> data;
    std::vector<std::string> colNames;

    [[nodiscard]]
    SQLCode execute(sqlite3_stmt* statement)
    {
        int status;
        while (true)
        {
            status = sqlite3_step(statement);

            //The statement can be repeatedly looped until status stops being SQLITE_ROW, at which point it is either SQLITE_OK or an error code
            if (status == SQLITE_ROW)
            {
                for (int i = 0; i < colCount; i++)
                {
                    const auto val = sqlite3_column_text(statement, i);
                    if (val == nullptr)
                        data.emplace_back("NULL");
                    else
                    {
                        //Cast from unsigned char array to singed char array
                        data.emplace_back(reinterpret_cast<const char* const>(val));
                    }
                }
            }
            else
            {
                break;
            }
        }

        sqlite3_finalize(statement);

        if (status == SQLITE_DONE)
        {
            return SQLCode(SQLITE_OK);
        }
        return SQLCode(status);

    }

    const std::string& get(size_t row, size_t col) const { return data[row * colCount + col]; }

    //Public construction not allowed, static function must be used
    SQLResult() = delete;
    SQLResult(int size) : colCount(size) {}

    //Allows for [x][y] style access
    class accessWrapper
    {
        const SQLResult& result;
        const size_t row;
    public:
        accessWrapper(const SQLResult& obj, size_t targetRow) : result(obj), row(targetRow) {}

        const std::string& operator[](size_t col) const { return result.get(row, col); }
    };

public:

    static std::pair<SQLCode, const SQLResult> query(sqlite3* database, std::string_view SQL, const std::unordered_map<std::string_view, std::string_view>& namedParams)
    {
        sqlite3_stmt* statement = nullptr;
        assert(SQL.size() <= std::numeric_limits<int>::max());
        sqlite3_prepare_v2(database, SQL.data(), static_cast<int>(SQL.size()), &statement, nullptr);
        //Statement is not assigned a value if SQL parsing fails
        if (statement == nullptr)
        {
            return { SQLITE_ERROR, SQLResult(0) };
        }

        const auto m = sqlite3_bind_parameter_count(statement);

        for (const auto& [name, value] : namedParams)
        {
            int id = sqlite3_bind_parameter_index(statement, name.data());
            if (id != 0)
                sqlite3_bind_text(statement, id, value.data(), static_cast<int>(value.size()), SQLITE_STATIC);
            else
            {
                sqlite3_finalize(statement);
                return { SQLITE_ERROR, SQLResult(0) };
            }
        }

        SQLResult ret(sqlite3_column_count(statement));

        ret.colNames.reserve(ret.colCount);
        for (int i = 0; i < ret.colCount; i++)
        {
            ret.colNames.emplace_back(sqlite3_column_name(statement, i));
        }

        const SQLCode res = ret.execute(statement);
        return { res, ret };
    }

    size_t columnCount() const { return colCount; }
    size_t rowCount() const { return data.size() / colCount; }

    const accessWrapper operator[](size_t row) const { return accessWrapper(*this, row); }

    const std::string& getColName(size_t col) const { return colNames[col]; }
};

//Represents a database
class sqlite3DB final
{
    sqlite3* database;
public:

    using callbackFunction = int(*)(void*, int, char**, char**);

    sqlite3DB() = delete;
    //RAM-Database overload
    sqlite3DB(std::nullptr_t)
    {
        int result = sqlite3_open(nullptr, &database);
        if (result != SQLITE_OK)
        {
            database = nullptr;
        }
    }
    sqlite3DB(std::string_view source)
    {
        int result = sqlite3_open(source.data(), &database);
        if (result != SQLITE_OK)
        {
            database = nullptr;
        }
    }

    sqlite3DB(const sqlite3DB&) = delete;
    sqlite3DB(sqlite3DB&& move) noexcept
    {
        database = move.database;
        move.database = nullptr;
    }

    sqlite3DB& operator=(const sqlite3DB&) = delete;
    sqlite3DB& operator=(sqlite3DB&& move) noexcept
    {
        std::swap(database, move.database);
        return *this;
    }

    ~sqlite3DB()
    {
        if (database != nullptr)
        {
            sqlite3_close(database);
        }
    }

    bool isOpen() const
    {
        return database != nullptr;
    }

    std::pair<SQLCode, SQLResult> query(std::string_view SQL, const std::unordered_map<std::string_view, std::string_view>& namedParams)
    {
        return SQLResult::query(database, SQL, namedParams);
    }
};

class body;

//Simple function to wrap the input in %%'s, useful for some SQL queries
std::string generateLIKEArgument(std::string_view val);
//Function to modify the inputs in order to create an SQL statement that LIKE matches all inputs
std::string generateUpdateStatement(const body& b, const std::unordered_map<std::string, std::string>& relations);
