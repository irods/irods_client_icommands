#include <irods/rodsClient.h>
#include <irods/irods_version.h>
#include <irods/client_connection.hpp>
#include <irods/filesystem.hpp>
#include <irods/transport/default_transport.hpp>
#include <irods/dstream.hpp>
#include <irods/replica.hpp>
#include <irods/query_builder.hpp>
#include <irods/irods_exception.hpp>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <iostream>
#include <string>
#include <string_view>
#include <chrono>
#include <stdexcept>

// clang-format off

//
// Namespace / Type Aliases
//

namespace ix = irods::experimental;
namespace fs = irods::experimental::filesystem;
namespace io = irods::experimental::io;
namespace po = boost::program_options;

using rep_type            = fs::object_time_type::duration::rep;
using replica_number_type = int;

//
// Helper Functions (Prototypes)
//

auto print_usage_info() noexcept -> void;

auto print_version_info() noexcept -> void;

auto is_input_valid(const po::variables_map& _vm) noexcept -> bool;

auto canonical(const std::string& _path, const rodsEnv& _env) -> std::optional<std::string>;

auto get_time(ix::client_connection& _conn,
              const rodsEnv& _env,
              const po::variables_map& _vm) -> fs::object_time_type;

auto create_data_object_if_necessary(ix::client_connection& _conn,
                                     const rodsEnv& _env,
                                     const po::variables_map& _vm,
                                     const fs::path& _path) -> void;

auto replica_exists(ix::client_connection& _conn,
                    const fs::path& _path,
                    replica_number_type _replica_number) -> bool;

auto get_replica_number(ix::client_connection& _conn,
                        const po::variables_map& _vm,
                        const fs::path& _path) -> replica_number_type;

// clang-format on

