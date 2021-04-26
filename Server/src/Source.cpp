#include "Network.h"
#include "Database.h"
void printResult(const SQLResult& result)
{
    if (result.columnCount() == 0 || result.rowCount() == 0)
        return;

    std::vector<size_t> sizes(result.columnCount(), 0);
    for (size_t r = 0; r < result.rowCount(); r++)
    {
        for (size_t c = 0; c < result.columnCount(); c++)
        {
            if (result[r][c].size() > sizes[c])
                sizes[c] = result[r][c].size();
        }
    }

    std::cout << '|';
    for (size_t c = 0; c < result.columnCount(); c++)
    {
        if (result.getColName(c).size() > sizes[c])
            sizes[c] = result.getColName(c).size();

        std::cout << result.getColName(c);
        for (size_t i = result.getColName(c).size(); i < sizes[c]; i++)
        {
            std::cout << ' ';
        }
        std::cout << '|';
    }
    std::cout << "\n";

    for (size_t row = 0; row < result.rowCount(); row++)
    {
        std::cout << '|';
        for (size_t col = 0; col < result.columnCount(); col++)
        {
            std::cout << result[row][col];
            for (size_t i = result[row][col].size(); i < sizes[col]; i++)
            {
                std::cout << ' ';
            }
            std::cout << '|';
        }
        std::cout << "\n";
    }
}

void displayTables(sqlite3DB& DB, const std::string_view&)
{
    const auto [code, data] = DB.query(std::string_view("SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name"), {});
    if (!code)
    {
        std::cout << "Error accessing database metadata.\n";
    }
    else
    {
        printResult(data);
    }
}
void displayRows(sqlite3DB& DB, const std::string_view& args)
{
    const auto [code, data] = DB.query("SELECT * FROM " + std::string(args), {});
    if (!code)
    {
        std::cout << "Error displaying table rows.\n";
    }
    else
    {
        printResult(data);
    }
}

void autoexec(sqlite3DB& DB, const std::string_view& args)
{
    std::ifstream in(args.data());
    if (!in)
    {
        std::cout << "Error opening file.\n";
        return;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        std::cout << ">># " << line << '\n';
        const auto [status, result] = DB.query(line, {});
        if (!status)
        {
            std::cout << "!!! Command failed, stopping autoexecution.\n";
            break;
        }
        else
        {
            if (result.columnCount() > 0)
            {
                std::cout << "Output:\n";
                printResult(result);
            }
        }
    }

    std::cout << "Done.\n";
}

void load(sqlite3DB& DB, const std::string_view& args)
{
    DB = sqlite3DB(args);
    if (!DB.isOpen())
        std::cout << "Failed to open " << args << ".\n";
    else
        std::cout << "Opened DB file.\n";
}

std::unordered_map<std::string, std::function<void(sqlite3DB&, const std::string_view&)>> generateControlSequences()
{
    decltype(generateControlSequences()) ret;
    ret["dt"] = displayTables;
    ret["dr"] = displayRows;
    ret["ax"] = autoexec;
    ret["ld"] = load;
    return ret;
}

#include "Network.h"

void db()
{
    const auto sequences = generateControlSequences();
    std::string input;

    while (true)
    {
        std::getline(std::cin, input);
        if (input.empty()) continue;
        if (input.front() == '\\')
        {
            if (input.size() == 1)
            {
                std::cout << "Error: Empty control sequence.\n";
                continue;
            }

            std::string::const_iterator it = std::find(input.cbegin(), input.cend(), ' ');
            const std::string sequence{ input.cbegin() + 1, it };
            const std::string_view arguments = { input.data() + sequence.size() + 2, input.size() - sequence.size() - 2 };

            if (sequence == "\\")
                return;
            if (sequences.count(sequence) == 0)
            {
                std::cout << "Error: Invalid control sequence.\n";
                continue;
            }
            sequences.at(sequence)(*serverData::database, arguments);
            continue;
        }
        else
        {
            const auto [status, result] = serverData::database->query(input, {});
            if (status)
            {
                printResult(result);
                std::cout << "Done.\n";
            }
            else
            {
                std::cout << "SQL execution failed.\n";
            }
        }
    }
}

int main()      
{
    sqlite3DB DB(nullptr);
    {
        if (!DB.isOpen())
        {
            std::cout << "Failed to open database.\n";
            std::cin.ignore();
            std::terminate();
        }
    }
    serverData::database = &DB;
    authenticator auth;
    serverData::auth = &auth;

    std::cout << "Database ready:\n";
    db();

    auto [status, result] = DB.query("INSERT INTO USERS(ID, USERNAME, PASSWORD, PERMISSIONS) VALUES(0, \"ADMIN\", :HAS, 3);", { {":HAS", auth.hash("ADMIN")} });
    if (!status)
    {
        std::cout << "Failed to insert admin user.\n";
    }

    net();
    std::cin.ignore();
}