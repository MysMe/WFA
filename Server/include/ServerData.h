#pragma once
#include <vector>
#include <string>

class sqlite3DB;
class authenticator;

struct serverData
{
	static sqlite3DB* database;
	static authenticator* auth;

	//Each entry matches directly to a value in tableNames, do not change the order of one without changing the order of the other
	enum tables
	{
		USER,
		SUPPLIERS,
		PARTGROUPS,
		PARTS,
		PARTSINSERVICE,
		VEHICLESHARED,
		VEHICLES,
		SERVICESHARED,
		SERVICEUNAUTHORISED,
		SERVICEACTIVE,
		SERVICEOPEN,
		SERVICECLOSED
	};
	static const std::vector<std::string> tableNames;
};