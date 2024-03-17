#include <irods/client_connection.hpp>
#include <irods/genquery2.h>
#include <irods/irods_at_scope_exit.hpp>
#include <irods/irods_exception.hpp>
#include <irods/procApiRequest.h>
#include <irods/rcMisc.h> // For set_ips_display_name()
#include <irods/rodsClient.h> // For load_client_api_plugins()

#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <iterator>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>

auto print_usage_info() -> void;
auto print_column_mappings(const char* _json) -> void;

int main(int _argc, char* _argv[]) // NOLINT(modernize-use-trailing-return-type)
{
    set_ips_display_name("iquery (experimental)");

    namespace po = boost::program_options;

    po::options_description desc{""};

    // clang-format off
    desc.add_options()
        ("columns,c", po::bool_switch(), "")
        ("query_string", po::value<std::string>()->default_value("-"), "")
        ("sql-only", po::bool_switch(), "")
        ("zone,z", po::value<std::string>(), "")
        ("help,h", "");
    // clang-format on

    po::positional_options_description pod;
    pod.add("query_string", 1);

    load_client_api_plugins();

    try {
        po::variables_map vm;
        po::store(po::command_line_parser(_argc, _argv).options(desc).positional(pod).run(), vm);
        po::notify(vm);

        if (vm.count("help")) {
            print_usage_info();
            return 0;
        }

        Genquery2Input input{};
        std::string query_string;

        if (vm["columns"].as<bool>()) {
            input.column_mappings = 1;
        }
        else {
            query_string = vm["query_string"].as<std::string>();

            // Read from stdin.
            if ("-" == query_string) {
                query_string.clear();
                std::getline(std::cin, query_string);
            }

            if (query_string.empty()) {
                fmt::print(stderr, "error: Missing QUERY_STRING\n");
                return 1;
            }

            input.query_string = query_string.data();
        }

        std::string zone;
        if (vm.count("zone")) {
            zone = vm["zone"].as<std::string>();
            input.zone = zone.data();
        }

        if (vm["sql-only"].as<bool>()) {
            input.sql_only = 1;
        }

        irods::experimental::client_connection conn;
        char* output{};
        irods::at_scope_exit free_output{[&output] {
            if (output) {
                std::free(output);
            }
        }};

        const auto ec = rc_genquery2(static_cast<RcComm*>(conn), &input, &output);
        if (ec < 0) {
            fmt::print(stderr, "error: {}\n", ec);
            return 1;
        }

        if (1 == input.column_mappings) {
            print_column_mappings(output);
            return 0;
        }

        fmt::print("{}\n", output);

        return 0;
    }
    catch (const irods::exception& e) {
        fmt::print(stderr, "error: {}\n", e.client_display_what());
    }
    catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
    }

    return 1;
} // main

auto print_usage_info() -> void
{
    fmt::print(R"_(iquery - Query the iRODS Catalog

Usage: iquery [OPTION]... QUERY_STRING

Query the iRODS Catalog using GenQuery2.

QUERY_STRING is expected to be a string matching the GenQuery2 syntax. Failing
to meet this requirement will result in an error.

If QUERY_STRING is a hyphen (-) or is empty, input is read from stdin. Input
taken via stdin will be viewed as the QUERY_STRING to execute. For example:

    echo select COLL_NAME, DATA_NAME | iquery

Mandatory arguments to long options are mandatory for short options too.

Options:
  -c, --columns         List columns supported by GenQuery2.
      --sql-only        Print the SQL generated by the parser. The generated
                        SQL will not be executed.
  -z, --zone=ZONE_NAME  The name of the zone to run the query against. Defaults
                        to the local zone.
  -h, --help            Display this help message and exit.
)_");

    char name[] = "iquery (experimental)";
    printReleaseInfo(name);
} // print_usage_info

auto print_column_mappings(const char* _json) -> void
{
    const auto mappings = nlohmann::json::parse(_json);
    auto items = mappings.items();
    auto e = std::end(items);

    // Capture the size of the longest GenQuery2 column.
    std::size_t w = 0;
    std::for_each(std::begin(items), e, [&w](auto&& _v) { w = std::max(w, _v.key().size()); });

    // Print information about each column.
    std::for_each(std::begin(items), e, [w](auto&& _v) {
        auto items = _v.value().items();
        auto c = std::begin(items);

        if (c.key().empty()) {
            fmt::print("{:{}} (derived)\n", _v.key(), w);
            return;
        }

        fmt::print("{:{}} ({}.{})\n", _v.key(), w, c.key(), c.value().template get_ref<const std::string&>());
    });
} // print_column_mappings