int main(int _argc, char* _argv[])
{
    rodsEnv env;

    if (getRodsEnv(&env) < 0) {
        std::cerr << "Error: Could not get iRODS environment.\n";
        return 1;
    }

    po::options_description desc{""};
    desc.add_options()
        ("no-create,c", "")
        ("replica,n", po::value<replica_number_type>(), "")
        ("resource,R", po::value<std::string>(), "")
        ("reference,r", po::value<std::string>(), "")
        ("seconds-since-epoch,s", po::value<rep_type>(), "")
        ("logical_path", po::value<std::string>(), "")
        ("help,h", "")
        ("version,v", "");

    po::positional_options_description pod;
    pod.add("logical_path", 1);

    try {
        po::variables_map vm;
        po::store(po::command_line_parser(_argc, _argv).options(desc).positional(pod).run(), vm);
        po::notify(vm);

        if (vm.count("help")) {
           print_usage_info(); 
           return 0;
        }

        if (vm.count("version")) {
            print_version_info();
            return 0;
        }

        if (!is_input_valid(vm)) {
            return 1;
        }

        load_client_api_plugins();

        std::string path;

        if (auto v = canonical(vm["logical_path"].as<std::string>(), env); v) {
            path = *v;
        }
        else {
            std::cerr << "Error: Failed to convert path to absolute path.\n";
            return 1;
        }

        ix::client_connection conn;

        create_data_object_if_necessary(conn, env, vm, path);

        //
        // The target object exists. Update the mtime based on the arguments passed by the user.
        //

        const auto object_status = fs::client::status(conn, path);

        if (!fs::client::is_collection(object_status) && !fs::client::is_data_object(object_status)) {
            std::cerr << "Error: Logical path does not point to a collection or data object.\n";
            return 1;
        }

        const auto new_mtime = get_time(conn, env, vm);

        if (fs::client::is_collection(object_status)) {
            // Return immediately if invalid options are present in regards to the target.
            if (vm.count("replica") || vm.count("resource")) {
                std::cerr << "Error: -n and -R cannot be used for collections.\n";
                return 1;
            }

            if (fs::client::is_collection_registered(conn, path)) {
                fs::client::last_write_time(conn, path, new_mtime);
            }
            else {
                // TODO Should anything happen here?
            }
        }
        else if (fs::client::is_data_object(object_status)) {
            if (fs::client::is_data_object_registered(conn, path)) {
                const auto replica_number = get_replica_number(conn, vm, path);
                ix::replica::last_write_time<RcComm>(conn, path, replica_number, new_mtime);
            }
            else {
                // TODO Should anything happen here?
            }
        }
        else {
            // TODO Should anything happen here?
        }
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    catch (const irods::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

auto print_usage_info() noexcept -> void
{
    std::cout << R"_(itouch - Change logical path timestamp

Usage: itouch [OPTION]... LOGICAL_PATH

Update the modification time of a logical path to the current time.

A LOGICAL_PATH argument that does not exist will be created as an empty
data object, unless -c is supplied.

If a replica number or leaf resource is not specified, the latest good
replica will be updated.

Mandatory arguments to long options are mandatory for short options too.

Options:
  -c, --no-create  Do not create a data object.
  -n, --replica    The replica number of the replica to update.  This
                   option applies to data objects only.  Cannot be
                   used with -R.
  -R, --resource   The leaf resource that contians the replica to update.
                   This option applies to data objects only.  Cannot be
                   used with -n.
  -r, --reference=LOGICAL_PATH
                   Use the modification time of LOGICAL_PATH instead
                   of the current time.  Cannot be used with -s.
  -s, --seconds-since-epoch=SECONDS
                   Use SECONDS instead of the current time.  Cannot
                   be used with -r.
  -h, --help       Display this help message and exit.
  -v, --version    Display version information and exit.
)_";

    printReleaseInfo("itouch");
}

auto print_version_info() noexcept -> void
{
    // The empty string argument following the version macros exists so
    // that the fourth format identifier is satisfied. The job of the
    // fourth format identifier is simply for white-space formatting.
    fmt::print("iRODS Version {}.{}.{}{: >16}itouch\n", IRODS_VERSION_MAJOR, IRODS_VERSION_MINOR, IRODS_VERSION_PATCHLEVEL, "");
}

auto is_input_valid(const po::variables_map& _vm) noexcept -> bool
{
    if (_vm.count("replica") && _vm.count("resource")) {
        std::cerr << "Error: -n and -R cannot be used together.\n";
        return false;
    }

    if (_vm.count("reference") && _vm.count("seconds-since-epoch")) {
        std::cerr << "Error: -r and -s cannot be used together.\n";
        return false;
    }

    if (_vm.count("logical_path") == 0) {
        std::cerr << "Error: Missing logical path.\n";
        return false;
    }

    return true;
}

auto canonical(const std::string& _path, const rodsEnv& _env) -> std::optional<std::string>
{
    rodsPath_t input{};
    rstrcpy(input.inPath, _path.data(), MAX_NAME_LEN);

    if (parseRodsPath(&input, const_cast<rodsEnv*>(&_env)) != 0) {
        return std::nullopt;
    }

    auto* escaped_path = escape_path(input.outPath);
    std::optional<std::string> p = escaped_path;
    std::free(escaped_path);

    return p;
}

auto get_time(ix::client_connection& _conn,
              const rodsEnv& _env,
              const po::variables_map& _vm) -> fs::object_time_type
{
    if (_vm.count("reference")) {
        std::string ref_path;

        if (auto v = canonical(_vm["reference"].as<std::string>(), const_cast<rodsEnv&>(_env)); v) {
            ref_path = *v;
        }
        else {
            throw std::runtime_error{"Failed to convert reference path to absolute path."};
        }

        return fs::client::last_write_time(_conn, ref_path);
    }

    using duration_type = fs::object_time_type::duration;

    if (_vm.count("seconds-since-epoch")) {
        return fs::object_time_type{duration_type{_vm["seconds-since-epoch"].as<rep_type>()}};
    }

    return std::chrono::time_point_cast<duration_type>(fs::object_time_type::clock::now());
}

auto create_data_object_if_necessary(ix::client_connection& _conn,
                                     const rodsEnv& _env,
                                     const po::variables_map& _vm,
                                     const fs::path& _path) -> void
{
    if (fs::client::exists(_conn, _path)) {
        return;
    }

    // Return immediately if the object does not exist and the user specified to not
    // create any new data objects.
    if (_vm.count("no-create")) {
        throw std::runtime_error{"Cannot update modification time (object does not exist)."};
    }

    if (_vm.count("replica")) {
        throw std::runtime_error{"Replica numbers cannot be used when creating new data objects."};
    }

    // Create a new data object if the path does not end with a path separator.
    io::client::default_transport tp{_conn};

    // Create the data object at the leaf resource if the user provided one.
    if (_vm.count("resource")) {
        io::odstream{tp, _path, io::leaf_resource_name{_vm["resource"].as<std::string>()}};
        return;
    }

    // TODO Find better way to handle this.
    if (std::strlen(_env.rodsDefResource) == 0) {
        throw std::runtime_error{"Cannot create data object. User did not specify a leaf "
                                 "resource and no default resource defined."};
    }

    // Create a data object at the default resource.
    io::odstream{tp, _path, io::leaf_resource_name{_env.rodsDefResource}};
}

auto replica_exists(ix::client_connection& _conn,
                    const fs::path& _path,
                    replica_number_type _replica_number) -> bool
{
    ix::query_builder qb;

    if (const auto zone = fs::zone_name(_path); zone) {
        qb.zone_hint(*zone);
    }

    const auto gql = fmt::format("select DATA_ID "
                                 "where"
                                 " COLL_NAME = '{}' and"
                                 " DATA_NAME = '{}' and"
                                 " DATA_REPL_NUM = '{}'",
                                 _path.parent_path().c_str(),
                                 _path.object_name().c_str(),
                                 _replica_number);

    return qb.build<RcComm>(_conn, gql).size() > 0;
}

auto get_replica_number(ix::client_connection& _conn,
                        const po::variables_map& _vm,
                        const fs::path& _path) -> replica_number_type
{
    // Return the replica number passed by the user.
    if (_vm.count("replica")) {
        const auto replica_number = _vm["replica"].as<replica_number_type>();

        if (replica_number < 0) {
            throw std::runtime_error{"Replica numbers cannot be less than zero."};
        }

        if (!replica_exists(_conn, _path, replica_number)) {
            throw std::runtime_error{"Replica does not exist matching that replica number."};
        }

        return replica_number;
    }

    // Convert the passed resource to a replica number and return it.
    if (_vm.count("resource")) {
        const auto resc_name = _vm["resource"].as<std::string>();

        ix::query_builder qb;

        if (const auto zone = fs::zone_name(_path); zone) {
            qb.zone_hint(*zone);
        }

        const auto gql = fmt::format("select DATA_REPL_NUM "
                                     "where"
                                     " COLL_NAME = '{}' and"
                                     " DATA_NAME = '{}' and"
                                     " RESC_NAME = '{}'",
                                     _path.parent_path().c_str(),
                                     _path.object_name().c_str(),
                                     resc_name);

        for (auto&& row : qb.build<RcComm>(_conn, gql)) {
            return std::stoi(row[0]);
        }

        throw std::runtime_error{"Replica does not exist in resource."};
    }

    // The user did not specify a target replica, so fetch the replica number
    // of the latest good replica.

    ix::query_builder qb;

    if (const auto zone = fs::zone_name(_path); zone) {
        qb.zone_hint(*zone);
    }

    // Fetch only good replicas (i.e. DATA_REPL_STATUS = '1').
    const auto gql = fmt::format("select DATA_MODIFY_TIME, DATA_REPL_NUM "
                                 "where"
                                 " COLL_NAME = '{}' and"
                                 " DATA_NAME = '{}' and"
                                 " DATA_REPL_STATUS = '1'",
                                 _path.parent_path().c_str(),
                                 _path.object_name().c_str());

    auto query = qb.build<RcComm>(_conn, gql);

    if (query.size() == 0) {
        throw std::runtime_error{"No good replicas found for path."};
    }

    std::uintmax_t latest_mtime = 0;
    replica_number_type replica_number = -1;

    for (auto&& row : query) {
        const auto mtime = std::stoull(row[0]);

        if (mtime > latest_mtime) {
            latest_mtime = mtime;
            replica_number = std::stoi(row[1]);
        }
    }

    return replica_number;
}

